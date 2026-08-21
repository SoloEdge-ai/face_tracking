"""FastAPI HMI service and its Zenoh subscriber bridge."""

from __future__ import annotations

import asyncio
import json
import threading
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from .detection_protocol import decode_detection, encode_detection
from .frame_store import LatestFrameStore
from .protocol import ProtocolError, decode_frame_metadata
from .settings import Settings, load_settings

BOUNDARY = "frame"


def create_app(store: LatestFrameStore, *, frontend_dir: Path | None = None) -> FastAPI:
    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        yield

    app = FastAPI(title="Face Tracking HMI", lifespan=lifespan)

    @app.get("/healthz")
    def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/api/camera/status")
    def camera_status() -> JSONResponse:
        return JSONResponse(store.status())

    @app.get("/api/camera/stream.mjpg")
    async def camera_stream() -> StreamingResponse:
        async def generate() -> AsyncIterator[bytes]:
            last_key: tuple[str, int] | None = None
            while True:
                frame = await asyncio.to_thread(store.wait_for_after, last_key, 1.0)
                if frame is None:
                    continue
                last_key = (frame.metadata.source_instance_id, frame.metadata.sequence)
                headers = (
                    f"--{BOUNDARY}\r\n"
                    "Content-Type: image/jpeg\r\n"
                    f"Content-Length: {len(frame.jpeg)}\r\n"
                    f"X-Frame-Sequence: {frame.metadata.sequence}\r\n\r\n"
                ).encode("ascii")
                yield headers + frame.jpeg + b"\r\n"

        return StreamingResponse(
            generate(),
            media_type=f"multipart/x-mixed-replace; boundary={BOUNDARY}",
            headers={"Cache-Control": "no-store"},
        )

    @app.websocket("/ws/status")
    async def status_socket(websocket: WebSocket) -> None:
        await websocket.accept()
        try:
            while True:
                await websocket.send_json(store.status())
                await asyncio.sleep(1)
        except WebSocketDisconnect:
            return

    @app.websocket("/ws/detections")
    async def detection_socket(websocket: WebSocket) -> None:
        await websocket.accept()
        last_key: tuple[str, int] | None = None
        try:
            while True:
                detection = await asyncio.to_thread(store.wait_for_detection_after, last_key, 1.0)
                if detection is None:
                    await websocket.send_json({"boxes": []})
                    continue
                last_key = (detection.source_instance_id, detection.sequence)
                await websocket.send_json(json.loads(encode_detection(detection)))
        except WebSocketDisconnect:
            return

    if frontend_dir and frontend_dir.is_dir():
        app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")
    return app


class ZenohHmiSubscriber:
    """Owns one Zenoh session and pushes valid samples into a LatestFrameStore."""

    def __init__(self, settings: Settings, store: LatestFrameStore) -> None:
        self._settings = settings
        self._store = store
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="zenoh-hmi-subscriber", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def _run(self) -> None:
        from .zenoh_transport import open_zenoh_session

        with open_zenoh_session(self._settings.zenoh.connect) as session:

            def on_sample(sample: object) -> None:
                try:
                    payload = bytes(getattr(sample, "payload"))
                    attachment = getattr(sample, "attachment", None)
                    if attachment is None:
                        raise ProtocolError("missing metadata attachment")
                    metadata = decode_frame_metadata(bytes(attachment))
                    self._store.update(payload, metadata)
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            subscriber = session.declare_subscriber(self._settings.camera_image_key, on_sample)

            def on_status(sample: object) -> None:
                try:
                    raw_status = json.loads(bytes(getattr(sample, "payload")).decode("utf-8"))
                    if not isinstance(raw_status, dict) or raw_status.get("schema_version") != 1:
                        raise ValueError("invalid camera status")
                    self._store.update_driver_status(raw_status)
                except (TypeError, ValueError, UnicodeDecodeError, json.JSONDecodeError):
                    self._store.reject_invalid_frame()

            status_subscriber = session.declare_subscriber(
                self._settings.camera_status_key, on_status
            )

            def on_detection(sample: object) -> None:
                try:
                    self._store.update_detection(
                        decode_detection(bytes(getattr(sample, "payload")))
                    )
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            detection_subscriber = session.declare_subscriber(
                self._settings.detections_key, on_detection
            )
            try:
                self._stop.wait()
            finally:
                close = getattr(detection_subscriber, "undeclare", None)
                if callable(close):
                    close()
                close = getattr(status_subscriber, "undeclare", None)
                if callable(close):
                    close()
                close = getattr(subscriber, "undeclare", None)
                if callable(close):
                    close()


def main() -> None:
    import uvicorn

    settings = load_settings()
    store = LatestFrameStore(
        stale_after_ms=settings.hmi.stale_after_ms,
        offline_after_ms=settings.hmi.offline_after_ms,
    )
    subscriber = ZenohHmiSubscriber(settings, store)
    subscriber.start()
    frontend_dir = Path(__file__).resolve().parents[2] / "frontend" / "dist"
    try:
        uvicorn.run(
            create_app(store, frontend_dir=frontend_dir),
            host=settings.hmi.host,
            port=settings.hmi.port,
        )
    finally:
        subscriber.stop()


if __name__ == "__main__":
    main()
