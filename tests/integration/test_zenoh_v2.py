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
    assert all(message.schema_version == 2 for values in received.values() for message in values)
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
