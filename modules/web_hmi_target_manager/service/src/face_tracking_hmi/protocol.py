from __future__ import annotations

from .domain import (
    DetectionBox,
    DetectionResult,
    FrameMetadata,
    SelectedTargetObservation,
    TrackingState,
)
from .generated import face_tracking_v2_pb2 as wire


class ProtocolError(ValueError):
    pass


def decode_frame_metadata(payload: bytes) -> FrameMetadata:
    try:
        message = wire.FrameMetadata.FromString(payload)
        value = FrameMetadata(message.source_instance_id, message.sequence, message.captured_at_unix_ns, message.width, message.height, message.capture_format, message.jpeg_quality, message.schema_version)
        if value.schema_version != 2 or not value.source_instance_id or value.captured_at_unix_ns <= 0:
            raise ProtocolError("invalid frame metadata")
        return value
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid frame metadata protobuf") from exc


def decode_detection(payload: bytes) -> DetectionResult:
    try:
        message = wire.DetectionResult.FromString(payload)
        value = DetectionResult(
            message.source_instance_id, message.tracker_instance_id, message.sequence, message.captured_at_unix_ns,
            message.image_width, message.image_height, message.inference_ms,
            tuple(DetectionBox(box.x, box.y, box.width, box.height, box.confidence, box.track_id) for box in message.boxes),
            message.schema_version,
        )
        if value.schema_version != 2 or not value.source_instance_id:
            raise ProtocolError("invalid detection result")
        return value
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid detection protobuf") from exc


def decode_camera_status(payload: bytes) -> dict[str, object]:
    try:
        message = wire.CameraStatus.FromString(payload)
        if message.schema_version != 2:
            raise ProtocolError("unsupported camera status version")
        states = {wire.CAMERA_STATE_STARTING: "STARTING", wire.CAMERA_STATE_STREAMING: "STREAMING", wire.CAMERA_STATE_RECONNECTING: "RECONNECTING", wire.CAMERA_STATE_ERROR: "ERROR", wire.CAMERA_STATE_STOPPED: "STOPPED"}
        return {
            "schema_version": message.schema_version, "state": states.get(message.state, "STARTING"),
            "capture_fps": message.capture_fps, "publish_fps": message.publish_fps,
            "captured_frames": message.captured_frames, "published_frames": message.published_frames,
            "dropped_frames": message.dropped_frames, "device_path": message.device_path,
            "last_error": message.last_error if message.HasField("last_error") else None,
        }
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid camera status protobuf") from exc


def decode_detector_status(payload: bytes) -> dict[str, object]:
    try:
        message = wire.DetectorStatus.FromString(payload)
        if message.schema_version != 2:
            raise ProtocolError("unsupported detector status version")
        states = {
            wire.DETECTOR_STATE_STARTING: "STARTING",
            wire.DETECTOR_STATE_STREAMING: "STREAMING",
            wire.DETECTOR_STATE_ERROR: "ERROR",
            wire.DETECTOR_STATE_STOPPED: "STOPPED",
        }
        return {
            "schema_version": message.schema_version,
            "state": states.get(message.state, "STARTING"),
            "inference_fps": message.inference_fps,
            "processed_frames": message.processed_frames,
            "dropped_frames": message.dropped_frames,
            "decode_errors": message.decode_errors,
            "inference_errors": message.inference_errors,
            "last_error": message.last_error if message.HasField("last_error") else None,
        }
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid detector status protobuf") from exc


def detection_as_dict(result: DetectionResult) -> dict[str, object]:
    return {
        "schema_version": result.schema_version, "source_instance_id": result.source_instance_id,
        "tracker_instance_id": result.tracker_instance_id,
        "sequence": result.sequence, "captured_at_unix_ns": result.captured_at_unix_ns,
        "image_width": result.image_width, "image_height": result.image_height,
        "inference_ms": result.inference_ms,
        "boxes": [{"x": box.x, "y": box.y, "width": box.width, "height": box.height, "confidence": box.confidence, "track_id": box.track_id} for box in result.boxes],
    }


def encode_selected_target(value: SelectedTargetObservation) -> bytes:
    states = {
        TrackingState.NO_TARGET: wire.TRACKING_STATE_NO_TARGET,
        TrackingState.TRACKING: wire.TRACKING_STATE_TRACKING,
        TrackingState.MISSING: wire.TRACKING_STATE_MISSING,
        TrackingState.LOST: wire.TRACKING_STATE_LOST,
    }
    return wire.SelectedTargetObservation(
        schema_version=value.schema_version,
        source_instance_id=value.source_instance_id,
        tracker_instance_id=value.tracker_instance_id,
        sequence=value.sequence,
        captured_at_unix_ns=value.captured_at_unix_ns,
        selected_track_id=value.selected_track_id,
        target_center_x=value.target_center_x,
        target_center_y=value.target_center_y,
        image_width=value.image_width,
        image_height=value.image_height,
        tracking_state=states[value.tracking_state],
    ).SerializeToString()


