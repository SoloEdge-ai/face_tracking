"""Versioned Zenoh payloads for face detections."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from typing import Any

from .protocol import ProtocolError

DETECTION_SCHEMA_VERSION = 1


@dataclass(frozen=True, slots=True)
class DetectionBox:
    x: float
    y: float
    width: float
    height: float
    confidence: float

    def validate(self, image_width: int, image_height: int) -> None:
        if self.width <= 0 or self.height <= 0:
            raise ProtocolError("detection box dimensions must be positive")
        if not 0 <= self.confidence <= 1:
            raise ProtocolError("detection confidence must be between 0 and 1")
        if self.x < 0 or self.y < 0:
            raise ProtocolError("detection box coordinates must be non-negative")
        if self.x + self.width > image_width or self.y + self.height > image_height:
            raise ProtocolError("detection box must fit inside source image")


@dataclass(frozen=True, slots=True)
class DetectionResult:
    source_instance_id: str
    sequence: int
    captured_at_unix_ns: int
    image_width: int
    image_height: int
    inference_ms: float
    boxes: tuple[DetectionBox, ...]
    schema_version: int = DETECTION_SCHEMA_VERSION

    def validate(self) -> None:
        if self.schema_version != DETECTION_SCHEMA_VERSION:
            raise ProtocolError(f"unsupported detection schema_version: {self.schema_version}")
        if not self.source_instance_id or self.sequence < 0 or self.captured_at_unix_ns <= 0:
            raise ProtocolError("detection source identity, sequence and timestamp are required")
        if self.image_width <= 0 or self.image_height <= 0:
            raise ProtocolError("detection image dimensions must be positive")
        if self.inference_ms < 0:
            raise ProtocolError("inference_ms must be non-negative")
        for box in self.boxes:
            box.validate(self.image_width, self.image_height)


def encode_detection(result: DetectionResult) -> bytes:
    result.validate()
    return json.dumps(asdict(result), separators=(",", ":"), sort_keys=True).encode("utf-8")


def decode_detection(payload: bytes | bytearray | memoryview) -> DetectionResult:
    try:
        raw: Any = json.loads(bytes(payload).decode("utf-8"))
        if not isinstance(raw, dict):
            raise ProtocolError("detection must be a JSON object")
        raw["boxes"] = tuple(DetectionBox(**box) for box in raw["boxes"])
        result = DetectionResult(**raw)
        result.validate()
        return result
    except (KeyError, TypeError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError("detection payload is invalid") from exc
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("detection fields are invalid") from exc
