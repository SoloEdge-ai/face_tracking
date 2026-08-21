from fastapi.testclient import TestClient

from face_tracking.detection_protocol import DetectionBox, DetectionResult
from face_tracking.frame_store import LatestFrameStore
from face_tracking.hmi import create_app
from face_tracking.protocol import FrameMetadata


def test_status_endpoint_exposes_latest_frame_state() -> None:
    store = LatestFrameStore()
    store.update(
        b"\xff\xd8frame\xff\xd9",
        FrameMetadata(
            source_instance_id="camera-a",
            sequence=7,
            captured_at_unix_ns=__import__("time").time_ns(),
            width=1280,
            height=720,
            capture_format="MJPG",
            jpeg_quality=80,
        ),
    )
    client = TestClient(create_app(store))

    response = client.get("/api/camera/status")

    assert response.status_code == 200
    assert response.json()["state"] == "STREAMING"
    assert response.json()["sequence"] == 7


def test_health_endpoint_does_not_require_a_camera_frame() -> None:
    client = TestClient(create_app(LatestFrameStore()))

    response = client.get("/healthz")

    assert response.status_code == 200
    assert response.json() == {"status": "ok"}


def test_detection_websocket_pushes_boxes() -> None:
    store = LatestFrameStore()
    store.update_detection(
        DetectionResult(
            source_instance_id="camera-a",
            sequence=8,
            captured_at_unix_ns=1,
            image_width=1280,
            image_height=720,
            inference_ms=10,
            boxes=(DetectionBox(1, 2, 3, 4, 0.9),),
        )
    )

    with TestClient(create_app(store)).websocket_connect("/ws/detections") as socket:
        payload = socket.receive_json()

    assert payload["sequence"] == 8
    assert payload["boxes"][0]["confidence"] == 0.9
