#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"

build_dir="${FACE_TRACKING_BUILD_DIR:-build}"
camera="$build_dir/bin/face_tracking_camera_zenoh"
detector="$build_dir/bin/face_tracking_detector_zenoh"
bringup="$build_dir/bin/face_tracking_bringup"

for executable in "$camera" "$detector" "$bringup"; do
  if [[ ! -x "$executable" ]]; then
    echo "Missing $executable. Run ./setup.sh first." >&2
    exit 1
  fi
done
if [[ ! -x .venv/bin/python ]]; then
  echo "Missing HMI virtual environment. Run ./setup.sh first." >&2
  exit 1
fi
if [[ ! -f modules/web_hmi_target_manager/web/dist/index.html ]]; then
  echo "Missing HMI frontend build. Run ./setup.sh first." >&2
  exit 1
fi

export FACE_TRACKING_CONFIG="${FACE_TRACKING_CONFIG:-config/default.yaml}"
export PYTHONNOUSERSITE=1
exec "$bringup" "$camera" "$detector" .venv/bin/python
