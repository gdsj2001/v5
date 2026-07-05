#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
golden_local="${V5_GOLDEN_PROGRAM:-$repo_root/gcode/golden/cc.ngc}"
golden_remote="${V5_REMOTE_GOLDEN_PROGRAM:-/tmp/v5_golden/cc.ngc}"
linuxcncrsh_port="${V5_LINUXCNCRSH_PORT:-5007}"
apply=0
motion=0

for arg in "$@"; do
  case "$arg" in
    --apply) apply=1 ;;
    --motion) motion=1 ;;
    --help)
      echo "usage: run_v5_board_acceptance.sh [--apply] [--motion]"
      echo "dry-run by default; --apply requires V5_BOARD_SSH; --motion also requires V5_ALLOW_MOTION=1"
      exit 0
      ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [ ! -r "$golden_local" ]; then
  echo "missing golden program: $golden_local" >&2
  exit 3
fi

if [ "$apply" -eq 0 ]; then
  echo "dry-run board acceptance:"
  echo "  precheck: $repo_root/tools/deploy/precheck_v5_board.sh"
  echo "  deploy:   $repo_root/tools/deploy/push_v5_runtime_to_board.sh --apply"
  echo "  verify:   $repo_root/tools/deploy/verify_v5_board_runtime.sh"
  echo "  capture:  $repo_root/tools/deploy/capture_v5_board_ui.sh --apply"
  echo "  touch:    $repo_root/tools/deploy/collect_v5_board_touch_evidence.sh --apply --screenshot <captured-png>"
  echo "  golden:   $golden_local -> ${board_ssh:-<set V5_BOARD_SSH>}:$golden_remote port=$board_ssh_port"
  echo "  command:  V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  echo "  touch:    V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<captured-png> $repo_root/tools/deploy/collect_v5_board_touch_evidence.sh --apply"
  echo "  motion:   V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_ALLOW_MOTION=1 $0 --apply --motion"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 4
fi

V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/precheck_v5_board.sh"
V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/push_v5_runtime_to_board.sh" --apply
V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/verify_v5_board_runtime.sh"
V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/capture_v5_board_ui.sh" --apply

if [ "$motion" -eq 0 ]; then
  echo "acceptance source/deploy/verify/ui-capture complete; collect real-finger touch evidence before touch claims; golden motion not requested"
  exit 0
fi

if [ "${V5_ALLOW_MOTION:-0}" != "1" ]; then
  echo "V5_ALLOW_MOTION=1 is required with --motion" >&2
  exit 5
fi

ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "mkdir -p '$(dirname "$golden_remote")'"
scp -q -P "$board_ssh_port" "$golden_local" "$board_ssh:$golden_remote"
ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "test -s '$golden_remote'"
ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "/usr/libexec/8ax/v5_linuxcncrsh_probe --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000"
ssh -o BatchMode=yes -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "V5_ALLOW_MOTION=1 /usr/libexec/8ax/v5_linuxcncrsh_golden_run --program '$golden_remote' --start --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000"
echo "golden program opened and start submitted: $golden_remote"
