#!/usr/bin/env bash
# Starts the two project processes after safely handing Zenoh to the system unit.
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"

if [[ ! -x .venv/bin/python ]]; then
  echo "Missing .venv. Run ./setup.sh first." >&2
  exit 1
fi
if [[ ! -f frontend/dist/index.html ]]; then
  echo "Missing frontend/dist. Run ./setup.sh first." >&2
  exit 1
fi

.venv/bin/python -m face_tracking.zenoh_service

camera_pid=""
hmi_pid=""
detector_pid=""
stop_children() {
  trap - EXIT INT TERM
  for pid in "$camera_pid" "$hmi_pid" "$detector_pid"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
    fi
  done
  for pid in "$camera_pid" "$hmi_pid" "$detector_pid"; do
    if [[ -n "$pid" ]]; then
      wait "$pid" 2>/dev/null || true
    fi
  done
}
on_signal() {
  stop_children
  exit 0
}
trap on_signal INT TERM
trap stop_children EXIT

.venv/bin/python -m face_tracking.camera_driver &
camera_pid=$!
.venv/bin/python -m face_tracking.face_detector &
detector_pid=$!
.venv/bin/python -m face_tracking.hmi &
hmi_pid=$!

echo "Camera and HMI started. Open http://$(hostname -I | awk '{print $1}'):8080"
wait -n "$camera_pid" "$detector_pid" "$hmi_pid"
echo "A project process stopped; shutting down the other one." >&2
exit 1
