"""Latest-frame YOLO face detector publishing results through Zenoh."""

from __future__ import annotations

import logging
import threading
import time
from dataclasses import dataclass
from typing import Any, Protocol

from .detection_protocol import DetectionBox, DetectionResult, encode_detection
from .protocol import FrameMetadata, ProtocolError, decode_frame_metadata
from .settings import Settings, load_settings
from .zenoh_transport import open_zenoh_session

LOG = logging.getLogger(__name__)


class DetectionPublisher(Protocol):
    def publish_detection(self, result: DetectionResult) -> None: ...

    def publish_status(self, status: dict[str, object]) -> None: ...


@dataclass(frozen=True, slots=True)
class CameraSample:
    jpeg: bytes
    metadata: FrameMetadata


class LatestCameraSample:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sample: CameraSample | None = None
        self._last_key: tuple[str, int] | None = None
        self.dropped_frames = 0

    def update(self, jpeg: bytes, metadata: FrameMetadata) -> bool:
        key = (metadata.source_instance_id, metadata.sequence)
        with self._lock:
            if key == self._last_key:
                return False
            if self._sample is not None:
                self.dropped_frames += 1
            self._sample = CameraSample(bytes(jpeg), metadata)
            self._last_key = key
            return True

    def snapshot(self) -> CameraSample | None:
        with self._lock:
            return self._sample


class FaceDetectorLoop:
    """Inference seam: caller supplies decoder and model, loop owns latest-only rate limiting."""

    def __init__(self, publisher: DetectionPublisher, *, inference_hz: int) -> None:
        self._publisher = publisher
        self._inference_hz = inference_hz
        self._next_inference_at = 0.0
        self._last_processed_key: tuple[str, int] | None = None
        self.processed_frames = 0
        self.decode_errors = 0
        self.inference_errors = 0

    def process_if_due(
        self,
        sample: CameraSample | None,
        *,
        monotonic_seconds: float,
        decode: Any,
        infer: Any,
    ) -> bool:
        if monotonic_seconds + 1e-9 < self._next_inference_at:
            return False
        self._next_inference_at = monotonic_seconds + 1 / self._inference_hz
        if sample is None:
            return False
        key = (sample.metadata.source_instance_id, sample.metadata.sequence)
        if key == self._last_processed_key:
            return False
        image = decode(sample.jpeg)
        if image is None:
            self.decode_errors += 1
            self._last_processed_key = key
            return False
        started = time.monotonic()
        boxes = infer(image)
        inference_ms = (time.monotonic() - started) * 1_000
        result = DetectionResult(
            source_instance_id=sample.metadata.source_instance_id,
            sequence=sample.metadata.sequence,
            captured_at_unix_ns=sample.metadata.captured_at_unix_ns,
            image_width=sample.metadata.width,
            image_height=sample.metadata.height,
            inference_ms=round(inference_ms, 2),
            boxes=tuple(boxes),
        )
        self._publisher.publish_detection(result)
        self._last_processed_key = key
        self.processed_frames += 1
        return True


class ZenohDetectorPublisher:
    def __init__(self, settings: Settings) -> None:
        self._settings = settings
        self._session_context = open_zenoh_session(settings.zenoh.connect)
        self._session: Any = None
        self._detection_publisher: Any = None
        self._status_publisher: Any = None
        self._liveliness_token: Any = None

    def __enter__(self) -> ZenohDetectorPublisher:
        self._session = self._session_context.__enter__()
        self._detection_publisher = self._session.declare_publisher(self._settings.detections_key)
        self._status_publisher = self._session.declare_publisher(self._settings.detector_status_key)
        self._liveliness_token = self._session.liveliness().declare_token(
            self._settings.detector_liveliness_key
        )
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        for declaration in (
            self._liveliness_token,
            self._status_publisher,
            self._detection_publisher,
        ):
            undeclare = getattr(declaration, "undeclare", None)
            if callable(undeclare):
                undeclare()
        self._session_context.__exit__(exc_type, exc, traceback)

    def publish_detection(self, result: DetectionResult) -> None:
        self._detection_publisher.put(encode_detection(result), encoding="application/json")

    def publish_status(self, status: dict[str, object]) -> None:
        import json

        self._status_publisher.put(
            json.dumps(status, separators=(",", ":"), sort_keys=True), encoding="application/json"
        )


