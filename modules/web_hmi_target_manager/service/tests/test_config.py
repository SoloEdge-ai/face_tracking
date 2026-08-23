from pathlib import Path

from face_tracking_hmi.config import load_settings


def test_config_loader_does_not_reject_future_adapter(tmp_path: Path) -> None:
    config = tmp_path / "future.yaml"
    config.write_text(
        """
common: {device_id: pi}
middleware: {adapter: ros2, connect: unused, key_prefix: face_tracking}
hmi: {host: 127.0.0.1, port: 8080, stale_after_ms: 500, offline_after_ms: 3000}
""",
        encoding="utf-8",
    )
    settings = load_settings(config)
    assert settings.adapter == "ros2"
    assert settings.target_missing_frame_threshold == 10
