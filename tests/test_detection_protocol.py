import pytest

from face_tracking.detection_protocol import (
    DetectionBox,
    DetectionResult,
    decode_detection,
    encode_detection,
)
from face_tracking.protocol import ProtocolError


def result() -> DetectionResult:
    return DetectionResult(
        source_instance_id="camera-a",
        sequence=4,
        captured_at_unix_ns=1,
        image_width=1280,
        image_height=720,
        inference_ms=24.5,
        boxes=(DetectionBox(x=10, y=20, width=100, height=120, confidence=0.92),),
    )


def test_detection_round_trips_through_zenoh_payload() -> None:
    assert decode_detection(encode_detection(result())) == result()


@pytest.mark.parametrize(
    "box",
    [
        DetectionBox(x=0, y=0, width=0, height=1, confidence=0.5),
        DetectionBox(x=0, y=0, width=1, height=1, confidence=1.1),
        DetectionBox(x=1270, y=0, width=20, height=1, confidence=0.5),
    ],
)
def test_detection_rejects_invalid_boxes(box: DetectionBox) -> None:
    invalid = DetectionResult(
        source_instance_id="camera-a",
        sequence=1,
        captured_at_unix_ns=1,
        image_width=1280,
        image_height=720,
        inference_ms=1,
        boxes=(box,),
    )
    with pytest.raises(ProtocolError):
        encode_detection(invalid)
