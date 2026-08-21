"""USB camera capture and low-latency latest-frame Zenoh publication."""

from __future__ import annotations

import logging
import threading
import time
import uuid
from dataclasses import dataclass
from enum import StrEnum
from typing import Any, Protocol

from .protocol import FrameMetadata
from .settings import Settings, load_settings

LOG = logging.getLogger(__name__)


class CameraState(StrEnum):
    STARTING = "STARTING"
    STREAMING = "STREAMING"
    RECONNECTING = "RECONNECTING"
    ERROR = "ERROR"
    STOPPED = "STOPPED"


class ImagePublisher(Protocol):
    def publish_image(self, jpeg: bytes, metadata: FrameMetadata) -> None: ...

    def publish_status(self, status: dict[str, Any]) -> None: ...


@dataclass(frozen=True, slots=True)
class CapturedFrame:
    bgr: Any
    captured_at_unix_ns: int


class LatestCapturedFrame:
    """A single slot ensures capture never queues old camera frames."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._frame: CapturedFrame | None = None
        self.captured_frames = 0

    def update(self, bgr: Any, *, captured_at_unix_ns: int) -> None:
        with self._lock:
            self._frame = CapturedFrame(bgr=bgr, captured_at_unix_ns=captured_at_unix_ns)
            self.captured_frames += 1

    def snapshot(self) -> CapturedFrame | None:
        with self._lock:
            return self._frame


@dataclass(slots=True)
class CameraPublisherLoop:
    publisher: ImagePublisher
    source_instance_id: str
    width: int
    height: int
    capture_format: str
    jpeg_quality: int
    publish_hz: int
    _latest_jpeg: bytes | None = None
    _latest_capture_ns: int = 0
    _last_published_capture_ns: int = 0
    _next_publish_at: float = 0.0
    _sequence: int = 0
    published_frames: int = 0

    def accept_latest_jpeg(self, jpeg: bytes, *, captured_at_unix_ns: int) -> None:
        self._latest_jpeg = bytes(jpeg)
        self._latest_capture_ns = captured_at_unix_ns

    def is_due(self, monotonic_seconds: float) -> bool:
        return monotonic_seconds + 1e-9 >= self._next_publish_at

    def publish_if_due(self, *, monotonic_seconds: float) -> bool:
        if not self.is_due(monotonic_seconds):
            return False
        self._next_publish_at = monotonic_seconds + 1 / self.publish_hz
        if self._latest_jpeg is None or self._latest_capture_ns == self._last_published_capture_ns:
            return False
        metadata = FrameMetadata(
            source_instance_id=self.source_instance_id,
            sequence=self._sequence,
            captured_at_unix_ns=self._latest_capture_ns,
            width=self.width,
            height=self.height,
            capture_format=self.capture_format,
            jpeg_quality=self.jpeg_quality,
        )
        self.publisher.publish_image(self._latest_jpeg, metadata)
        self._last_published_capture_ns = self._latest_capture_ns
        self._sequence += 1
        self.published_frames += 1
        return True


class OpenCvCameraDriver:
    def __init__(self, settings: Settings, publisher: ImagePublisher) -> None:
        self._settings = settings
        self._publisher = publisher
        self._loop = CameraPublisherLoop(
            publisher=publisher,
            source_instance_id=str(uuid.uuid4()),
            width=settings.camera.width,
            height=settings.camera.height,
            capture_format="MJPG",
            jpeg_quality=settings.camera.jpeg_quality,
            publish_hz=settings.camera.publish_hz,
        )
        self._buffer = LatestCapturedFrame()
        self._state = CameraState.STARTING
        self._last_error: str | None = None
        self._state_lock = threading.Lock()
        self._stop = threading.Event()
        self._capture_thread: threading.Thread | None = None
        self._last_status_at = 0.0
        self._last_capture_count = 0
        self._last_publish_count = 0

    def _set_state(self, state: CameraState, error: str | None = None) -> None:
        with self._state_lock:
            self._state = state
            self._last_error = error

    def _state_snapshot(self) -> tuple[CameraState, str | None]:
        with self._state_lock:
            return self._state, self._last_error

    def _publish_status(self, monotonic_seconds: float, *, force: bool = False) -> None:
        if not force and monotonic_seconds - self._last_status_at < 1.0:
            return
        elapsed = max(monotonic_seconds - self._last_status_at, 1.0)
        state, last_error = self._state_snapshot()
        captured_frames = self._buffer.captured_frames
        published_frames = self._loop.published_frames
        self._publisher.publish_status(
            {
                "schema_version": 1,
                "state": state,
                "capture_fps": (captured_frames - self._last_capture_count) / elapsed,
                "publish_fps": (published_frames - self._last_publish_count) / elapsed,
                "captured_frames": captured_frames,
                "published_frames": published_frames,
                "dropped_frames": max(0, captured_frames - published_frames),
                "device_path": self._settings.camera.device_path,
                "last_error": last_error,
            }
        )
        self._last_status_at = monotonic_seconds
        self._last_capture_count = captured_frames
        self._last_publish_count = published_frames

    def _open_camera(self, cv2: Any) -> Any | None:
        camera = cv2.VideoCapture(self._settings.camera.device_path, cv2.CAP_V4L2)
        camera.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, self._settings.camera.width)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, self._settings.camera.height)
        camera.set(cv2.CAP_PROP_FPS, self._settings.camera.capture_fps)
        if camera.isOpened():
            LOG.info("camera connected: %s", self._settings.camera.device_path)
            return camera
        camera.release()
        return None

    def _capture_forever(self) -> None:
        import cv2

        camera = None
        try:
            while not self._stop.is_set():
                if camera is None:
                    camera = self._open_camera(cv2)
                    if camera is None:
                        message = f"cannot open camera {self._settings.camera.device_path}"
                        LOG.error(message)
                        self._set_state(CameraState.RECONNECTING, message)
                        self._stop.wait(self._settings.camera.reconnect_seconds)
                        continue
                    self._set_state(CameraState.STREAMING)
                try:
                    if not camera.grab():
                        raise RuntimeError("camera grab failed")
                    ok, bgr = camera.retrieve()
                    if not ok:
                        raise RuntimeError("camera retrieve failed")
                    self._buffer.update(bgr, captured_at_unix_ns=time.time_ns())
                except Exception as exc:
                    message = str(exc)
                    LOG.warning("%s; reconnecting", message)
                    self._set_state(CameraState.ERROR, message)
                    camera.release()
                    camera = None
                    self._stop.wait(self._settings.camera.reconnect_seconds)
                    if not self._stop.is_set():
                        self._set_state(CameraState.RECONNECTING, message)
        finally:
            if camera is not None:
                camera.release()

    def _publish_latest_frame(self, cv2: Any, monotonic_seconds: float) -> None:
        if not self._loop.is_due(monotonic_seconds):
            return
        latest = self._buffer.snapshot()
        if latest is None:
            self._loop.publish_if_due(monotonic_seconds=monotonic_seconds)
            return
        encoded, jpeg = cv2.imencode(
            ".jpg",
            latest.bgr,
            [int(cv2.IMWRITE_JPEG_QUALITY), self._settings.camera.jpeg_quality],
        )
        if not encoded:
            self._set_state(CameraState.ERROR, "JPEG encoding failed")
            LOG.warning("JPEG encoding failed")
            return
        self._loop.accept_latest_jpeg(
            jpeg.tobytes(), captured_at_unix_ns=latest.captured_at_unix_ns
        )
        self._loop.publish_if_due(monotonic_seconds=monotonic_seconds)

    def run_forever(self) -> None:
        import cv2

        self._capture_thread = threading.Thread(
            target=self._capture_forever,
            name="uvc-camera-capture",
            daemon=True,
        )
        self._capture_thread.start()
        try:
            while True:
                now = time.monotonic()
                self._publish_latest_frame(cv2, now)
                self._publish_status(now)
                time.sleep(0.005)
        except KeyboardInterrupt:
            LOG.info("camera driver interrupted")
        finally:
            self._stop.set()
            self._capture_thread.join(timeout=self._settings.camera.reconnect_seconds + 1)
            self._set_state(CameraState.STOPPED)
            self._publish_status(time.monotonic(), force=True)


def main() -> None:
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )
    settings = load_settings()
    from .zenoh_transport import ZenohCameraPublisher

    with ZenohCameraPublisher(settings) as publisher:
        OpenCvCameraDriver(settings, publisher).run_forever()


if __name__ == "__main__":
    main()
