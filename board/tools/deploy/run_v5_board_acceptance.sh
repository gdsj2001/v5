#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
board_build_dir="${V5_BOARD_BUILD_DIR:-$build_root/board}"
board_build_targets="${V5_BOARD_BUILD_TARGETS:-v5_lvgl_shell v5_state_publisher v5_position_status_publisher v5_touch_diagnostics v5_linuxcncrsh_probe v5_command_gate_server v5_command_gate_drive_window v5_linuxcncrsh_golden_run}"
product_closure_verify="$repo_root/tools/deploy/verify_v5_product_source_closure.py"
apply=0
ui_first_frame=0

for arg in "$@"; do
  case "$arg" in
    --apply) apply=1 ;;
    --ui-first-frame) ui_first_frame=1 ;;
    --help)
      echo "usage: run_v5_board_acceptance.sh [--apply] [--ui-first-frame]"
      echo "dry-run by default; --apply requires V5_BOARD_SSH; --ui-first-frame runs 10 UI/cache cycles; motion must use the documented live UI/operator path"
      exit 0
      ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
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
  echo "  motion:   not executed here; use the AI live browser and the owner-documented operator sequence"
  echo "  command:  V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  echo "  touch:    V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<captured-png> $repo_root/tools/deploy/collect_v5_board_touch_evidence.sh --apply"
  echo "  ui-action: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port V5_UI_SCREENSHOT_EVIDENCE=<captured-png> $repo_root/tools/deploy/collect_v5_board_ui_action_evidence.sh --apply"
  echo "  first-frame: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply --ui-first-frame"
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
cmake_cache="$board_build_dir/CMakeCache.txt"
if [ ! -r "$cmake_cache" ]; then
  echo "missing canonical board CMake cache: $cmake_cache" >&2
  exit 6
fi
cmake_c_compiler=$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p' "$cmake_cache" | head -n 1)
if [ -z "$cmake_c_compiler" ]; then
  cmake_compiler_file=$(find "$board_build_dir/CMakeFiles" -mindepth 2 -maxdepth 2 \
    -name CMakeCCompiler.cmake -type f -print -quit)
  if [ -n "$cmake_compiler_file" ]; then
    cmake_c_compiler=$(sed -n 's/^set(CMAKE_C_COMPILER "\(.*\)")$/\1/p' \
      "$cmake_compiler_file" | head -n 1)
  fi
fi
case "$cmake_c_compiler" in
  *arm-xilinx-linux-gnueabi-gcc) ;;
  *)
    echo "canonical board CMake cache is not ARM: compiler=${cmake_c_compiler:-missing}" >&2
    exit 6
    ;;
esac

python3 "$product_closure_verify" \
  --board-root "$repo_root" \
  --build-dir "$board_build_dir" \
  --prepare-cmake-query
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

echo "acceptance source/deploy/verify/ui-capture complete; collect live UI/operator, native motion and final ESTOP evidence separately"
