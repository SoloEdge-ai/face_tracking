"""Safely hand TCP/7447 from a manual router to ``zenohd.service``."""

from __future__ import annotations

import os
import re
import subprocess
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass


class ZenohServiceError(RuntimeError):
    """The system router could not be safely made available."""


@dataclass(frozen=True, slots=True)
class ProcessIdentity:
    pid: int
    uid: int | None
    executable: str | None


Runner = Callable[[Sequence[str]], subprocess.CompletedProcess[str]]


def _run(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, check=False)


def _is_active(run: Runner) -> bool:
    return run(("systemctl", "is-active", "--quiet", "zenohd")).returncode == 0


def _listeners(run: Runner) -> list[int]:
    result = run(("sudo", "ss", "-ltnp", "sport", "=", ":7447"))
    return sorted({int(pid) for pid in re.findall(r"pid=(\d+)", result.stdout)})


def _identity(run: Runner, pid: int) -> ProcessIdentity:
    uid_result = run(("sudo", "stat", "-c", "%u", f"/proc/{pid}"))
    exe_result = run(("sudo", "readlink", "-f", f"/proc/{pid}/exe"))
    try:
        uid = int(uid_result.stdout.strip()) if uid_result.returncode == 0 else None
    except ValueError:
        uid = None
    executable = exe_result.stdout.strip() if exe_result.returncode == 0 else None
    return ProcessIdentity(pid=pid, uid=uid, executable=executable)


def _journal(run: Runner) -> str:
    result = run(("sudo", "journalctl", "-u", "zenohd", "-n", "30", "--no-pager"))
    return result.stdout.strip() or result.stderr.strip()


def ensure_system_zenoh(
    *,
    run: Runner = _run,
    current_uid: int | None = None,
    sleep: Callable[[float], None] = time.sleep,
) -> None:
    """Ensure the enabled-by-user system unit owns port 7447.

    A manual router is interrupted only when it is the caller's own exact
    ``/usr/bin/zenohd`` process. Any ambiguity is an error, never a kill.
    """
    if _is_active(run):
        return

    current_uid = os.getuid() if current_uid is None else current_uid
    pids = _listeners(run)
    if pids:
        identities = [_identity(run, pid) for pid in pids]
        unsafe = [
            process
            for process in identities
            if process.uid != current_uid or process.executable != "/usr/bin/zenohd"
        ]
        if unsafe:
            detail = "; ".join(
                f"pid={item.pid} uid={item.uid!r} exe={item.executable!r}" for item in unsafe
            )
            raise ZenohServiceError(
                "TCP 7447 is owned by a process that cannot be safely replaced: " + detail
            )
        for process in identities:
            result = run(("kill", "-INT", str(process.pid)))
            if result.returncode != 0:
                raise ZenohServiceError(f"could not send SIGINT to manual zenohd pid {process.pid}")
        for _ in range(10):
            if not _listeners(run):
                break
            sleep(1)
        else:
            raise ZenohServiceError("manual zenohd did not release TCP 7447 within 10 seconds")

    run(("sudo", "systemctl", "reset-failed", "zenohd"))
    started = run(("sudo", "systemctl", "start", "zenohd"))
    if started.returncode != 0:
        raise ZenohServiceError("zenohd.service failed to start\n" + _journal(run))

    for _ in range(10):
        if _is_active(run) and _listeners(run):
            return
        sleep(1)
    raise ZenohServiceError(
        "zenohd.service is not active and listening on TCP 7447\n" + _journal(run)
    )


def main() -> None:
    try:
        ensure_system_zenoh()
    except ZenohServiceError as exc:
        raise SystemExit(f"Zenoh service handover failed: {exc}") from exc


if __name__ == "__main__":
    main()
