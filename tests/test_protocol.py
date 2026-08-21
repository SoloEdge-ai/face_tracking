import json

import pytest

from face_tracking.protocol import (
    FrameMetadata,
    ProtocolError,
    decode_frame_metadata,
    encode_frame_metadata,
)


def test_frame_metadata_round_trips_as_utf8_json() -> None:
    metadata = FrameMetadata(
        source_instance_id="camera-instance",
        sequence=42,
        captured_at_unix_ns=1_700_000_000_000_000_000,
        width=1280,
        height=720,
        capture_format="MJPG",
        jpeg_quality=80,
    )

    encoded = encode_frame_metadata(metadata)

    assert json.loads(encoded.decode("utf-8"))["schema_version"] == 1
    assert decode_frame_metadata(encoded) == metadata


@pytest.mark.parametrize(
    "payload",
    [
        b"not-json",
        b"{}",
        b'{"schema_version": 2}',
        b'{"schema_version": 1, "sequence": -1}',
    ],
)
def test_invalid_metadata_is_rejected(payload: bytes) -> None:
    with pytest.raises(ProtocolError):
        decode_frame_metadata(payload)
