from face_tracking.detection_protocol import DetectionResult
from face_tracking.frame_store import HmiCameraState, LatestFrameStore
from face_tracking.protocol import FrameMetadata


def metadata(sequence: int = 1) -> FrameMetadata:
    return FrameMetadata(
        source_instance_id="camera-a",
        sequence=sequence,
        captured_at_unix_ns=1_000_000_000,
        width=1280,
        height=720,
        capture_format="MJPG",
        jpeg_quality=80,
    )


def test_store_keeps_only_the_latest_unique_jpeg_frame() -> None:
    store = LatestFrameStore()

    assert store.update(b"\xff\xd8first\xff\xd9", metadata(1), received_at_unix_ns=1_000_000_000)
    assert not store.update(
        b"\xff\xd8duplicate\xff\xd9", metadata(1), received_at_unix_ns=1_100_000_000
    )
    assert store.update(b"\xff\xd8second\xff\xd9", metadata(2), received_at_unix_ns=1_200_000_000)

    frame = store.snapshot()
    assert frame is not None
    assert frame.jpeg == b"\xff\xd8second\xff\xd9"
    assert frame.metadata.sequence == 2


def test_store_reports_stale_and_offline_from_capture_age() -> None:
    store = LatestFrameStore(stale_after_ms=500, offline_after_ms=3000)
    store.update(b"\xff\xd8frame\xff\xd9", metadata(), received_at_unix_ns=1_000_000_000)

    assert store.status(now_unix_ns=1_400_000_000)["state"] == HmiCameraState.STREAMING
    assert store.status(now_unix_ns=1_600_000_000)["state"] == HmiCameraState.STALE
    assert store.status(now_unix_ns=4_100_000_000)["state"] == HmiCameraState.OFFLINE
    assert store.status(now_unix_ns=4_100_000_000)["hmi_fps"] == 0


def test_store_rejects_non_jpeg_payloads() -> None:
    store = LatestFrameStore()

    assert not store.update(b"not-a-jpeg", metadata())
    assert store.status()["invalid_frames"] == 1


def test_detection_expires_after_one_second() -> None:
    store = LatestFrameStore()
    result = DetectionResult("camera-a", 1, 1, 1280, 720, 1, ())
    store.update_detection(result, received_at_unix_ns=1_000_000_000)

    assert store.detection(now_unix_ns=1_900_000_000) == result
    assert store.detection(now_unix_ns=2_100_000_000) is None
