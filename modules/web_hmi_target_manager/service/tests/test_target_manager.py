from face_tracking_hmi.domain import DetectionBox, DetectionResult, TrackingState
from face_tracking_hmi.target_manager import TargetManager, TargetSelection


def detection(sequence: int, boxes: tuple[DetectionBox, ...]) -> DetectionResult:
    return DetectionResult(
        "camera-a", "tracker-a", sequence, sequence * 100_000_000, 1280, 720, 5.0, boxes
    )


def test_selected_face_moves_through_tracking_lost_reacquired_and_no_target() -> None:
    manager = TargetManager(missing_frame_threshold=10, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    face = DetectionBox(100, 200, 80, 100, 0.9, 7)
    manager.observe_detection(detection(1, (face,)), received_at_unix_ns=1_000_000_000)

    selected = manager.select(
        TargetSelection("camera-a", "tracker-a", 1, 7), now_unix_ns=1_100_000_000
    )
    assert selected.tracking_state == TrackingState.TRACKING
    assert selected.target_center_x == 140
    assert selected.target_center_y == 250

    for sequence in range(2, 11):
        missing = manager.observe_detection(
            detection(sequence, ()), received_at_unix_ns=1_000_000_000 + sequence * 100_000_000
        )
        assert missing is not None and missing.tracking_state == TrackingState.MISSING
    lost = manager.observe_detection(detection(11, ()), received_at_unix_ns=2_100_000_000)
    assert lost is not None and lost.tracking_state == TrackingState.LOST

    moved = DetectionBox(130, 200, 80, 100, 0.88, 7)
    reacquired = manager.observe_detection(
        detection(12, (moved,)), received_at_unix_ns=2_200_000_000
    )
    assert reacquired is not None and reacquired.tracking_state == TrackingState.TRACKING
    assert reacquired.target_center_x == 170

    for sequence in range(13, 22):
        manager.observe_detection(
            detection(sequence, ()), received_at_unix_ns=1_000_000_000 + sequence * 100_000_000
        )
    lost_again = manager.observe_detection(
        detection(22, ()), received_at_unix_ns=3_200_000_000
    )
    assert lost_again is not None and lost_again.tracking_state == TrackingState.LOST
    cleared = manager.tick(now_unix_ns=4_200_000_000)
    assert cleared is not None and cleared.tracking_state == TrackingState.NO_TARGET
    assert manager.snapshot()["selected_track_id"] is None


def test_tracker_restart_clears_selected_target() -> None:
    manager = TargetManager(missing_frame_threshold=5, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    manager.observe_detection(
        detection(1, (DetectionBox(1, 2, 30, 40, 0.9, 3),)),
        received_at_unix_ns=1_000_000_000,
    )
    manager.select(TargetSelection("camera-a", "tracker-a", 1, 3), now_unix_ns=1_100_000_000)
    restarted = DetectionResult("camera-a", "tracker-b", 2, 200_000_000, 1280, 720, 5.0, ())
    cleared = manager.observe_detection(restarted, received_at_unix_ns=1_200_000_000)
    assert cleared is not None and cleared.tracking_state == TrackingState.NO_TARGET


def test_camera_restart_clears_selected_target() -> None:
    manager = TargetManager(missing_frame_threshold=5, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    manager.observe_detection(
        detection(1, (DetectionBox(1, 2, 30, 40, 0.9, 3),)),
        received_at_unix_ns=1_000_000_000,
    )
    manager.select(TargetSelection("camera-a", "tracker-a", 1, 3), now_unix_ns=1_100_000_000)
    restarted = DetectionResult("camera-b", "tracker-a", 1, 200_000_000, 1280, 720, 5.0, ())
    cleared = manager.observe_detection(restarted, received_at_unix_ns=1_200_000_000)
    assert cleared is not None and cleared.tracking_state == TrackingState.NO_TARGET


def test_selected_face_is_lost_only_after_ten_consecutive_missing_frames() -> None:
    manager = TargetManager(
        missing_frame_threshold=10,
        reacquire_timeout_ms=1000,
        selection_max_age_ms=1000,
    )
    face = DetectionBox(100, 200, 80, 100, 0.9, 7)
    manager.observe_detection(detection(1, (face,)), received_at_unix_ns=1_000_000_000)
    manager.select(TargetSelection("camera-a", "tracker-a", 1, 7), now_unix_ns=1_100_000_000)

    for sequence in range(2, 11):
        missing = manager.observe_detection(
            detection(sequence, ()), received_at_unix_ns=1_000_000_000 + sequence * 100_000_000
        )
        assert missing is not None and missing.tracking_state == TrackingState.MISSING

    reacquired = manager.observe_detection(
        detection(11, (face,)), received_at_unix_ns=2_100_000_000
    )
    assert reacquired is not None and reacquired.tracking_state == TrackingState.TRACKING

    for sequence in range(12, 21):
        missing = manager.observe_detection(
            detection(sequence, ()), received_at_unix_ns=1_000_000_000 + sequence * 100_000_000
        )
        assert missing is not None and missing.tracking_state == TrackingState.MISSING
    lost = manager.observe_detection(
        detection(21, ()), received_at_unix_ns=3_100_000_000
    )
    assert lost is not None and lost.tracking_state == TrackingState.LOST
