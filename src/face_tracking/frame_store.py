"""Thread-safe latest-frame cache used by the HMI API."""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass
from enum import StrEnum

from .detection_protocol import DetectionResult
from .protocol import FrameMetadata


class HmiCameraState(StrEnum):
    STARTING = "STARTING"
    STREAMING = "STREAMING"
    STALE = "STALE"
    OFFLINE = "OFFLINE"


@dataclass(frozen=True, slots=True)
class Frame:
    jpeg: bytes
    metadata: FrameMetadata
    received_at_unix_ns: int


@dataclass(frozen=True, slots=True)
class StoredDetection:
    result: DetectionResult
    received_at_unix_ns: int


class LatestFrameStore:
    def __init__(self, *, stale_after_ms: int = 500, offline_after_ms: int = 3000) -> None:
        self._condition = threading.Condition()
        self._frame: Frame | None = None
        self._last_key: tuple[str, int] | None = None
        self._invalid_frames = 0
        self._arrival_times: deque[int] = deque()
        self._driver_status: dict[str, object] = {}
        self._detection: StoredDetection | None = None
        self._stale_after_ms = stale_after_ms
        self._offline_after_ms = offline_after_ms

    def update(
        self, jpeg: bytes, metadata: FrameMetadata, *, received_at_unix_ns: int | None = None
    ) -> bool:
        if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
            self._invalid_frames += 1
            return False
        key = (metadata.source_instance_id, metadata.sequence)
        with self._condition:
            if key == self._last_key:
                return False
            self._frame = Frame(
                jpeg=bytes(jpeg),
                metadata=metadata,
                received_at_unix_ns=received_at_unix_ns or time.time_ns(),
            )
            self._last_key = key
            self._arrival_times.append(self._frame.received_at_unix_ns)
            cutoff = self._frame.received_at_unix_ns - 1_000_000_000
            while self._arrival_times and self._arrival_times[0] < cutoff:
                self._arrival_times.popleft()
            self._condition.notify_all()
        return True

    def update_driver_status(self, status: dict[str, object]) -> None:
        with self._condition:
            self._driver_status = dict(status)
            self._condition.notify_all()

    def update_detection(
        self, result: DetectionResult, *, received_at_unix_ns: int | None = None
    ) -> None:
        with self._condition:
            self._detection = StoredDetection(result, received_at_unix_ns or time.time_ns())
            self._condition.notify_all()

    def detection(self, *, now_unix_ns: int | None = None) -> DetectionResult | None:
        now = now_unix_ns or time.time_ns()
        with self._condition:
            if self._detection is None or now - self._detection.received_at_unix_ns > 1_000_000_000:
                return None
            return self._detection.result

    def wait_for_detection_after(
        self, last_key: tuple[str, int] | None, timeout_seconds: float
    ) -> DetectionResult | None:
        deadline = time.monotonic() + timeout_seconds
        with self._condition:
            while True:
                detection = self._detection
                if detection is not None:
                    key = (detection.result.source_instance_id, detection.result.sequence)
                    if key != last_key:
                        return detection.result
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._condition.wait(remaining)

    def reject_invalid_frame(self) -> None:
        with self._condition:
            self._invalid_frames += 1

    def snapshot(self) -> Frame | None:
        with self._condition:
            return self._frame

    def wait_for_after(
        self, last_key: tuple[str, int] | None, timeout_seconds: float
    ) -> Frame | None:
        deadline = time.monotonic() + timeout_seconds
        with self._condition:
            while (
                self._frame is None
                or (self._frame.metadata.source_instance_id, self._frame.metadata.sequence)
                == last_key
            ):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._condition.wait(remaining)
            return self._frame

    def status(self, *, now_unix_ns: int | None = None) -> dict[str, object]:
        now = now_unix_ns or time.time_ns()
        with self._condition:
            frame = self._frame
            invalid_frames = self._invalid_frames
            cutoff = now - 1_000_000_000
            while self._arrival_times and self._arrival_times[0] < cutoff:
                self._arrival_times.popleft()
            hmi_fps = len(self._arrival_times)
            driver_status = dict(self._driver_status)
        if frame is None:
            return {
                "state": HmiCameraState.STARTING,
                "invalid_frames": invalid_frames,
                "hmi_fps": hmi_fps,
                "driver": driver_status,
            }
        age_ms = max(0, (now - frame.metadata.captured_at_unix_ns) / 1_000_000)
        state = HmiCameraState.STREAMING
        if age_ms > self._offline_after_ms:
            state = HmiCameraState.OFFLINE
        elif age_ms > self._stale_after_ms:
            state = HmiCameraState.STALE
        return {
            "state": state,
            "frame_age_ms": round(age_ms, 1),
            "sequence": frame.metadata.sequence,
            "source_instance_id": frame.metadata.source_instance_id,
            "captured_at_unix_ns": frame.metadata.captured_at_unix_ns,
            "width": frame.metadata.width,
            "height": frame.metadata.height,
            "invalid_frames": invalid_frames,
            "hmi_fps": hmi_fps,
            "driver": driver_status,
        }
