from __future__ import annotations

import os
import threading
import time

import pytest
from face_tracking_hmi.generated import face_tracking_v2_pb2 as wire

pytestmark = pytest.mark.skipif(
    os.environ.get("FACE_TRACKING_E2E") != "1",
    reason="set FACE_TRACKING_E2E=1 while ./run.sh is active",
)


def test_cpp_publishers_are_python_decodable() -> None:
    import zenoh

    device_id = os.environ.get("FACE_TRACKING_DEVICE_ID", "raspberrypi")
    prefix = f"face_tracking/{device_id}"
    received: dict[str, list[object]] = {
        "frames": [],
        "camera": [],
        "detections": [],
        "detector": [],
        "controller": [],
        "camera_liveliness": [],
        "detector_liveliness": [],
        "controller_liveliness": [],
    }
    condition = threading.Condition()

    def record(name: str, decoder: type[object], *, attachment: bool = False):
        def callback(sample: object) -> None:
            source = getattr(sample, "attachment") if attachment else getattr(sample, "payload")
            message = decoder.FromString(bytes(source))  # type: ignore[attr-defined]
            with condition:
                received[name].append(message)
                condition.notify_all()

        return callback

    def record_liveliness(name: str):
        def callback(sample: object) -> None:
            with condition:
                received[name].append(sample)
                condition.notify_all()

        return callback

    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", '["tcp/127.0.0.1:7447"]')
    with zenoh.open(config) as session:
        subscribers = [
            session.declare_subscriber(
                f"{prefix}/camera/image", record("frames", wire.FrameMetadata, attachment=True)
            ),
            session.declare_subscriber(
                f"{prefix}/camera/status", record("camera", wire.CameraStatus)
            ),
            session.declare_subscriber(
                f"{prefix}/detections", record("detections", wire.DetectionResult)
            ),
            session.declare_subscriber(
                f"{prefix}/diagnostics/detector", record("detector", wire.DetectorStatus)
            ),
            session.declare_subscriber(
                f"{prefix}/diagnostics/pixel_center_controller",
                record("controller", wire.PixelCenterControllerStatus),
            ),
            session.liveliness().declare_subscriber(
                f"{prefix}/liveliness/camera",
                record_liveliness("camera_liveliness"),
                history=True,
            ),
            session.liveliness().declare_subscriber(
                f"{prefix}/liveliness/detector",
                record_liveliness("detector_liveliness"),
                history=True,
            ),
            session.liveliness().declare_subscriber(
                f"{prefix}/liveliness/pixel_center_controller",
                record_liveliness("controller_liveliness"),
                history=True,
            ),
        ]
        deadline = time.monotonic() + 20
        with condition:
            while time.monotonic() < deadline and not all(received.values()):
                condition.wait(deadline - time.monotonic())
            if os.environ.get("FACE_TRACKING_PI_PERFORMANCE") == "1":
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and len(received["detector"]) < 6:
                    condition.wait(deadline - time.monotonic())
        for subscriber in reversed(subscribers):
            subscriber.undeclare()

    assert all(received.values()), {name: len(values) for name, values in received.items()}
    wire_channels = ("frames", "camera", "detections", "detector", "controller")
    assert all(
        message.schema_version == 2
        for name in wire_channels
        for message in received[name]
    )
    assert received["frames"][-1].source_instance_id
    assert received["camera"][-1].publish_fps <= 11.5
    if os.environ.get("FACE_TRACKING_PI_PERFORMANCE") == "1":
        detector_samples = received["detector"][-5:]
        detection = received["detections"][-1]
        average_inference_fps = sum(sample.inference_fps for sample in detector_samples) / len(
            detector_samples
        )
        assert average_inference_fps >= 4.8, {
            "inference_fps": [sample.inference_fps for sample in detector_samples],
            "inference_ms": detection.inference_ms,
            "processed_frames": detector_samples[-1].processed_frames,
            "dropped_frames": detector_samples[-1].dropped_frames,
        }


def test_controller_uses_synthetic_target_when_no_real_face_is_present() -> None:
    import zenoh

    device_id = os.environ.get("FACE_TRACKING_DEVICE_ID", "raspberrypi")
    prefix = f"face_tracking/{device_id}"
    commands: list[wire.PanTiltDelta] = []
    condition = threading.Condition()

    def on_command(sample: object) -> None:
        with condition:
            commands.append(wire.PanTiltDelta.FromString(bytes(getattr(sample, "payload"))))
            condition.notify_all()

    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", '["tcp/127.0.0.1:7447"]')
    with zenoh.open(config) as session:
        subscriber = session.declare_subscriber(f"{prefix}/pan_tilt/delta_cmd", on_command)
        now = time.time_ns()
        observation = wire.SelectedTargetObservation(
            schema_version=2,
            source_instance_id="synthetic-camera",
            tracker_instance_id="synthetic-tracker",
            sequence=100,
            captured_at_unix_ns=now,
            selected_track_id=7,
            target_center_x=740,
            target_center_y=300,
            image_width=1280,
            image_height=720,
            tracking_state=wire.TRACKING_STATE_TRACKING,
        )
        session.put(f"{prefix}/target/selected", observation.SerializeToString())
        deadline = time.monotonic() + 2
        with condition:
            while time.monotonic() < deadline and not commands:
                condition.wait(deadline - time.monotonic())
        subscriber.undeclare()

    assert commands
    assert commands[0].reason == wire.CONTROLLER_DECISION_APPLIED
    assert commands[0].delta_pan_deg == pytest.approx(1.0)
    assert commands[0].delta_tilt_deg == pytest.approx(-0.6)
