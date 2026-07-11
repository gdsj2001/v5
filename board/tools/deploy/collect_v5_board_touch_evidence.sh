#!/bin/sh
set -eu

home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
evidence_root="${V5_EVIDENCE_ROOT:-$build_root/evidence}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
screenshot_evidence="${V5_UI_SCREENSHOT_EVIDENCE:-}"
remote_events="${V5_REMOTE_TOUCH_EVENTS:-/run/8ax_v5_product_ui/touch_events.jsonl}"
remote_enable="${V5_REMOTE_TOUCH_ENABLE:-/run/8ax_v5_product_ui/enable_touch_diagnostics}"
calibration_path="${V5_TOUCH_CALIBRATION_PATH:-/opt/8ax/safe_ui/re_touch_calibration.json}"
old_calibration_path="${V5_OLD_TOUCH_CALIBRATION_PATH:-/opt/8ax/ui/re_touch_calibration.json}"
local_out="${V5_TOUCH_EVIDENCE_OUT:-$evidence_root/board_touch/v5_board_touch_$(date -u +%Y%m%dT%H%M%SZ).jsonl}"
wait_seconds="${V5_TOUCH_WAIT_SECONDS:-10}"
apply=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --apply) apply=1 ;;
    --screenshot)
      shift
      screenshot_evidence="${1:-}"
      ;;
    --seconds)
      shift
      wait_seconds="${1:-10}"
      ;;
    --help)
      echo "usage: collect_v5_board_touch_evidence.sh [--apply] [--screenshot LOCAL_PNG] [--seconds N]"
      echo "dry-run by default; --apply requires V5_BOARD_SSH and existing screenshot evidence"
      echo "captures real-finger touch diagnostics only; no touch/key/mouse/linuxcncrsh/motion command is sent"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$apply" -eq 0 ]; then
  echo "dry-run board touch evidence collection:"
  echo "  board:       ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  screenshot: ${screenshot_evidence:-<set V5_UI_SCREENSHOT_EVIDENCE or --screenshot>}"
  echo "  remote log: $remote_events"
  echo "  local log:  $local_out"
  echo "  wait:       ${wait_seconds}s for operator real finger touch"
  echo "  command:    V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<png> $0 --apply"
  echo "  input:      no synthetic touch, key, mouse, linuxcncrsh, MDI, or motion command is sent"
  exit 0
fi

case "$wait_seconds" in
  ''|*[!0-9]*) echo "--seconds must be a non-negative integer" >&2; exit 2 ;;
esac

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 3
fi
if [ -z "$screenshot_evidence" ] || [ ! -s "$screenshot_evidence" ]; then
  echo "existing screenshot evidence is required before touch evidence" >&2
  exit 4
fi

ssh_base="ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p $board_ssh_port"
scp_base="scp -q -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -P $board_ssh_port"

if ! $ssh_base "$board_ssh" 'true' >/dev/null 2>&1; then
  echo "cannot connect to board via ssh: $board_ssh port=$board_ssh_port" >&2
  exit 5
fi

$ssh_base "$board_ssh" "/etc/init.d/v5-touch-diagnostics status >/dev/null && CAL='$calibration_path' OLD='$old_calibration_path' EVENTS='$remote_events' ENABLE='$remote_enable' WAIT='$wait_seconds' sh -s" <<'REMOTE_TOUCH'
set -eu
test -r "$CAL"
if command -v grep >/dev/null 2>&1; then
  grep -q 'raw-evdev-cal-v2' "$CAL"
fi
if [ -e "$OLD" ]; then
  echo "retired touch calibration path still exists: $OLD" >&2
  exit 11
fi
mkdir -p "$(dirname "$ENABLE")" "$(dirname "$EVENTS")"
touch "$ENABLE"
before_lines=0
if [ -r "$EVENTS" ]; then
  before_lines=$(wc -l < "$EVENTS" 2>/dev/null || echo 0)
fi
echo "operator action required: touch the visible target with a real finger within ${WAIT}s" >&2
sleep "$WAIT"
after_lines=0
if [ -r "$EVENTS" ]; then
  after_lines=$(wc -l < "$EVENTS" 2>/dev/null || echo 0)
fi
if [ "$after_lines" -le "$before_lines" ]; then
  echo "no new touch diagnostics were recorded" >&2
  exit 12
fi
tail -n 80 "$EVENTS"
REMOTE_TOUCH

mkdir -p "$(dirname "$local_out")"
$ssh_base "$board_ssh" "tail -n 200 '$remote_events'" > "$local_out"
test -s "$local_out"
printf 'collected real-finger touch evidence: %s\n' "$local_out"
printf 'screenshot evidence used: %s\n' "$screenshot_evidence"
printf 'input: no synthetic touch, key, mouse, linuxcncrsh, MDI, or motion command was sent\n'
