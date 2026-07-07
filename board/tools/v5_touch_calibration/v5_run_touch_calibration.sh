#!/bin/sh
set -eu

BASE=/opt/8ax/tools/v5_touch_calibration
FB_DEVICE=/dev/fb0
SCREEN_SIZE=1024x600
RUN_DIR=/run/v5_touch_calibration

mkdir -p "$RUN_DIR" /tmp/runtime-v5-touch-calibration /opt/8ax/safe_ui

/etc/init.d/v5-ui-relay stop >/tmp/v5_touch_calibration_stop_ui.log 2>&1 || true
/etc/init.d/v5-touch-diagnostics stop >/tmp/v5_touch_calibration_stop_diag.log 2>&1 || true
pkill -f '[v]5_touch_diagnostics' 2>/dev/null || true
pkill -f '[v]5_touch_calibration.py' 2>/dev/null || true

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
exec /usr/bin/python3 "$BASE/v5_touch_calibration.py"
