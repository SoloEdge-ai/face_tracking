from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

import yaml


@dataclass(frozen=True, slots=True)
class Settings:
    device_id: str
    adapter: str
    connect: str
    key_prefix: str
    host: str
    port: int
    stale_after_ms: int
    offline_after_ms: int
    target_missing_frame_threshold: int = 5
    target_reacquire_timeout_ms: int = 1000
    selection_max_age_ms: int = 1000

    def key(self, suffix: str) -> str:
        return f"{self.key_prefix}/{self.device_id}/{suffix}"


def load_settings(path: str | Path | None = None) -> Settings:
    config_path = Path(path or os.environ.get("FACE_TRACKING_CONFIG", "config/default.yaml"))
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    common, middleware, hmi = raw["common"], raw["middleware"], raw["hmi"]
    target = hmi.get("target", {})
    settings = Settings(
        device_id=os.environ.get("FACE_TRACKING_DEVICE_ID", common["device_id"]),
        adapter=middleware["adapter"], connect=middleware["connect"],
        key_prefix=middleware["key_prefix"], host=hmi["host"], port=hmi["port"],
        stale_after_ms=hmi["stale_after_ms"], offline_after_ms=hmi["offline_after_ms"],
        target_missing_frame_threshold=target.get("missing_frame_threshold", 5),
        target_reacquire_timeout_ms=target.get("reacquire_timeout_ms", 1000),
        selection_max_age_ms=target.get("selection_max_age_ms", 1000),
    )
    if not settings.device_id or not settings.connect or not settings.key_prefix:
        raise ValueError("invalid common or middleware configuration")
    if settings.port <= 0 or settings.stale_after_ms <= 0 or settings.offline_after_ms <= settings.stale_after_ms:
        raise ValueError("invalid HMI configuration")
    if (
        settings.target_missing_frame_threshold <= 0
        or settings.target_reacquire_timeout_ms <= 0
        or settings.selection_max_age_ms <= 0
    ):
        raise ValueError("invalid target manager configuration")
    return settings
