"""Configuration loading for the two manual services."""

from __future__ import annotations

import os
import socket
import tomllib
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class CameraSettings:
    device_path: str
    width: int
    height: int
    capture_fps: int
    publish_hz: int
    jpeg_quality: int
    reconnect_seconds: float


@dataclass(frozen=True, slots=True)
class ZenohSettings:
    connect: str
    key_prefix: str


@dataclass(frozen=True, slots=True)
class HmiSettings:
    host: str
    port: int
    stale_after_ms: int
    offline_after_ms: int


@dataclass(frozen=True, slots=True)
class Settings:
    device_id: str
    camera: CameraSettings
    zenoh: ZenohSettings
    hmi: HmiSettings

    @property
    def camera_image_key(self) -> str:
        return f"{self.zenoh.key_prefix}/{self.device_id}/camera/image"

    @property
    def camera_status_key(self) -> str:
        return f"{self.zenoh.key_prefix}/{self.device_id}/camera/status"

    @property
    def camera_liveliness_key(self) -> str:
        return f"{self.zenoh.key_prefix}/{self.device_id}/liveliness/camera"


def load_settings(path: str | Path | None = None) -> Settings:
    config_path = Path(path or os.environ.get("FACE_TRACKING_CONFIG", "config/default.toml"))
    with config_path.open("rb") as config_file:
        raw = tomllib.load(config_file)

    device_id = os.environ.get(
        "FACE_TRACKING_DEVICE_ID", raw["device"].get("id") or socket.gethostname()
    )
    camera = raw["camera"]
    zenoh = raw["zenoh"]
    hmi = raw["hmi"]
    return Settings(
        device_id=device_id,
        camera=CameraSettings(**camera),
        zenoh=ZenohSettings(**zenoh),
        hmi=HmiSettings(**hmi),
    )
