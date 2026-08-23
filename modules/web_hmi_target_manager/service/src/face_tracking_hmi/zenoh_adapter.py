from __future__ import annotations

import json
import queue
import threading

from .config import Settings
from .protocol import (
    ProtocolError,
    decode_camera_status,
    decode_detection,
    decode_detector_status,
    decode_frame_metadata,
    encode_selected_target,
)
from .store import LatestFrameStore
from .target_manager import TargetManager


class ZenohTransport:
    def __init__(self, settings: Settings, store: LatestFrameStore, target_manager: TargetManager | None = None) -> None:
        self._settings, self._store = settings, store
        self._target_manager = target_manager
        self._outgoing_targets: queue.SimpleQueue[object] = queue.SimpleQueue()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="zenoh-hmi-transport", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def publish_selected_target(self, observation: object) -> None:
        self._outgoing_targets.put(observation)

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
                        result := decode_detection(bytes(getattr(sample, "payload")))
                    )
                    if self._target_manager:
                        observation = self._target_manager.observe_detection(result)
                        if observation:
                            self.publish_selected_target(observation)
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
            while not self._stop.wait(0.05):
                if self._target_manager:
                    observation = self._target_manager.tick()
                    if observation:
                        self.publish_selected_target(observation)
                while True:
                    try:
                        observation = self._outgoing_targets.get_nowait()
                    except queue.Empty:
                        break
                    session.put(
                        self._settings.key("target/selected"),
                        encode_selected_target(observation),
                    )
            for declaration in reversed(declarations):
                declaration.undeclare()
