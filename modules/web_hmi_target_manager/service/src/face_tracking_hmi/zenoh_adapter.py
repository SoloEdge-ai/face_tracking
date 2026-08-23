from __future__ import annotations

import json
import threading

from .config import Settings
from .protocol import (
    ProtocolError,
    decode_camera_status,
    decode_detection,
    decode_detector_status,
    decode_frame_metadata,
)
from .store import LatestFrameStore


class ZenohTransport:
    def __init__(self, settings: Settings, store: LatestFrameStore) -> None:
        self._settings, self._store = settings, store
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="zenoh-hmi-transport", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def _run(self) -> None:
        import zenoh

        config = zenoh.Config()
        config.insert_json5("mode", '"client"')
        config.insert_json5("connect/endpoints", json.dumps([self._settings.connect]))
        with zenoh.open(config) as session:

            def on_frame(sample: object) -> None:
                try:
                    attachment = getattr(sample, "attachment", None)
                    if attachment is None:
                        raise ProtocolError("missing frame metadata")
                    self._store.update(
                        bytes(getattr(sample, "payload")),
                        decode_frame_metadata(bytes(attachment)),
                    )
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            def on_status(sample: object) -> None:
                try:
                    self._store.update_driver_status(
                        decode_camera_status(bytes(getattr(sample, "payload")))
                    )
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            def on_detection(sample: object) -> None:
                try:
                    self._store.update_detection(
                        decode_detection(bytes(getattr(sample, "payload")))
                    )
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            def on_detector_status(sample: object) -> None:
                try:
                    status = decode_detector_status(bytes(getattr(sample, "payload")))
                    self._store.update_detector_status(status)
                except (ProtocolError, TypeError, ValueError):
                    self._store.reject_invalid_frame()

            declarations = [
                session.declare_subscriber(self._settings.key("camera/image"), on_frame),
                session.declare_subscriber(self._settings.key("camera/status"), on_status),
                session.declare_subscriber(self._settings.key("detections"), on_detection),
                session.declare_subscriber(
                    self._settings.key("diagnostics/detector"), on_detector_status
                ),
            ]
            self._stop.wait()
            for declaration in reversed(declarations):
                declaration.undeclare()
