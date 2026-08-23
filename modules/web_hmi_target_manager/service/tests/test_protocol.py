from pathlib import Path

from face_tracking_hmi.generated import face_tracking_v2_pb2 as wire
from face_tracking_hmi.protocol import (
    decode_detection,
    decode_detector_status,
    decode_frame_metadata,
)


def test_frame_metadata_protobuf_decodes() -> None:
    payload = wire.FrameMetadata(schema_version=2, source_instance_id="camera", sequence=7, captured_at_unix_ns=10, width=1280, height=720, capture_format="MJPG", jpeg_quality=80).SerializeToString()
    assert decode_frame_metadata(payload).sequence == 7


def test_detection_protobuf_decodes() -> None:
    message = wire.DetectionResult(schema_version=2, source_instance_id="camera", sequence=8, captured_at_unix_ns=10, image_width=1280, image_height=720, inference_ms=2.5)
    message.boxes.add(x=1, y=2, width=3, height=4, confidence=0.75)
    decoded = decode_detection(message.SerializeToString())
    assert decoded.sequence == 8
    assert decoded.boxes[0].confidence == 0.75


def test_cpp_golden_fixtures_decode() -> None:
    fixtures = Path(__file__).parents[4] / "tests" / "integration" / "fixtures"
    frame = decode_frame_metadata(bytes.fromhex((fixtures / "frame_metadata_v2.hex").read_text().strip()))
    detection = decode_detection(bytes.fromhex((fixtures / "detection_result_v2.hex").read_text().strip()))
    assert frame.source_instance_id == "camera-a"
    assert detection.boxes[0].confidence == 0.75


def test_detector_status_protobuf_decodes() -> None:
    payload = wire.DetectorStatus(
        schema_version=2,
        state=wire.DETECTOR_STATE_STREAMING,
        inference_fps=5.1,
        processed_frames=12,
    ).SerializeToString()
    assert decode_detector_status(payload)["state"] == "STREAMING"
