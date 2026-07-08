#!/bin/sh
set -eu

DST=/opt/8ax/safe_ui/re_touch_calibration.json
RUN_DIR=/run/v5_touch_calibration
REBOOT_MARKER="$RUN_DIR/reboot.requested"

if [ ! -s "$DST" ]; then
  echo "missing calibration output: $DST" >&2
  exit 1
fi

mkdir -p "$RUN_DIR"
chmod 0644 "$DST" || true
rm -f /opt/8ax/ui/re_touch_calibration.json 2>/dev/null || true

if [ "${1:-}" = "--reboot-board" ]; then
  : > "$REBOOT_MARKER"
  sync
  nohup sh -c 'sleep 1; sync; if command -v reboot >/dev/null 2>&1; then reboot; else /sbin/reboot; fi' \
    >/tmp/v5_touch_calibration_board_reboot.log 2>&1 &
  echo "BOARD_REBOOT_DISPATCHED"
  exit 0
fi

/etc/init.d/v5-ui-relay restart >/tmp/v5_touch_calibration_restart_ui.log 2>&1 || true
/etc/init.d/v5-touch-diagnostics restart >/tmp/v5_touch_calibration_restart_diag.log 2>&1 || true

echo "DIRECT_LAUNCH"
