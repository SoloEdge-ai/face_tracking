#!/usr/bin/env bash
set -euo pipefail

boot_config="${FACE_TRACKING_BOOT_CONFIG:-/boot/firmware/config.txt}"
overlay="dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2"

if [[ ! -f "$boot_config" ]]; then
  echo "Raspberry Pi boot configuration not found: $boot_config" >&2
  exit 1
fi

if ! grep -Fqx "$overlay" "$boot_config"; then
  last_section="$(awk '/^\[[^]]+\]$/ { section=$0 } END { print section }' "$boot_config")"
  if [[ "$last_section" == "[all]" ]]; then
    printf '\n%s\n' "$overlay" | sudo tee -a "$boot_config" >/dev/null
  else
    printf '\n[all]\n%s\n' "$overlay" | sudo tee -a "$boot_config" >/dev/null
  fi
  echo "Enabled two-channel hardware PWM in $boot_config. Reboot before starting the system."
else
  echo "Two-channel hardware PWM boot overlay is already configured."
fi

if [[ -r /sys/class/pwm/pwmchip0/npwm ]] &&
   [[ "$(< /sys/class/pwm/pwmchip0/npwm)" -ge 4 ]] &&
   command -v pinctrl >/dev/null &&
   pinctrl get 18 | grep -q 'PWM0_CHAN2' &&
   pinctrl get 19 | grep -q 'PWM0_CHAN3'; then
  echo "Hardware PWM is active: GPIO18/PWM channel 2 and GPIO19/PWM channel 3."
else
  echo "Hardware PWM is not active in this boot; reboot is required."
fi
