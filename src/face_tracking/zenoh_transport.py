"""The production Zenoh 1.x transport boundary."""

from __future__ import annotations

import json
from collections.abc import Iterator
from contextlib import contextmanager

from .protocol import FrameMetadata, encode_frame_metadata
from .settings import Settings


def _zenoh_module():
    try:
        import zenoh
    except ImportError as exc:
        raise RuntimeError(
            "Zenoh Python binding is missing. Install python3-eclipse-zenoh before starting services."
        ) from exc
    return zenoh


@contextmanager
def open_zenoh_session(connect: str) -> Iterator[object]:
    zenoh = _zenoh_module()
    config = zenoh.Config()
    config.insert_json5("mode", '"client"')
    config.insert_json5("connect/endpoints", json.dumps([connect]))
    session = zenoh.open(config)
    try:
        yield session
    finally:
        close = getattr(session, "close", None)
        if callable(close):
            close()


class ZenohCameraPublisher:
    def __init__(self, settings: Settings) -> None:
        self._settings = settings
        self._session_context = open_zenoh_session(settings.zenoh.connect)
        self._session: object | None = None
        self._image_publisher: object | None = None
        self._status_publisher: object | None = None
        self._liveliness_token: object | None = None

    def __enter__(self) -> ZenohCameraPublisher:
        zenoh = _zenoh_module()
        self._session = self._session_context.__enter__()
        self._image_publisher = self._session.declare_publisher(
            self._settings.camera_image_key,
            congestion_control=zenoh.CongestionControl.DROP,
            priority=zenoh.Priority.DATA_LOW,
        )
        self._status_publisher = self._session.declare_publisher(self._settings.camera_status_key)
        self._liveliness_token = self._session.liveliness().declare_token(
            self._settings.camera_liveliness_key
        )
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        for declaration in (self._liveliness_token, self._status_publisher, self._image_publisher):
            undeclare = getattr(declaration, "undeclare", None)
            if callable(undeclare):
                undeclare()
        self._session_context.__exit__(exc_type, exc, traceback)

    def publish_image(self, jpeg: bytes, metadata: FrameMetadata) -> None:
        if self._image_publisher is None:
            raise RuntimeError("Zenoh publisher is not open")
        self._image_publisher.put(
            jpeg,
            encoding="image/jpeg",
            attachment=encode_frame_metadata(metadata),
        )

    def publish_status(self, status: dict[str, object]) -> None:
        if self._status_publisher is None:
            raise RuntimeError("Zenoh publisher is not open")
        self._status_publisher.put(
            json.dumps(status, separators=(",", ":"), sort_keys=True),
            encoding="application/json",
        )
