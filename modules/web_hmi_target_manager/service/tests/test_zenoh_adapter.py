import sys
import threading
from types import SimpleNamespace

from pytest import MonkeyPatch

from face_tracking_hmi.config import Settings
from face_tracking_hmi.store import LatestFrameStore
from face_tracking_hmi.zenoh_adapter import ZenohTransport


class FakeDeclaration:
    def __init__(self) -> None:
        self.undeclared = False

    def undeclare(self) -> None:
        self.undeclared = True


class FakeSession:
    def __init__(self) -> None:
        self.entered = threading.Event()
        self.keys: list[str] = []
        self.declarations: list[FakeDeclaration] = []

    def __enter__(self) -> "FakeSession":
        self.entered.set()
        return self

    def __exit__(self, *_: object) -> None:
        return None

    def declare_subscriber(self, key: str, _: object) -> FakeDeclaration:
        self.keys.append(key)
        declaration = FakeDeclaration()
        self.declarations.append(declaration)
        return declaration


class FakeConfig:
    def insert_json5(self, _: str, __: str) -> None:
        return None


def test_zenoh_adapter_declares_and_releases_all_subscribers(monkeypatch: MonkeyPatch) -> None:
    session = FakeSession()
    fake_zenoh = SimpleNamespace(Config=FakeConfig, open=lambda _: session)
    monkeypatch.setitem(sys.modules, "zenoh", fake_zenoh)
    settings = Settings("pi", "zenoh", "tcp/127.0.0.1:7447", "face_tracking", "0.0.0.0", 8080, 500, 3000)
    transport = ZenohTransport(settings, LatestFrameStore())

    transport.start()
    assert session.entered.wait(timeout=1)
    transport.stop()

    assert session.keys == [
        "face_tracking/pi/camera/image",
        "face_tracking/pi/camera/status",
        "face_tracking/pi/detections",
        "face_tracking/pi/diagnostics/detector",
        "face_tracking/pi/pan_tilt/delta_cmd",
        "face_tracking/pi/diagnostics/pixel_center_controller",
        "face_tracking/pi/pan_tilt/commanded_state",
    ]
    assert all(declaration.undeclared for declaration in session.declarations)
