from dataclasses import dataclass, field

from face_tracking.detection_protocol import DetectionBox, DetectionResult
from face_tracking.face_detector import CameraSample, FaceDetectorLoop, LatestCameraSample
from face_tracking.protocol import FrameMetadata


@dataclass
class Publisher:
    detections: list[DetectionResult] = field(default_factory=list)
    statuses: list[dict] = field(default_factory=list)

    def publish_detection(self, result: DetectionResult) -> None:
        self.detections.append(result)

    def publish_status(self, status: dict) -> None:
        self.statuses.append(status)


def sample(sequence: int) -> CameraSample:
    return CameraSample(
        jpeg=b"jpeg",
        metadata=FrameMetadata("camera", sequence, sequence + 1, 1280, 720, "MJPG", 80),
    )


def test_detector_publishes_latest_frame_at_five_hz() -> None:
    publisher = Publisher()
    loop = FaceDetectorLoop(publisher, inference_hz=5)

    for index in range(11):
        loop.process_if_due(
            sample(index),
            monotonic_seconds=index / 10,
            decode=lambda _: object(),
            infer=lambda _: [DetectionBox(0, 0, 100, 100, 0.9)],
        )

    assert [result.sequence for result in publisher.detections] == [0, 2, 4, 6, 8, 10]


def test_detector_publishes_empty_result_when_no_face_is_found() -> None:
    publisher = Publisher()
    loop = FaceDetectorLoop(publisher, inference_hz=5)

    assert loop.process_if_due(
        sample(1), monotonic_seconds=0, decode=lambda _: object(), infer=lambda _: []
    )
    assert publisher.detections[0].boxes == ()


def test_latest_camera_sample_overwrites_older_frame() -> None:
    latest = LatestCameraSample()
    latest.update(b"one", sample(1).metadata)
    latest.update(b"two", sample(2).metadata)

    assert latest.snapshot() == CameraSample(b"two", sample(2).metadata)
    assert latest.dropped_frames == 1
