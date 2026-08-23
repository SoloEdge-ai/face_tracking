from face_tracking_hmi.domain import DetectionBox, DetectionResult, FrameMetadata, HmiCameraState
from face_tracking_hmi.store import LatestFrameStore


def metadata(sequence: int = 1, captured_at: int = 1_000_000_000) -> FrameMetadata:
    return FrameMetadata("camera", sequence, captured_at, 1280, 720, "MJPG", 80)


def test_store_keeps_latest_unique_jpeg() -> None:
    store = LatestFrameStore()
    assert store.update(b"\xff\xd8first\xff\xd9", metadata(1), received_at_unix_ns=2)
    assert not store.update(b"\xff\xd8duplicate\xff\xd9", metadata(1), received_at_unix_ns=3)
    assert store.update(b"\xff\xd8second\xff\xd9", metadata(2), received_at_unix_ns=4)
    assert store.snapshot().metadata.sequence == 2


def test_store_reports_stale_and_offline_from_capture_age() -> None:
    store = LatestFrameStore(stale_after_ms=500, offline_after_ms=3000)
    store.update(b"\xff\xd8jpeg\xff\xd9", metadata(), received_at_unix_ns=1_000_000_000)
    assert store.status(now_unix_ns=1_400_000_000)["state"] == HmiCameraState.STREAMING
    assert store.status(now_unix_ns=1_600_000_000)["state"] == HmiCameraState.STALE
    assert store.status(now_unix_ns=4_100_000_000)["state"] == HmiCameraState.OFFLINE


def test_store_rejects_non_jpeg() -> None:
    store = LatestFrameStore()
    assert not store.update(b"bad", metadata())
    assert store.status()["invalid_frames"] == 1


def test_detection_expires_after_one_second() -> None:
    store = LatestFrameStore()
    result = DetectionResult("camera", "", 1, 1, 1280, 720, 5.0, (DetectionBox(1, 2, 3, 4, 0.9),))
    store.update_detection(result, received_at_unix_ns=1_000_000_000)
    assert store.detection(now_unix_ns=1_500_000_000) == result
    assert store.detection(now_unix_ns=2_100_000_000) is None
