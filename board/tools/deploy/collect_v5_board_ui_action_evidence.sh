#!/bin/sh
set -eu

home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
evidence_root="${V5_EVIDENCE_ROOT:-$build_root/evidence}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
remote_touch_events="${V5_REMOTE_TOUCH_EVENTS:-/run/8ax_v5_product_ui/touch_events.jsonl}"
remote_ui_events="${V5_REMOTE_UI_EVENTS:-/run/8ax_v5_product_ui/ui_events.jsonl}"
remote_enable="${V5_REMOTE_TOUCH_ENABLE:-/run/8ax_v5_product_ui/enable_touch_diagnostics}"
calibration_path="${V5_TOUCH_CALIBRATION_PATH:-/opt/8ax/safe_ui/re_touch_calibration.json}"
old_calibration_path="${V5_OLD_TOUCH_CALIBRATION_PATH:-/opt/8ax/ui/re_touch_calibration.json}"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
local_dir="${V5_UI_ACTION_EVIDENCE_DIR:-$evidence_root/board_touch}"
local_touch_out="${V5_UI_ACTION_TOUCH_OUT:-$local_dir/v5_board_ui_action_${stamp}_touch.jsonl}"
local_ui_out="${V5_UI_ACTION_UI_OUT:-$local_dir/v5_board_ui_action_${stamp}_ui.jsonl}"
screenshot="${V5_UI_SCREENSHOT_EVIDENCE:-}"
wait_seconds=30
apply=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --apply) apply=1; shift ;;
    --screenshot) screenshot="$2"; shift 2 ;;
    --seconds) wait_seconds="$2"; shift 2 ;;
    --help)
      echo "usage: collect_v5_board_ui_action_evidence.sh [--apply] [--screenshot LOCAL_PNG] [--seconds N]"
      echo "waits for real-finger LVGL button evidence; no synthetic touch/key/mouse/linuxcncrsh/motion command is sent"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ "$apply" -eq 0 ]; then
  echo "dry-run board UI action evidence collection:"
  echo "  board:      ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  screenshot: ${screenshot:-<set V5_UI_SCREENSHOT_EVIDENCE or --screenshot>}"
  echo "  touch out:  $local_touch_out"
  echo "  ui out:     $local_ui_out"
  echo "  wait:       ${wait_seconds}s for operator real finger button tap"
  echo "  input:      no synthetic touch, key, mouse, linuxcncrsh, MDI, or motion command is sent"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 3
fi
if [ -z "$screenshot" ] || [ ! -s "$screenshot" ]; then
  echo "existing screenshot evidence is required before UI action evidence" >&2
  exit 4
fi

ssh_base="ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p $board_ssh_port"
scp_base="scp -q -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -P $board_ssh_port"
mkdir -p "$local_dir"

$ssh_base "$board_ssh" "TOUCH='$remote_touch_events' UI='$remote_ui_events' ENABLE='$remote_enable' CAL='$calibration_path' OLD='$old_calibration_path' WAIT='$wait_seconds' sh -s" <<'REMOTE_WAIT'
set -eu
/etc/init.d/v5-ui-relay status >/dev/null
/etc/init.d/v5-touch-diagnostics status >/dev/null
test -r "$CAL"
grep -q 'raw-evdev-cal-v2' "$CAL"
if [ -e "$OLD" ]; then
  echo "retired touch calibration path still exists: $OLD" >&2
  exit 10
fi
mkdir -p "$(dirname "$TOUCH")" "$(dirname "$UI")"
touch "$ENABLE" "$TOUCH" "$UI"
before_touch=$(wc -l < "$TOUCH" 2>/dev/null || echo 0)
before_ui=$(wc -l < "$UI" 2>/dev/null || echo 0)
echo "operator action required: tap one visible v5 local button with a real finger within ${WAIT}s" >&2
end=$(( $(date +%s) + WAIT ))
while [ $(date +%s) -lt $end ]; do
  after_touch=$(wc -l < "$TOUCH" 2>/dev/null || echo 0)
  after_ui=$(wc -l < "$UI" 2>/dev/null || echo 0)
  if [ "$after_touch" -gt "$before_touch" ] && [ "$after_ui" -gt "$before_ui" ]; then
    break
  fi
  sleep 1
done
after_touch=$(wc -l < "$TOUCH" 2>/dev/null || echo 0)
after_ui=$(wc -l < "$UI" 2>/dev/null || echo 0)
if [ "$after_touch" -le "$before_touch" ]; then
  echo "no new touch diagnostics were recorded" >&2
  exit 11
fi
if [ "$after_ui" -le "$before_ui" ]; then
  echo "no new LVGL UI action event was recorded" >&2
  exit 12
fi
tail -n $((after_touch-before_touch)) "$TOUCH" >/tmp/v5_ui_action_touch_delta.jsonl
tail -n $((after_ui-before_ui)) "$UI" >/tmp/v5_ui_action_ui_delta.jsonl
printf 'before_touch=%s after_touch=%s before_ui=%s after_ui=%s\n' "$before_touch" "$after_touch" "$before_ui" "$after_ui"
REMOTE_WAIT

$scp_base "$board_ssh:/tmp/v5_ui_action_touch_delta.jsonl" "$local_touch_out"
$scp_base "$board_ssh:/tmp/v5_ui_action_ui_delta.jsonl" "$local_ui_out"
test -s "$local_touch_out"
test -s "$local_ui_out"
printf 'collected real-finger touch delta: %s\n' "$local_touch_out"
printf 'collected real-finger UI action delta: %s\n' "$local_ui_out"
printf 'input: no synthetic touch, key, mouse, linuxcncrsh, MDI, or motion command was sent\n'
