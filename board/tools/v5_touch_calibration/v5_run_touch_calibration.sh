#!/bin/sh
set -eu

BASE=/opt/8ax/tools/v5_touch_calibration
FB_DEVICE=/dev/fb0
RUN_DIR=/run/v5_touch_calibration
REBOOT_MARKER="$RUN_DIR/reboot.requested"

mkdir -p "$RUN_DIR" /opt/8ax/safe_ui
rm -f "$REBOOT_MARKER"

cleanup() {
  rc=$?
  if [ ! -e "$REBOOT_MARKER" ]; then
    /etc/init.d/v5-ui-relay restart >/tmp/v5_touch_calibration_restore_ui.log 2>&1 || true
    /etc/init.d/v5-touch-diagnostics restart >/tmp/v5_touch_calibration_restore_diag.log 2>&1 || true
  fi
  exit "$rc"
}
trap cleanup EXIT

stop_other_ui_processes() {
  for pattern in \
    '[v]5_lvgl_shell' \
    '[v]5_remote_ui_relay.py' \
    '[v]5_touch_diagnostics' \
    '[v]5_touch_calibration.py'
  do
    pkill -TERM -f "$pattern" 2>/dev/null || true
  done

  i=0
  while ps w 2>/dev/null | awk '
    /[v]5_lvgl_shell/ || /[v]5_remote_ui_relay.py/ ||
    /[v]5_touch_diagnostics/ || /[v]5_touch_calibration.py/ { found=1 }
    END { exit found ? 0 : 1 }
  '; do
    i=$((i + 1))
    [ "$i" -lt 30 ] || break
    sleep 0.1
  done

  for pattern in \
    '[v]5_lvgl_shell' \
    '[v]5_remote_ui_relay.py' \
    '[v]5_touch_diagnostics' \
    '[v]5_touch_calibration.py'
  do
    pkill -KILL -f "$pattern" 2>/dev/null || true
  done
}

/etc/init.d/v5-ui-relay stop >/tmp/v5_touch_calibration_stop_ui.log 2>&1 || true
/etc/init.d/v5-touch-diagnostics stop >/tmp/v5_touch_calibration_stop_diag.log 2>&1 || true
stop_other_ui_processes

export HOME=/root
export USER=root
export LOGNAME=root
export V5_FB_DEVICE="$FB_DEVICE"
export V5_RAW_TOUCH_DEVICE="${V5_RAW_TOUCH_DEVICE:-/dev/input/by-path/z20-touchscreen}"
export V5_POINTERCAL_REQUIRED=0
/usr/bin/python3 "$BASE/v5_touch_calibration.py"