class FaceDetectorService:
    def __init__(self, settings: Settings, publisher: ZenohDetectorPublisher) -> None:
        self._settings = settings
        self._publisher = publisher
        self._samples = LatestCameraSample()
        self._loop = FaceDetectorLoop(publisher, inference_hz=settings.detector.inference_hz)
        self._stop = threading.Event()
        self._state = "STARTING"
        self._last_error: str | None = None
        self._last_status_at = 0.0
        self._last_processed_count = 0

    def _publish_status(self, now: float, *, force: bool = False) -> None:
        if not force and now - self._last_status_at < 1:
            return
        elapsed = max(now - self._last_status_at, 1)
        self._publisher.publish_status(
            {
                "schema_version": 1,
                "state": self._state,
                "inference_fps": (self._loop.processed_frames - self._last_processed_count)
                / elapsed,
                "processed_frames": self._loop.processed_frames,
                "dropped_frames": self._samples.dropped_frames,
                "decode_errors": self._loop.decode_errors,
                "inference_errors": self._loop.inference_errors,
                "last_error": self._last_error,
            }
        )
        self._last_status_at = now
        self._last_processed_count = self._loop.processed_frames

    def run_forever(self) -> None:
        import cv2
        import numpy as np
        import torch
        from ultralytics import YOLO

        torch.set_num_threads(1)
        subscriber = self._publisher._session.declare_subscriber(
            self._settings.camera_image_key, self._on_camera_sample
        )
        try:
            try:
                model = YOLO(self._settings.detector.model_path)
                self._state = "STREAMING"
                while not self._stop.is_set():
                    now = time.monotonic()
                    try:
                        self._loop.process_if_due(
                            self._samples.snapshot(),
                            monotonic_seconds=now,
                            decode=lambda jpeg: cv2.imdecode(
                                np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR
                            ),
                            infer=lambda image: self._infer(model, image),
                        )
                    except Exception as exc:
                        self._state = "ERROR"
                        self._last_error = str(exc)
                        self._loop.inference_errors += 1
                        LOG.exception("face inference failed")
                    self._publish_status(now)
                    self._stop.wait(0.01)
            except Exception as exc:
                self._state = "ERROR"
                self._last_error = str(exc)
                self._publish_status(time.monotonic(), force=True)
                raise
            finally:
                self._state = "STOPPED"
                self._publish_status(time.monotonic(), force=True)
        finally:
            undeclare = getattr(subscriber, "undeclare", None)
            if callable(undeclare):
                undeclare()

    def _on_camera_sample(self, sample: object) -> None:
        try:
            metadata = decode_frame_metadata(bytes(getattr(sample, "attachment")))
            self._samples.update(bytes(getattr(sample, "payload")), metadata)
        except (ProtocolError, TypeError, ValueError):
            self._loop.decode_errors += 1

    def _infer(self, model: Any, image: Any) -> list[DetectionBox]:
        result = model.predict(
            source=image,
            imgsz=self._settings.detector.image_size,
            conf=self._settings.detector.confidence,
            iou=self._settings.detector.iou,
            device="cpu",
            verbose=False,
        )[0]
        boxes: list[DetectionBox] = []
        for row in result.boxes.data.tolist():
            x1, y1, x2, y2, confidence = row[:5]
            if x2 <= x1 or y2 <= y1:
                continue
            boxes.append(
                DetectionBox(
                    x=max(0.0, float(x1)),
                    y=max(0.0, float(y1)),
                    width=min(float(x2), image.shape[1]) - max(0.0, float(x1)),
                    height=min(float(y2), image.shape[0]) - max(0.0, float(y1)),
                    confidence=float(confidence),
                )
            )
        return boxes


def main() -> None:
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )
    settings = load_settings()
    with ZenohDetectorPublisher(settings) as publisher:
        FaceDetectorService(settings, publisher).run_forever()


if __name__ == "__main__":
    main()
