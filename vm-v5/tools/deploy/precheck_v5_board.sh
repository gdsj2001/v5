#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
strict_remote="${V5_PRECHECK_STRICT_REMOTE:-0}"

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
  ssh -o BatchMode=yes -o ConnectTimeout=3 -p "$board_ssh_port" "$board_ssh" "$@"
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
    source_path="$repo_root/$source"
    if [ -e "$source_path" ]; then
      record_ok "source $kind $source -> $destination mode=$mode"
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

  remote_check 'ps w 2>/dev/null | grep -E "v5_state_publisher|v5_lvgl_shell|linuxcncrsh|linuxcncsvr|milltask" | grep -v grep || true' |
    sed 's/^/INFO board process: /'

  if remote_check 'test -x /etc/init.d/v5-state-publisher'; then
    record_ok "board init script installed: /etc/init.d/v5-state-publisher"
  else
    record_warn "v5-state-publisher init script not installed yet"
  fi
  if remote_check 'test -x /etc/init.d/v5-linuxcnc-command-gate'; then
    record_ok "board init script installed: /etc/init.d/v5-linuxcnc-command-gate"
  else
    record_warn "v5-linuxcnc-command-gate init script not installed yet"
  fi
}

check_no_old_source_names() {
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
