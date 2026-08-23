#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"

build_dir="${FACE_TRACKING_BUILD_DIR:-build}"
camera="$build_dir/bin/face_tracking_camera_zenoh"
detector="$build_dir/bin/face_tracking_detector_zenoh"
controller="$build_dir/bin/face_tracking_controller_zenoh"
servo="$build_dir/bin/face_tracking_servo_zenoh"
bringup="$build_dir/bin/face_tracking_bringup"

for executable in "$camera" "$detector" "$controller" "$servo" "$bringup"; do
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
pwm_chip="$(awk '
  /^servo:/ { in_servo=1; next }
  in_servo && /^[^[:space:]]/ { in_servo=0 }
  in_servo && $1 == "pwm_chip:" { print $2; exit }
' "$FACE_TRACKING_CONFIG")"
pwm_path="/sys/class/pwm/pwmchip${pwm_chip}"
if [[ ! "$pwm_chip" =~ ^[0-9]+$ ]] || [[ ! -r "$pwm_path/npwm" ]] ||
   [[ "$(< "$pwm_path/npwm")" -lt 4 ]] || [[ ! -w "$pwm_path/export" ]] ||
   [[ ! -w "$pwm_path/unexport" ]] || ! command -v pinctrl >/dev/null ||
   ! pinctrl get 18 | grep -q 'PWM0_CHAN2' ||
   ! pinctrl get 19 | grep -q 'PWM0_CHAN3'; then
  echo "GPIO18/GPIO19 hardware PWM is unavailable or not writable. Run ./scripts/configure_hardware_pwm.sh and reboot." >&2
  exit 1
fi

export PYTHONNOUSERSITE=1
exec "$bringup" "$camera" "$detector" "$controller" "$servo" .venv/bin/python
