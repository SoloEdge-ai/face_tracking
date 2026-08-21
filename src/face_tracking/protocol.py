"""Versioned metadata carried alongside Zenoh JPEG samples."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from typing import Any

SCHEMA_VERSION = 1


class ProtocolError(ValueError):
    """A frame metadata attachment does not match the public protocol."""


@dataclass(frozen=True, slots=True)
class FrameMetadata:
    source_instance_id: str
    sequence: int
    captured_at_unix_ns: int
    width: int
    height: int
    capture_format: str
    jpeg_quality: int
    schema_version: int = SCHEMA_VERSION

    def validate(self) -> None:
        if self.schema_version != SCHEMA_VERSION:
            raise ProtocolError(f"unsupported schema_version: {self.schema_version}")
        if not self.source_instance_id:
            raise ProtocolError("source_instance_id is required")
        if self.sequence < 0:
            raise ProtocolError("sequence must be non-negative")
        if self.captured_at_unix_ns <= 0:
            raise ProtocolError("captured_at_unix_ns must be positive")
        if self.width <= 0 or self.height <= 0:
            raise ProtocolError("width and height must be positive")
        if not self.capture_format:
            raise ProtocolError("capture_format is required")
        if not 1 <= self.jpeg_quality <= 100:
            raise ProtocolError("jpeg_quality must be between 1 and 100")


def encode_frame_metadata(metadata: FrameMetadata) -> bytes:
    metadata.validate()
    return json.dumps(asdict(metadata), separators=(",", ":"), sort_keys=True).encode("utf-8")


def decode_frame_metadata(payload: bytes | bytearray | memoryview) -> FrameMetadata:
    try:
        raw: Any = json.loads(bytes(payload).decode("utf-8"))
        if not isinstance(raw, dict):
            raise ProtocolError("metadata must be a JSON object")
        metadata = FrameMetadata(**raw)
        metadata.validate()
        return metadata
    except (TypeError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError("metadata must be valid UTF-8 JSON") from exc
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("metadata fields are invalid") from exc
