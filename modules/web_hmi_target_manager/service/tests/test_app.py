import pytest
from face_tracking_hmi.app import create_app, mjpeg_chunks
from face_tracking_hmi.domain import DetectionBox, DetectionResult, FrameMetadata
from face_tracking_hmi.store import LatestFrameStore
from face_tracking_hmi.target_manager import TargetManager
from fastapi.testclient import TestClient


class FakeTransport:
    def __init__(self) -> None:
        self.started = False
        self.stopped = False
        self.published = []

    def start(self) -> None:
        self.started = True

    def stop(self) -> None:
        self.stopped = True

    def publish_selected_target(self, observation: object) -> None:
        self.published.append(observation)


def test_status_endpoint_exposes_latest_frame() -> None:
    store = LatestFrameStore()
    store.update(b"\xff\xd8jpeg\xff\xd9", FrameMetadata("camera", 3, 1_000_000_000, 1280, 720, "MJPG", 80), received_at_unix_ns=1_000_000_000)
    response = TestClient(create_app(store)).get("/api/camera/status")
    assert response.status_code == 200
    assert response.json()["sequence"] == 3


def test_health_works_without_camera() -> None:
    assert TestClient(create_app(LatestFrameStore())).get("/healthz").json() == {"status": "ok"}


def test_detection_websocket_pushes_boxes() -> None:
    store = LatestFrameStore()
    store.update_detection(DetectionResult("camera", "", 4, 1, 1280, 720, 8.5, (DetectionBox(1, 2, 30, 40, 0.9),)))
    with TestClient(create_app(store)).websocket_connect("/ws/detections") as socket:
        payload = socket.receive_json()
    assert payload["sequence"] == 4
    assert payload["boxes"][0]["confidence"] == 0.9


def test_user_can_select_and_clear_a_recent_tracked_face() -> None:
    store = LatestFrameStore()
    manager = TargetManager(lost_after_ms=400, reacquire_timeout_ms=1000, selection_max_age_ms=1000)
    transport = FakeTransport()
    result = DetectionResult(
        "camera", "tracker", 4, 1, 1280, 720, 8.5,
        (DetectionBox(1, 2, 30, 40, 0.9, 9),),
    )
    store.update_detection(result)
    manager.observe_detection(result)
    client = TestClient(create_app(store, target_manager=manager, transport=transport))

    response = client.put(
        "/api/target",
        json={"source_instance_id": "camera", "tracker_instance_id": "tracker", "sequence": 4, "track_id": 9},
    )
    assert response.status_code == 200
    assert response.json()["tracking_state"] == "TRACKING"
    assert transport.published[-1].selected_track_id == 9
    faces = client.get("/api/faces").json()
    assert faces["faces"][0]["track_id"] == 9
    assert faces["tracker_instance_id"] == "tracker"

    cleared = client.delete("/api/target")
    assert cleared.status_code == 200
    assert cleared.json()["tracking_state"] == "NO_TARGET"
    assert transport.published[-1].selected_track_id == 0


def test_transport_follows_application_lifespan() -> None:
    transport = FakeTransport()
    with TestClient(create_app(LatestFrameStore(), transport=transport)) as client:
        assert client.get("/healthz").status_code == 200
        assert transport.started
        assert not transport.stopped
    assert transport.stopped


@pytest.mark.asyncio
async def test_mjpeg_stream_contains_latest_frame_headers_and_payload() -> None:
    store = LatestFrameStore()
    jpeg = b"\xff\xd8jpeg\xff\xd9"
    store.update(jpeg, FrameMetadata("camera", 9, 1, 1280, 720, "MJPG", 80))
    stream = mjpeg_chunks(store)
    chunk = await anext(stream)
    await stream.aclose()
    assert b"Content-Type: image/jpeg" in chunk
    assert b"X-Frame-Sequence: 9" in chunk
    assert jpeg in chunk
