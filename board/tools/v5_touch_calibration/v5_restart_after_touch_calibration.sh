#!/bin/sh
set -eu

DST=/opt/8ax/safe_ui/re_touch_calibration.json

if [ ! -s "$DST" ]; then
  echo "missing calibration output: $DST" >&2
  exit 1
fi

chmod 0644 "$DST" || true
rm -f /opt/8ax/ui/re_touch_calibration.json 2>/dev/null || true
/etc/init.d/v5-ui-relay restart >/tmp/v5_touch_calibration_restart_ui.log 2>&1 || true
/etc/init.d/v5-touch-diagnostics restart >/tmp/v5_touch_calibration_restart_diag.log 2>&1 || true

echo "DIRECT_LAUNCH"
