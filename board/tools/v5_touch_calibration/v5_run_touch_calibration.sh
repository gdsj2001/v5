#!/bin/sh
set -eu

BASE=/opt/8ax/tools/v5_touch_calibration
FB_DEVICE=/dev/fb0
SCREEN_SIZE=1024x600
RUN_DIR=/run/v5_touch_calibration
REBOOT_MARKER="$RUN_DIR/reboot.requested"

mkdir -p "$RUN_DIR" /tmp/runtime-v5-touch-calibration /opt/8ax/safe_ui
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
export XDG_RUNTIME_DIR=/tmp/runtime-v5-touch-calibration
export QT_QPA_PLATFORM="linuxfb:fb=${FB_DEVICE}:size=${SCREEN_SIZE}"
export LINUXCNC_OPENGL_PLATFORM=linuxfb
export QT_QPA_EGLFS_HIDECURSOR=1
export QT_LOGGING_RULES="qt.qpa.*=false"
export QT_AUTO_SCREEN_SCALE_FACTOR=0
export QT_ENABLE_HIGHDPI_SCALING=0
export QT_SCALE_FACTOR=1
export QT_SCREEN_SCALE_FACTORS=1
export QT_FONT_DPI=96
export V5_RAW_TOUCH_DEVICE="${V5_RAW_TOUCH_DEVICE:-/dev/input/by-path/z20-touchscreen}"
export V5_ENABLE_LINUXCNC=1
export V5_TOUCH_CAL_EXIT_TO_LINUXCNC=1
export V5_TOUCH_RESTART_HELPER="$BASE/v5_restart_after_touch_calibration.sh"
export V5_APPLY_TOUCH_CALIBRATION_HELPER="$BASE/v5_apply_touch_calibration.py"
export V5_POINTERCAL_REQUIRED=0

unset DISPLAY
unset TSLIB_TSDEVICE
unset TSLIB_FBDEVICE
unset TSLIB_CALIBFILE
unset TSLIB_CONFFILE
unset QT_QPA_FB_TSLIB
unset QT_QPA_GENERIC_PLUGINS
unset QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS

chmod 700 "$XDG_RUNTIME_DIR" || true
/usr/bin/python3 "$BASE/v5_touch_calibration.py"
