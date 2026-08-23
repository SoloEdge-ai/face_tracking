from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel
from fastapi.responses import JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from .protocol import detection_as_dict
from .store import LatestFrameStore
from .target_manager import TargetManager, TargetSelection, TargetSelectionError
from .transport import TransportAdapter

BOUNDARY = "frame"


class TargetSelectionRequest(BaseModel):
    source_instance_id: str
    tracker_instance_id: str
    sequence: int
    track_id: int


async def mjpeg_chunks(store: LatestFrameStore) -> AsyncIterator[bytes]:
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


def create_app(
    store: LatestFrameStore,
    *,
    frontend_dir: Path | None = None,
    transport: TransportAdapter | None = None,
    target_manager: TargetManager | None = None,
) -> FastAPI:
    target_manager = target_manager or TargetManager(
        lost_after_ms=400, reacquire_timeout_ms=1000, selection_max_age_ms=1000
    )
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

    @app.get("/api/controller/status")
    def controller_status() -> JSONResponse:
        return JSONResponse(store.controller_snapshot())

    @app.get("/api/faces")
    def faces() -> JSONResponse:
        detection = store.detection()
        payload = detection_as_dict(detection) if detection else {"boxes": []}
        payload["faces"] = payload.pop("boxes")
        payload.update(target_manager.snapshot())
        return JSONResponse(payload)

    @app.get("/api/target")
    def selected_target() -> JSONResponse:
        return JSONResponse(target_manager.snapshot())

    @app.put("/api/target")
    def select_target(request: TargetSelectionRequest) -> JSONResponse:
        try:
            observation = target_manager.select(
                TargetSelection(
                    request.source_instance_id,
                    request.tracker_instance_id,
                    request.sequence,
                    request.track_id,
                )
            )
        except TargetSelectionError as error:
            raise HTTPException(status_code=409, detail=str(error)) from error
        if transport:
            transport.publish_selected_target(observation)
        return JSONResponse(target_manager.snapshot())

    @app.delete("/api/target")
    def clear_target() -> JSONResponse:
        observation = target_manager.cancel()
        if transport:
            transport.publish_selected_target(observation)
        return JSONResponse(target_manager.snapshot())

    @app.get("/api/camera/stream.mjpg")
    async def camera_stream() -> StreamingResponse:
        return StreamingResponse(
            mjpeg_chunks(store),
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
                    await websocket.send_json({"boxes": [], **target_manager.snapshot()})
                    continue
                last_key = (detection.source_instance_id, detection.sequence)
                await websocket.send_json(
                    {**detection_as_dict(detection), **target_manager.snapshot()}
                )
        except WebSocketDisconnect:
            return

    @app.websocket("/ws/controller")
    async def controller_socket(websocket: WebSocket) -> None:
        await websocket.accept()
        try:
            while True:
                await websocket.send_json(store.controller_snapshot())
                await asyncio.sleep(0.2)
        except WebSocketDisconnect:
            return

    if frontend_dir and frontend_dir.is_dir():
        app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")
    return app
