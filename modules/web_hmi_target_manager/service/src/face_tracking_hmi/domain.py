from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum


class HmiCameraState(StrEnum):
    STARTING = "STARTING"
    STREAMING = "STREAMING"
    STALE = "STALE"
    OFFLINE = "OFFLINE"


@dataclass(frozen=True, slots=True)
class FrameMetadata:
    source_instance_id: str
    sequence: int
    captured_at_unix_ns: int
    width: int
    height: int
    capture_format: str
    jpeg_quality: int
    schema_version: int = 2


@dataclass(frozen=True, slots=True)
class DetectionBox:
    x: float
    y: float
    width: float
    height: float
    confidence: float


@dataclass(frozen=True, slots=True)
class DetectionResult:
    source_instance_id: str
    sequence: int
    captured_at_unix_ns: int
    image_width: int
    image_height: int
    inference_ms: float
    boxes: tuple[DetectionBox, ...]
    schema_version: int = 2


@dataclass(frozen=True, slots=True)
class Frame:
    jpeg: bytes
    metadata: FrameMetadata
    received_at_unix_ns: int
