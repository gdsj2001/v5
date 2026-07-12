#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
golden_ac_local="$repo_root/gcode/golden/cc-ac.ngc"
golden_bc_local="$repo_root/gcode/golden/cc-bc.ngc"
golden_remote_dir="/tmp/v5_golden"
golden_local=""
golden_remote=""
linuxcncrsh_port="${V5_LINUXCNCRSH_PORT:-5007}"
home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
board_build_dir="${V5_BOARD_BUILD_DIR:-$build_root/board}"
board_build_targets="${V5_BOARD_BUILD_TARGETS:-v5_product_runtime}"
product_closure_verify="$repo_root/tools/deploy/verify_v5_product_source_closure.py"
apply=0
motion=0
ui_first_frame=0

for arg in "$@"; do
  case "$arg" in
    --apply) apply=1 ;;
    --motion) motion=1 ;;
    --ui-first-frame) ui_first_frame=1 ;;
    --help)
      echo "usage: run_v5_board_acceptance.sh [--apply] [--ui-first-frame] [--motion]"
      echo "dry-run by default; --apply requires V5_BOARD_SSH; --ui-first-frame runs 10 UI/cache cycles; --motion also requires V5_ALLOW_MOTION=1"
      exit 0
      ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

for golden_source in "$golden_ac_local" "$golden_bc_local"; do
  if [ ! -r "$golden_source" ]; then
    echo "missing golden program: $golden_source" >&2
    exit 3
  fi
done

if [ "$apply" -eq 0 ]; then
  echo "dry-run board acceptance:"
  echo "  closure: python3 $product_closure_verify --board-root $repo_root --build-dir $board_build_dir --validate-shell"
  echo "  build:    cmake --build $board_build_dir --target $board_build_targets"
  echo "  precheck: $repo_root/tools/deploy/precheck_v5_board.sh"
  echo "  deploy:   $repo_root/tools/deploy/push_v5_runtime_to_board.sh --apply"
  echo "  verify:   $repo_root/tools/deploy/verify_v5_board_runtime.sh"
  echo "  capture:  $repo_root/tools/deploy/capture_v5_board_ui.sh --apply"
  echo "  touch:    $repo_root/tools/deploy/collect_v5_board_touch_evidence.sh --apply --screenshot <captured-png>"
  echo "  ui-action: $repo_root/tools/deploy/collect_v5_board_ui_action_evidence.sh --apply --screenshot <captured-png>"
  echo "  first-frame: $repo_root/tools/deploy/verify_v5_ui_first_frame_acceptance.py --apply --cycles 10"
  echo "  golden:   native XYZAC_TRT -> $golden_ac_local; native XYZBC_TRT -> $golden_bc_local"
  echo "  remote:   ${board_ssh:-<set V5_BOARD_SSH>}:$golden_remote_dir/<model-matched-program> port=$board_ssh_port"
  echo "  command:  V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  echo "  touch:    V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<captured-png> $repo_root/tools/deploy/collect_v5_board_touch_evidence.sh --apply"
  echo "  ui-action: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<captured-png> $repo_root/tools/deploy/collect_v5_board_ui_action_evidence.sh --apply"
  echo "  first-frame: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply --ui-first-frame"
  echo "  motion:   V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_ALLOW_MOTION=1 $0 --apply --motion"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 4
fi

if [ ! -d "$board_build_dir" ]; then
  echo "missing board build directory: $board_build_dir" >&2
  exit 6
fi

python3 "$product_closure_verify" \
  --board-root "$repo_root" \
  --build-dir "$board_build_dir" \
  --prepare-cmake-query
cmake -S "$repo_root" -B "$board_build_dir"
python3 "$product_closure_verify" \
  --board-root "$repo_root" \
  --build-dir "$board_build_dir" \
  --validate-shell
cmake --build "$board_build_dir" --target $board_build_targets
V5_REPO_ROOT="$repo_root" V5_BOARD_BUILD_DIR="$board_build_dir" V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/precheck_v5_board.sh"
V5_REPO_ROOT="$repo_root" V5_BOARD_BUILD_DIR="$board_build_dir" V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/push_v5_runtime_to_board.sh" --apply
V5_REPO_ROOT="$repo_root" V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/verify_v5_board_runtime.sh"
V5_REPO_ROOT="$repo_root" V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/capture_v5_board_ui.sh" --apply

if [ "$ui_first_frame" -eq 1 ]; then
  V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" \
    python3 "$repo_root/tools/deploy/verify_v5_ui_first_frame_acceptance.py" --apply --cycles 10
fi

if [ "$motion" -eq 0 ]; then
  echo "acceptance source/deploy/verify/ui-capture complete; collect real-finger touch and UI action evidence before operator-path claims; golden motion not requested"
  exit 0
fi

if [ "${V5_ALLOW_MOTION:-0}" != "1" ]; then
  echo "V5_ALLOW_MOTION=1 is required with --motion" >&2
  exit 5
fi

active_model=$(ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "/usr/libexec/8ax/v5_linuxcncrsh_golden_run --print-active-model")
case "$active_model" in
  XYZAC_TRT)
    golden_local="$golden_ac_local"
    golden_remote="$golden_remote_dir/cc-ac.ngc"
    ;;
  XYZBC_TRT)
    golden_local="$golden_bc_local"
    golden_remote="$golden_remote_dir/cc-bc.ngc"
    ;;
  *)
    echo "unsupported or unavailable native active model: $active_model" >&2
    exit 7
    ;;
esac
echo "native active model selected golden program: model=$active_model source=$golden_local"

ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "mkdir -p '$golden_remote_dir' && rm -f '$golden_remote_dir/cc.ngc'"
scp -q -o LogLevel=ERROR -P "$board_ssh_port" "$golden_local" "$board_ssh:$golden_remote"
ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "test -s '$golden_remote'"
golden_local_hash=$(sha256sum "$golden_local" | cut -d ' ' -f 1)
golden_remote_hash=$(ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "sha256sum '$golden_remote'" | cut -d ' ' -f 1)
if [ "$golden_local_hash" != "$golden_remote_hash" ]; then
  echo "golden program hash mismatch: local=$golden_local_hash remote=$golden_remote_hash" >&2
  exit 8
fi
echo "golden program hash confirmed: $golden_local_hash"
ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "/usr/libexec/8ax/v5_linuxcncrsh_probe --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000"
ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 -p "$board_ssh_port" "$board_ssh" "V5_ALLOW_MOTION=1 /usr/libexec/8ax/v5_linuxcncrsh_golden_run --program '$golden_remote' --start --ini /opt/8ax/v5/linuxcnc/ini/v5_bus.ini --host 127.0.0.1 --port '$linuxcncrsh_port' --password EMC --timeout-ms 1000"
echo "golden program opened and start submitted: $golden_remote"
