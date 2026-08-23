from pathlib import Path

import uvicorn

from .app import create_app
from .config import load_settings
from .store import LatestFrameStore
from .transport import create_transport


def main() -> None:
    settings = load_settings()
    store = LatestFrameStore(stale_after_ms=settings.stale_after_ms, offline_after_ms=settings.offline_after_ms)
    transport = create_transport(settings, store)
    frontend_dir = Path(__file__).resolve().parents[3] / "web" / "dist"
    uvicorn.run(
        create_app(store, frontend_dir=frontend_dir, transport=transport),
        host=settings.host,
        port=settings.port,
    )


if __name__ == "__main__":
    main()
