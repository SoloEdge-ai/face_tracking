from face_tracking_hmi.domain import DetectionBox, DetectionResult, TrackingState
from face_tracking_hmi.target_manager import TargetManager, TargetSelection


def detection(sequence: int, boxes: tuple[DetectionBox, ...]) -> DetectionResult:
    return DetectionResult(
        "camera-a", "tracker-a", sequence, sequence * 100_000_000, 1280, 720, 5.0, boxes
    )


def test_selected_face_moves_through_tracking_lost_reacquired_and_no_target() -> None:
    manager = TargetManager(lost_after_ms=400, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    face = DetectionBox(100, 200, 80, 100, 0.9, 7)
    manager.observe_detection(detection(1, (face,)), received_at_unix_ns=1_000_000_000)

    selected = manager.select(
        TargetSelection("camera-a", "tracker-a", 1, 7), now_unix_ns=1_100_000_000
    )
    assert selected.tracking_state == TrackingState.TRACKING
    assert selected.target_center_x == 140
    assert selected.target_center_y == 250

    lost = manager.tick(now_unix_ns=1_600_000_000)
    assert lost is not None and lost.tracking_state == TrackingState.LOST

    moved = DetectionBox(130, 200, 80, 100, 0.88, 7)
    reacquired = manager.observe_detection(
        detection(2, (moved,)), received_at_unix_ns=1_700_000_000
    )
    assert reacquired is not None and reacquired.tracking_state == TrackingState.TRACKING
    assert reacquired.target_center_x == 170

    lost_again = manager.tick(now_unix_ns=2_200_000_000)
    assert lost_again is not None and lost_again.tracking_state == TrackingState.LOST
    cleared = manager.tick(now_unix_ns=2_800_000_000)
    assert cleared is not None and cleared.tracking_state == TrackingState.NO_TARGET
    assert manager.snapshot()["selected_track_id"] is None


def test_tracker_restart_clears_selected_target() -> None:
    manager = TargetManager(lost_after_ms=400, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    manager.observe_detection(
        detection(1, (DetectionBox(1, 2, 30, 40, 0.9, 3),)),
        received_at_unix_ns=1_000_000_000,
    )
    manager.select(TargetSelection("camera-a", "tracker-a", 1, 3), now_unix_ns=1_100_000_000)
    restarted = DetectionResult("camera-a", "tracker-b", 2, 200_000_000, 1280, 720, 5.0, ())
    cleared = manager.observe_detection(restarted, received_at_unix_ns=1_200_000_000)
    assert cleared is not None and cleared.tracking_state == TrackingState.NO_TARGET
