from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from .protocol import detection_as_dict
from .store import LatestFrameStore
from .transport import TransportAdapter

BOUNDARY = "frame"


def create_app(
    store: LatestFrameStore,
    *,
    frontend_dir: Path | None = None,
    transport: TransportAdapter | None = None,
) -> FastAPI:
    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        if transport:
            transport.start()
        try:
            yield
        finally:
            if transport:
                transport.stop()

    app = FastAPI(title="Face Tracking HMI", lifespan=lifespan)

    @app.get("/healthz")
    def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/api/camera/status")
    def camera_status() -> JSONResponse:
        return JSONResponse(store.status())

    @app.get("/api/detector/status")
    def detector_status() -> JSONResponse:
        return JSONResponse(store.detector_status())

    @app.get("/api/camera/stream.mjpg")
    async def camera_stream() -> StreamingResponse:
        async def generate() -> AsyncIterator[bytes]:
            last_key: tuple[str, int] | None = None
            while True:
                frame = await asyncio.to_thread(store.wait_for_after, last_key, 1.0)
                if frame is None:
                    continue
                last_key = (frame.metadata.source_instance_id, frame.metadata.sequence)
                headers = (f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\nContent-Length: {len(frame.jpeg)}\r\nX-Frame-Sequence: {frame.metadata.sequence}\r\n\r\n").encode("ascii")
                yield headers + frame.jpeg + b"\r\n"
        return StreamingResponse(generate(), media_type=f"multipart/x-mixed-replace; boundary={BOUNDARY}", headers={"Cache-Control": "no-store"})

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
                await websocket.send_json(detection_as_dict(detection))
        except WebSocketDisconnect:
            return

    if frontend_dir and frontend_dir.is_dir():
        app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")
    return app