def decode_pan_tilt_delta(payload: bytes) -> dict[str, object]:
    try:
        message = wire.PanTiltDelta.FromString(payload)
        if message.schema_version != 2:
            raise ProtocolError("unsupported controller command version")
        reasons = {
            wire.CONTROLLER_DECISION_APPLIED: "APPLIED",
            wire.CONTROLLER_DECISION_DEADBAND: "DEADBAND",
            wire.CONTROLLER_DECISION_NO_TARGET: "NO_TARGET",
            wire.CONTROLLER_DECISION_LOST: "LOST",
            wire.CONTROLLER_DECISION_STALE: "STALE",
            wire.CONTROLLER_DECISION_DUPLICATE: "DUPLICATE",
            wire.CONTROLLER_DECISION_OUT_OF_ORDER: "OUT_OF_ORDER",
            wire.CONTROLLER_DECISION_MISSING_HOLD: "MISSING_HOLD",
        }
        return {
            "schema_version": message.schema_version,
            "source_instance_id": message.source_instance_id,
            "tracker_instance_id": message.tracker_instance_id,
            "sequence": message.sequence,
            "captured_at_unix_ns": message.captured_at_unix_ns,
            "computed_at_unix_ns": message.computed_at_unix_ns,
            "selected_track_id": message.selected_track_id,
            "delta_pan_deg": message.delta_pan_deg,
            "delta_tilt_deg": message.delta_tilt_deg,
            "reason": reasons.get(message.reason, "UNSPECIFIED"),
        }
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid controller command protobuf") from exc


def decode_controller_status(payload: bytes) -> dict[str, object]:
    try:
        message = wire.PixelCenterControllerStatus.FromString(payload)
        if message.schema_version != 2:
            raise ProtocolError("unsupported controller status version")
        states = {
            wire.PIXEL_CENTER_CONTROLLER_STATE_STARTING: "STARTING",
            wire.PIXEL_CENTER_CONTROLLER_STATE_ACTIVE: "ACTIVE",
            wire.PIXEL_CENTER_CONTROLLER_STATE_HOLDING: "HOLDING",
            wire.PIXEL_CENTER_CONTROLLER_STATE_ERROR: "ERROR",
            wire.PIXEL_CENTER_CONTROLLER_STATE_STOPPED: "STOPPED",
        }
        return {
            "schema_version": message.schema_version,
            "state": states.get(message.state, "STARTING"),
            "observation_age_ms": message.observation_age_ms,
            "error_x_px": message.error_x_px,
            "error_y_px": message.error_y_px,
            "last_delta_pan_deg": message.last_delta_pan_deg,
            "last_delta_tilt_deg": message.last_delta_tilt_deg,
            "processed_observations": message.processed_observations,
            "stale_observations": message.stale_observations,
            "duplicate_observations": message.duplicate_observations,
            "out_of_order_observations": message.out_of_order_observations,
            "last_rejection_reason": message.last_rejection_reason
            if message.HasField("last_rejection_reason")
            else None,
        }
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid controller status protobuf") from exc


def decode_servo_commanded_state(payload: bytes) -> dict[str, object]:
    try:
        message = wire.PanTiltCommandedState.FromString(payload)
        if message.schema_version != 2 or message.updated_at_unix_ns <= 0:
            raise ProtocolError("unsupported or invalid servo state")
        states = {
            wire.SERVO_DRIVER_STATE_STARTING: "STARTING",
            wire.SERVO_DRIVER_STATE_HOMING: "HOMING",
            wire.SERVO_DRIVER_STATE_HOLDING: "HOLDING",
            wire.SERVO_DRIVER_STATE_TRACKING: "TRACKING",
            wire.SERVO_DRIVER_STATE_ERROR: "ERROR",
            wire.SERVO_DRIVER_STATE_STOPPED: "STOPPED",
        }
        decisions = {
            wire.SERVO_DECISION_HOME_STARTUP: "HOME_STARTUP",
            wire.SERVO_DECISION_APPLIED: "APPLIED",
            wire.SERVO_DECISION_HELD_DEADBAND: "HELD_DEADBAND",
            wire.SERVO_DECISION_HELD_MISSING: "HELD_MISSING",
            wire.SERVO_DECISION_HELD_LIMIT: "HELD_LIMIT",
            wire.SERVO_DECISION_HOME_LOST: "HOME_LOST",
            wire.SERVO_DECISION_HOME_NO_TARGET: "HOME_NO_TARGET",
            wire.SERVO_DECISION_HOME_STALE: "HOME_STALE",
            wire.SERVO_DECISION_HOME_UPSTREAM_TIMEOUT: "HOME_UPSTREAM_TIMEOUT",
            wire.SERVO_DECISION_REJECTED_DUPLICATE: "REJECTED_DUPLICATE",
            wire.SERVO_DECISION_REJECTED_OUT_OF_ORDER: "REJECTED_OUT_OF_ORDER",
            wire.SERVO_DECISION_REJECTED_INVALID: "REJECTED_INVALID",
            wire.SERVO_DECISION_ERROR: "ERROR",
        }
        return {
            "schema_version": message.schema_version,
            "updated_at_unix_ns": message.updated_at_unix_ns,
            "commanded_pan_deg": message.commanded_pan_deg,
            "commanded_tilt_deg": message.commanded_tilt_deg,
            "last_track_id": message.last_track_id,
            "state": states.get(message.state, "STARTING"),
            "decision": decisions.get(message.decision, "UNSPECIFIED"),
            "pan_limit_held": message.pan_limit_held,
            "tilt_limit_held": message.tilt_limit_held,
            "pwm_active": message.pwm_active,
            "last_error": message.last_error if message.HasField("last_error") else None,
            "applied_commands": message.applied_commands,
            "rejected_commands": message.rejected_commands,
        }
    except ProtocolError:
        raise
    except Exception as exc:
        raise ProtocolError("invalid servo state protobuf") from exc
