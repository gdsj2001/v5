#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
strict_remote="${V5_PRECHECK_STRICT_REMOTE:-0}"
precheck_scope="${V5_PRECHECK_SCOPE:-full}"
home_dir="${HOME:?HOME is required}"
build_root="${V5_BUILD_ROOT:-$home_dir/v5-build}"
board_build_dir="${V5_BOARD_BUILD_DIR:-$build_root/board}"

if [ ! -r "$manifest" ]; then
  echo "FAIL missing deploy manifest: $manifest" >&2
  exit 2
fi

fail=0
warn=0
say() { printf '%s\n' "$*"; }
record_fail() { say "FAIL $*"; fail=1; }
record_warn() { say "WARN $*"; warn=1; }
record_ok() { say "OK $*"; }

remote_check() {
  if [ -z "$board_ssh" ]; then
    return 2
  fi
  ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=3 -p "$board_ssh_port" "$board_ssh" "$@"
}

manifest_source_path() {
  kind="$1"
  source="$2"
  case "$kind:$source" in
    binary:build/board/app/*)
      printf '%s/app/%s' "$board_build_dir" "${source#build/board/app/}"
      ;;
    *)
      printf '%s/%s' "$repo_root" "$source"
      ;;
  esac
}

check_source_manifest() {
  tab=$(printf '\t')
  while IFS="$tab" read -r kind source destination mode extra; do
    case "$kind" in
      ''|'#'*) continue ;;
    esac
    if [ -n "${extra:-}" ] || [ -z "${source:-}" ] || [ -z "${destination:-}" ] || [ -z "${mode:-}" ]; then
      record_fail "bad manifest row: $kind $source $destination $mode ${extra:-}"
      continue
    fi
    source_path="$(manifest_source_path "$kind" "$source")"
    if [ -e "$source_path" ]; then
      if [ "$kind" = "binary" ]; then
        if ! file "$source_path" | grep -q 'ELF 32-bit.*ARM'; then
          record_fail "non-ARM deploy binary: $source_path"
          continue
        fi
        if ! readelf -h "$source_path" | grep -q 'hard-float ABI'; then
          record_fail "deploy binary is not ARM hard-float ABI: $source_path"
          continue
        fi
      fi
      record_ok "source $kind $source -> $destination mode=$mode path=$source_path"
    else
      record_fail "missing source $source_path"
    fi
  done < "$manifest"
}

check_remote_target() {
  if [ -z "$board_ssh" ]; then
    record_warn "V5_BOARD_SSH not set; remote board checks skipped"
    return 0
  fi

  if ! remote_check 'true' >/dev/null 2>&1; then
    record_fail "cannot connect to board via ssh: $board_ssh port=$board_ssh_port"
    return 0
  fi
  record_ok "ssh board reachable: $board_ssh port=$board_ssh_port"

  for cmd in install sh test setsid kill; do
    if remote_check "command -v $cmd >/dev/null 2>&1"; then
      record_ok "board command available: $cmd"
    else
      record_fail "board command missing: $cmd"
    fi
  done

  for dir in /usr/libexec /etc/init.d /opt /dev/shm /run; do
    if remote_check "test -d '$dir'"; then
      record_ok "board directory exists: $dir"
    else
      record_fail "board directory exists: $dir"
    fi
  done

  remote_check 'ps w 2>/dev/null | grep -E "v5_state_publisher|v5_native_hal_owner|v5_wcs_status_publisher|v5_lvgl_shell|v5_remote_ui_relay|linuxcncrsh|linuxcncsvr|milltask" | grep -v grep || true' |
    sed 's/^/INFO board process: /'

  if remote_check 'test -x /etc/init.d/v5-state-publisher'; then
    record_ok "board init script installed: /etc/init.d/v5-state-publisher"
  else
    record_warn "v5-state-publisher init script not installed yet"
  fi
  if remote_check 'test ! -e /etc/init.d/v5-rtcp-status-publisher'; then
    record_ok "retired init script absent: /etc/init.d/v5-rtcp-status-publisher"
  else
    record_ok "retired init script cleanup pending in this deploy: /etc/init.d/v5-rtcp-status-publisher"
  fi
  if remote_check 'test ! -e /etc/init.d/v5-g53-geometry-memory-owner'; then
    record_ok "retired init script absent: /etc/init.d/v5-g53-geometry-memory-owner"
  else
    record_ok "retired init script cleanup pending in this deploy: /etc/init.d/v5-g53-geometry-memory-owner"
  fi
  if remote_check 'test -x /usr/bin/v5_native_hal_owner && test -r /usr/lib/linuxcnc/modules/v5_safety_latch.so'; then
    record_ok "native HAL owner and realtime safety component installed"
  else
    record_warn "native HAL owner or realtime safety component not installed yet"
  fi
  if remote_check 'test -x /etc/init.d/v5-wcs-status-publisher'; then
    record_ok "board init script installed: /etc/init.d/v5-wcs-status-publisher"
  else
    record_warn "v5-wcs-status-publisher init script not installed yet"
  fi
  if remote_check 'test -x /etc/init.d/v5-linuxcnc-command-gate'; then
    record_ok "board init script installed: /etc/init.d/v5-linuxcnc-command-gate"
  else
    record_warn "v5-linuxcnc-command-gate init script not installed yet"
  fi
  if remote_check 'test -x /etc/init.d/v5-ui-relay'; then
    record_ok "board init script installed: /etc/init.d/v5-ui-relay"
  else
    record_warn "v5-ui-relay init script not installed yet"
  fi
  if remote_check 'test -x /etc/init.d/v5-touch-diagnostics'; then
    record_ok "board init script installed: /etc/init.d/v5-touch-diagnostics"
  else
    record_warn "v5-touch-diagnostics init script not installed yet"
  fi
}

check_no_old_source_names() {
  if [ "$precheck_scope" = "manifest" ]; then
    record_ok "focused manifest precheck skips unrelated retired-name full-tree scan"
    return
  fi
  old_major=3
  old_pulse="v${old_major}_pulse"
  old_icon_base="v5_reb""_icons"
  old_icon_c="${old_icon_base}.c"
  old_icon_h="${old_icon_base}.h"
  if find "$repo_root" -path "$repo_root/build" -prune -o \( -name "${old_pulse}.ini" -o -name "${old_pulse}.hal" -o -name "$old_icon_c" -o -name "$old_icon_h" \) -print | grep .; then
    record_fail "retired pulse or unused icon file still present"
  else
    record_ok "retired pulse and unused icon files absent"
  fi
}

case "$precheck_scope" in
  full|manifest) ;;
  *)
    echo "unsupported V5_PRECHECK_SCOPE: $precheck_scope (expected full or manifest)" >&2
    exit 2
    ;;
esac

check_source_manifest
check_no_old_source_names
check_remote_target

if [ "$fail" -ne 0 ]; then
  exit 1
fi
if [ "$warn" -ne 0 ] && [ "$strict_remote" = "1" ]; then
  exit 1
fi
say "precheck complete"
