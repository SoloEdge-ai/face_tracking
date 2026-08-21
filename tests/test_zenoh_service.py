from __future__ import annotations

import subprocess

import pytest

from face_tracking.zenoh_service import ZenohServiceError, ensure_system_zenoh


class FakeRunner:
    def __init__(self, *, active: bool = False, listener: str = "") -> None:
        self.active = active
        self.listener = listener
        self.commands: list[tuple[str, ...]] = []
        self.identities: dict[int, tuple[int, str]] = {}
        self.start_succeeds = True

    def __call__(self, command: tuple[str, ...]) -> subprocess.CompletedProcess[str]:
        self.commands.append(tuple(command))
        if command[:3] == ("systemctl", "is-active", "--quiet"):
            return subprocess.CompletedProcess(command, 0 if self.active else 3, "", "")
        if command[:3] == ("sudo", "ss", "-ltnp"):
            return subprocess.CompletedProcess(command, 0, self.listener, "")
        if command[:3] == ("sudo", "stat", "-c"):
            pid = int(command[-1].split("/")[2])
            return subprocess.CompletedProcess(command, 0, str(self.identities[pid][0]), "")
        if command[:3] == ("sudo", "readlink", "-f"):
            pid = int(command[-1].split("/")[2])
            return subprocess.CompletedProcess(command, 0, self.identities[pid][1], "")
        if command[:3] == ("sudo", "systemctl", "start"):
            if self.start_succeeds:
                self.active = True
                self.listener = 'LISTEN 0 1 *:7447 *:* users:(("zenohd",pid=999,fd=3))'
                return subprocess.CompletedProcess(command, 0, "", "")
            return subprocess.CompletedProcess(command, 1, "", "failed")
        if command[:2] == ("kill", "-INT"):
            self.listener = ""
        return subprocess.CompletedProcess(command, 0, "", "")


def test_active_system_service_is_reused() -> None:
    runner = FakeRunner(active=True)
    ensure_system_zenoh(run=runner, current_uid=1000, sleep=lambda _: None)
    assert runner.commands == [("systemctl", "is-active", "--quiet", "zenohd")]


def test_idle_port_starts_system_service() -> None:
    runner = FakeRunner()
    ensure_system_zenoh(run=runner, current_uid=1000, sleep=lambda _: None)
    assert ("sudo", "systemctl", "reset-failed", "zenohd") in runner.commands
    assert ("sudo", "systemctl", "start", "zenohd") in runner.commands


def test_only_own_manual_zenohd_is_interrupted() -> None:
    runner = FakeRunner(listener='users:(("zenohd",pid=123,fd=3))')
    runner.identities[123] = (1000, "/usr/bin/zenohd")
    ensure_system_zenoh(run=runner, current_uid=1000, sleep=lambda _: None)
    assert ("kill", "-INT", "123") in runner.commands


def test_other_listener_is_never_interrupted() -> None:
    runner = FakeRunner(listener='users:(("python",pid=321,fd=3))')
    runner.identities[321] = (1000, "/usr/bin/python3")
    with pytest.raises(ZenohServiceError, match="cannot be safely replaced"):
        ensure_system_zenoh(run=runner, current_uid=1000, sleep=lambda _: None)
    assert not any(command[0] == "kill" for command in runner.commands)


def test_failed_service_start_reports_journal() -> None:
    runner = FakeRunner()
    runner.start_succeeds = False
    with pytest.raises(ZenohServiceError, match="failed to start"):
        ensure_system_zenoh(run=runner, current_uid=1000, sleep=lambda _: None)
