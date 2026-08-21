from face_tracking.camera_driver import CameraPublisherLoop, LatestCapturedFrame


class FakePublisher:
    def __init__(self) -> None:
        self.frames: list[tuple[bytes, object]] = []

    def publish_image(self, jpeg: bytes, metadata: object) -> None:
        self.frames.append((jpeg, metadata))


def test_camera_loop_publishes_at_most_ten_frames_per_second() -> None:
    publisher = FakePublisher()
    loop = CameraPublisherLoop(
        publisher=publisher,
        source_instance_id="camera-a",
        width=1280,
        height=720,
        capture_format="MJPG",
        jpeg_quality=80,
        publish_hz=10,
    )

    for index in range(31):
        loop.accept_latest_jpeg(
            b"\xff\xd8" + bytes([index]), captured_at_unix_ns=1_000_000_000 + index
        )
        loop.publish_if_due(monotonic_seconds=index / 30)

    assert len(publisher.frames) == 11
    assert publisher.frames[-1][1].sequence == 10


def test_camera_loop_never_republishes_the_same_frame() -> None:
    publisher = FakePublisher()
    loop = CameraPublisherLoop(
        publisher=publisher,
        source_instance_id="camera-a",
        width=1280,
        height=720,
        capture_format="MJPG",
        jpeg_quality=80,
        publish_hz=10,
    )

    loop.accept_latest_jpeg(b"\xff\xd8frame", captured_at_unix_ns=1)
    loop.publish_if_due(monotonic_seconds=0)
    loop.publish_if_due(monotonic_seconds=1)

    assert len(publisher.frames) == 1


def test_capture_buffer_overwrites_oldest_frame() -> None:
    buffer = LatestCapturedFrame()

    buffer.update("old", captured_at_unix_ns=1)
    buffer.update("new", captured_at_unix_ns=2)

    latest = buffer.snapshot()
    assert latest is not None
    assert latest.bgr == "new"
    assert latest.captured_at_unix_ns == 2
    assert buffer.captured_frames == 2
