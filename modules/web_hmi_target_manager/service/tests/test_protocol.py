from pathlib import Path

from face_tracking_hmi.generated import face_tracking_v2_pb2 as wire
from face_tracking_hmi.protocol import (
    decode_camera_status,
    decode_controller_status,
    decode_detection,
    decode_detector_status,
    decode_frame_metadata,
    decode_pan_tilt_delta,
    decode_servo_commanded_state,
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
    frame_bytes = bytes.fromhex((fixtures / "frame_metadata_v2.hex").read_text().strip())
    detection_bytes = bytes.fromhex((fixtures / "detection_result_v2.hex").read_text().strip())
    camera_bytes = bytes.fromhex((fixtures / "camera_status_v2.hex").read_text().strip())
    detector_bytes = bytes.fromhex((fixtures / "detector_status_v2.hex").read_text().strip())
    frame = decode_frame_metadata(frame_bytes)
    detection = decode_detection(detection_bytes)
    camera = decode_camera_status(camera_bytes)
    detector = decode_detector_status(detector_bytes)
    assert frame.source_instance_id == "camera-a"
    assert frame.sequence == 7
    assert frame.captured_at_unix_ns == 123456
    assert frame.width == 1280 and frame.height == 720
    assert frame.capture_format == "MJPG" and frame.jpeg_quality == 80
    assert detection.boxes[0].confidence == 0.75
    assert detection.inference_ms == 12.5
    assert camera == {
        "schema_version": 2,
        "state": "STREAMING",
        "capture_fps": 29.5,
        "publish_fps": 9.75,
        "captured_frames": 100,
        "published_frames": 33,
        "dropped_frames": 67,
        "device_path": "/dev/video0",
        "last_error": None,
    }
    assert detector["state"] == "ERROR"
    assert detector["inference_fps"] == 4.25
    assert detector["last_error"] == "boom"

    frame_wire = wire.FrameMetadata(
        schema_version=2,
        source_instance_id="camera-a",
        sequence=7,
        captured_at_unix_ns=123456,
        width=1280,
        height=720,
        capture_format="MJPG",
        jpeg_quality=80,
    )
    detection_wire = wire.DetectionResult(
        schema_version=2,
        source_instance_id="camera-a",
        sequence=7,
        captured_at_unix_ns=123456,
        image_width=1280,
        image_height=720,
        inference_ms=12.5,
    )
    detection_wire.boxes.add(x=1, y=2, width=3, height=4, confidence=0.75)
    camera_wire = wire.CameraStatus(
        schema_version=2,
        state=wire.CAMERA_STATE_STREAMING,
        capture_fps=29.5,
        publish_fps=9.75,
        captured_frames=100,
        published_frames=33,
        dropped_frames=67,
        device_path="/dev/video0",
    )
    detector_wire = wire.DetectorStatus(
        schema_version=2,
        state=wire.DETECTOR_STATE_ERROR,
        inference_fps=4.25,
        processed_frames=20,
        dropped_frames=10,
        decode_errors=2,
        inference_errors=1,
        last_error="boom",
    )
    assert frame_wire.SerializeToString() == frame_bytes
    assert detection_wire.SerializeToString() == detection_bytes
    assert camera_wire.SerializeToString() == camera_bytes
    assert detector_wire.SerializeToString() == detector_bytes


def test_detector_status_protobuf_decodes() -> None:
    payload = wire.DetectorStatus(
        schema_version=2,
        state=wire.DETECTOR_STATE_STREAMING,
        inference_fps=5.1,
        processed_frames=12,
    ).SerializeToString()
    assert decode_detector_status(payload)["state"] == "STREAMING"


def test_controller_output_and_status_protobuf_decode() -> None:
    delta = wire.PanTiltDelta(
        schema_version=2,
        source_instance_id="camera",
        tracker_instance_id="tracker",
        sequence=8,
        captured_at_unix_ns=10,
        computed_at_unix_ns=20,
        selected_track_id=7,
        delta_pan_deg=1.0,
        delta_tilt_deg=-0.5,
        reason=wire.CONTROLLER_DECISION_APPLIED,
    )
    assert decode_pan_tilt_delta(delta.SerializeToString())["reason"] == "APPLIED"
    delta.reason = wire.CONTROLLER_DECISION_DUPLICATE
    assert decode_pan_tilt_delta(delta.SerializeToString())["reason"] == "DUPLICATE"
    delta.reason = wire.CONTROLLER_DECISION_OUT_OF_ORDER
    assert decode_pan_tilt_delta(delta.SerializeToString())["reason"] == "OUT_OF_ORDER"
    status = wire.PixelCenterControllerStatus(
        schema_version=2,
        state=wire.PIXEL_CENTER_CONTROLLER_STATE_ACTIVE,
        observation_age_ms=100,
        error_x_px=100,
        last_delta_pan_deg=1.0,
        processed_observations=3,
    )
    decoded = decode_controller_status(status.SerializeToString())
    assert decoded["state"] == "ACTIVE"
    assert decoded["error_x_px"] == 100


def test_servo_commanded_state_protobuf_decodes() -> None:
    payload = wire.PanTiltCommandedState(
        schema_version=2,
        updated_at_unix_ns=1_000_000_000,
        commanded_pan_deg=135,
        commanded_tilt_deg=10,
        last_track_id=7,
        state=wire.SERVO_DRIVER_STATE_HOLDING,
        decision=wire.SERVO_DECISION_HOME_LOST,
        pwm_active=True,
    ).SerializeToString()
    decoded = decode_servo_commanded_state(payload)
    assert decoded["commanded_pan_deg"] == 135
    assert decoded["commanded_tilt_deg"] == 10
    assert decoded["state"] == "HOLDING"
    assert decoded["decision"] == "HOME_LOST"
