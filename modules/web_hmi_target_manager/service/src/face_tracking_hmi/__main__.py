from pathlib import Path

import uvicorn

from .app import create_app
from .config import load_settings
from .store import LatestFrameStore
from .target_manager import TargetManager
from .zenoh_adapter import ZenohTransport


def main() -> None:
    settings = load_settings()
    store = LatestFrameStore(stale_after_ms=settings.stale_after_ms, offline_after_ms=settings.offline_after_ms)
    target_manager = TargetManager(
        missing_frame_threshold=settings.target_missing_frame_threshold,
        reacquire_timeout_ms=settings.target_reacquire_timeout_ms,
        selection_max_age_ms=settings.selection_max_age_ms,
    )
    if settings.adapter != "zenoh":
        raise RuntimeError(f"HMI executable does not provide adapter: {settings.adapter}")
    transport = ZenohTransport(settings, store, target_manager)
    frontend_dir = Path(__file__).resolve().parents[3] / "web" / "dist"
    uvicorn.run(
        create_app(store, frontend_dir=frontend_dir, transport=transport, target_manager=target_manager),
        host=settings.host,
        port=settings.port,
    )


if __name__ == "__main__":
    main()
