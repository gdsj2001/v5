#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
manifest="${V5_DEPLOY_MANIFEST:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
board_ssh="${V5_BOARD_SSH:-}"
board_ssh_port="${V5_BOARD_SSH_PORT:-22}"
board_ssh_key="${V5_BOARD_SSH_KEY:-}"
board_ssh_key_opts=""
if [ -n "$board_ssh_key" ]; then
  board_ssh_key_opts="-i $board_ssh_key -o IdentitiesOnly=yes"
fi
board_ssh_user_known_hosts="${V5_BOARD_SSH_USER_KNOWN_HOSTS:-/dev/null}"
board_ssh_strict_host_key="${V5_BOARD_SSH_STRICT_HOST_KEY:-no}"
board_ssh_pubkey_rsa_opt=""
if ssh -G 127.0.0.1 2>/dev/null | grep -qi '^pubkeyacceptedalgorithms '; then
  board_ssh_pubkey_rsa_opt="-o PubkeyAcceptedAlgorithms=+ssh-rsa"
elif ssh -G 127.0.0.1 2>/dev/null | grep -qi '^pubkeyacceptedkeytypes '; then
  board_ssh_pubkey_rsa_opt="-o PubkeyAcceptedKeyTypes=+ssh-rsa"
fi
board_ssh_legacy_rsa_opts="-o HostKeyAlgorithms=+ssh-rsa $board_ssh_pubkey_rsa_opt -o StrictHostKeyChecking=$board_ssh_strict_host_key -o UserKnownHostsFile=$board_ssh_user_known_hosts"
scp_legacy_protocol_opt=""
if ! scp -O 2>&1 | grep -q "unknown option -- O"; then
  scp_legacy_protocol_opt="-O"
fi
remote_root="${V5_REMOTE_STAGING_ROOT:-/tmp/v5_runtime_deploy}"
case "$(uname -s 2>/dev/null || echo unknown)" in
  MINGW*|MSYS*|CYGWIN*) local_python="${V5_LOCAL_PYTHON:-python}" ;;
  *) local_python="${V5_LOCAL_PYTHON:-python3}" ;;
esac
apply=0
refresh_only=0
project_root="$repo_root"
case "$project_root" in
  */board) project_root="${project_root%/board}" ;;
  *\\board) project_root="${project_root%\\board}" ;;
esac
local_backup_dir="${V5_LOCAL_OWNER_BACKUP_DIR:-$project_root/bak}"
temp_dir="$repo_root/repo_ignored/temp"
board_build_dir="${V5_BOARD_BUILD_DIR:-$project_root/build/board}"

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

copy_manifest_source_to_bundle() {
  kind="$1"
  source="$2"
  bundle_root="$3"
  source_path="$(manifest_source_path "$kind" "$source")"
  target_path="$bundle_root/$source"
  install -d "$(dirname "$target_path")"
  cp -p "$source_path" "$target_path"
}

for arg in "$@"; do
  case "$arg" in
    --apply)
      apply=1
      ;;
    --refresh-board-owner-files)
      apply=1
      refresh_only=1
      ;;
    --help)
      echo "usage: push_v5_runtime_to_board.sh [--apply|--refresh-board-owner-files]"
      echo "  --apply refreshes board-owned local files, then deploys to the board"
      echo "  --refresh-board-owner-files only refreshes board-owned local files"
      exit 0
      ;;
    *)
      echo "unknown argument: $arg" >&2
      exit 2
      ;;
  esac
done

if [ "$refresh_only" -eq 0 ] && [ ! -r "$manifest" ]; then
  echo "missing deploy manifest: $manifest" >&2
  exit 2
fi

if [ "$refresh_only" -eq 0 ]; then
  V5_BOARD_BUILD_DIR="$board_build_dir" "$repo_root/tools/deploy/precheck_v5_board.sh" "$manifest"

  tab=$(printf '\t')
  while IFS="$tab" read -r kind source destination mode extra; do
    case "$kind" in
      ''|'#'*) continue ;;
    esac
    if [ -n "${extra:-}" ]; then
      echo "bad manifest row: $kind $source $destination $mode ${extra:-}" >&2
      exit 3
    fi
    source_path="$(manifest_source_path "$kind" "$source")"
    if [ ! -e "$source_path" ]; then
      echo "missing deploy source: $source_path" >&2
      exit 4
    fi
  done < "$manifest"
fi

if [ "$apply" -eq 0 ]; then
  echo "dry-run remote deploy bundle:"
  echo "  board: ${board_ssh:-<set V5_BOARD_SSH>} port=$board_ssh_port"
  echo "  staging: $remote_root"
  echo "  manifest: $manifest"
  echo "  transfer: local tar archive scp staging, then remote install"
  echo "  command: V5_BOARD_SSH=<board> V5_BOARD_SSH_PORT=$board_ssh_port $0 --apply"
  exit 0
fi

if [ -z "$board_ssh" ]; then
  echo "V5_BOARD_SSH is required for --apply" >&2
  exit 5
fi

remote_sh() {
  ssh -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=5 $board_ssh_legacy_rsa_opts $board_ssh_key_opts -p "$board_ssh_port" "$board_ssh" "$@"
}

check_board_connection() {
  if ! remote_sh "true"; then
    echo "cannot connect to board via ssh: $board_ssh port=$board_ssh_port" >&2
    exit 6
  fi
}

safe_backup_name() {
  printf '%s' "$1" | sed 's#[/\\]#_#g'
}

validate_pulled_board_owner_file() {
  source="$1"
  tmp_path="$2"
  case "$source" in
    linuxcnc/ini/v5_bus.ini)
      if [ ! -s "$tmp_path" ] ||
         ! grep -Eq '^[[:space:]]*\[EMC\]' "$tmp_path" ||
         ! grep -Eq '^[[:space:]]*VERSION[[:space:]]*=' "$tmp_path"; then
        printf 'board owner invalid; keep local seed %s reason=missing_emc_version\n' "$source"
        return 1
      fi
      ;;
  esac
  return 0
}

pull_board_owner_file() {
  source="$1"
  remote_path="$2"
  local_path="$repo_root/$source"
  stamp=$(date -u +%Y%m%dT%H%M%S)
  tmp="$temp_dir/$(safe_backup_name "$source").remote.$$"
  if ! remote_sh "test -e '$remote_path'"; then
    printf 'board owner missing; keep local seed %s <- %s\n' "$source" "$remote_path"
    return
  fi
  mkdir -p "$temp_dir" "$local_backup_dir" "$(dirname "$local_path")"
  scp $scp_legacy_protocol_opt -q $board_ssh_legacy_rsa_opts $board_ssh_key_opts -P "$board_ssh_port" "$board_ssh:$remote_path" "$tmp"
  if ! validate_pulled_board_owner_file "$source" "$tmp"; then
    rm -f "$tmp"
    return
  fi
  if [ -e "$local_path" ] && cmp -s "$tmp" "$local_path"; then
    rm -f "$tmp"
    printf 'board owner unchanged %s <- %s\n' "$source" "$remote_path"
    return
  fi
  backup=""
  if [ -e "$local_path" ]; then
    backup="$local_backup_dir/$(safe_backup_name "$source").bak.$stamp"
    if [ -e "$backup" ]; then
      backup="$local_backup_dir/$(safe_backup_name "$source").bak.$stamp.$$"
    fi
    cp -p "$local_path" "$backup"
  fi
  mv "$tmp" "$local_path"
  printf 'refreshed board owner %s <- %s backup=%s\n' "$source" "$remote_path" "${backup:-<new-local-file>}"
}

merge_board_self_parameter_table() {
  source="config/settings/self_parameter_table.tsv"
  remote_path="/opt/8ax/v5/config/settings/self_parameter_table.tsv"
  local_path="$repo_root/$source"
  tmp="$temp_dir/$(safe_backup_name "$source").remote.$$"
  if ! remote_sh "test -e '$remote_path'"; then
    printf 'board self parameter table missing; keep local seed %s <- %s\n' "$source" "$remote_path"
    return
  fi
  if ! command -v "$local_python" >/dev/null 2>&1; then
    echo "$local_python required to merge board self parameter table: $source" >&2
    exit 7
  fi
  mkdir -p "$temp_dir" "$local_backup_dir" "$(dirname "$local_path")"
  scp $scp_legacy_protocol_opt -q $board_ssh_legacy_rsa_opts $board_ssh_key_opts -P "$board_ssh_port" "$board_ssh:$remote_path" "$tmp"
  local_path="$local_path" board_path="$tmp" backup_dir="$local_backup_dir" "$local_python" - <<'PY'
import os
import shutil
import time
from pathlib import Path

local = Path(os.environ["local_path"])
board = Path(os.environ["board_path"])
backup_dir = Path(os.environ["backup_dir"])


def read_text(path, strict):
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        if strict:
            raise
        return ""


def read_rows(path, strict):
    rows = []
    seen = set()
    for line_no, line in enumerate(read_text(path, strict).splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("\t")]
        if len(parts) != 3 or not parts[0] or not parts[1] or not parts[2]:
            if strict:
                raise SystemExit("bad local self parameter table row: %s:%d" % (path, line_no))
            continue
        key = (parts[0], parts[1])
        if strict and key in seen:
            raise SystemExit("duplicate local self parameter key: %s:%d %s/%s" % (path, line_no, key[0], key[1]))
        seen.add(key)
        rows.append((key[0], key[1], parts[2]))
    if strict and not rows:
        raise SystemExit("empty local self parameter table: %s" % path)
    return rows


local_rows = read_rows(local, True)
local_keys = {(axis, field) for axis, field, _ in local_rows}
board_values = {}
for axis, field, value in read_rows(board, False):
    if (axis, field) in local_keys:
        board_values[(axis, field)] = value

lines = ["# schema=v5.settings.parameter_table.tsv.v1"]
for axis, field, default in local_rows:
    lines.append("%s\t%s\t%s" % (axis, field, board_values.get((axis, field), default)))
expected_text = "\n".join(lines) + "\n"
if local.read_text(encoding="utf-8") == expected_text:
    print("board self parameter table unchanged %s rows=%d kept=%d" % (local, len(local_rows), len(board_values)))
    raise SystemExit(0)

stamp = time.strftime("%Y%m%dT%H%M%S")
backup_dir.mkdir(parents=True, exist_ok=True)
backup = backup_dir / ("%s.bak.%s" % (local.name, stamp))
if backup.exists():
    backup = backup_dir / ("%s.bak.%s.%s" % (local.name, stamp, os.getpid()))
shutil.copy2(local, backup)
tmp_local = local.with_name(local.name + ".tmp")
tmp_local.write_text(expected_text, encoding="utf-8")
os.replace(tmp_local, local)
actual_text = local.read_text(encoding="utf-8")
actual_rows = read_rows(local, True)
actual_keys = [(axis, field) for axis, field, _ in actual_rows]
expected_keys = [(axis, field) for axis, field, _ in local_rows]
if actual_text != expected_text or actual_keys != expected_keys:
    shutil.copy2(backup, local)
    raise SystemExit("merged local self parameter table validation failed; restored backup: %s" % backup)
print("merged board self parameter table %s <- %s rows=%d kept=%d backup=%s format=ok" % (local, board, len(local_rows), len(board_values), backup))
PY
  rm -f "$tmp"
}

refresh_board_owner_files() {
  merge_board_self_parameter_table
  pull_board_owner_file "config/settings/drive_parameter_table.tsv" "/opt/8ax/v5/config/settings/drive_parameter_table.tsv"
  pull_board_owner_file "linuxcnc/ini/v5_bus.ini" "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"
  pull_board_owner_file "linuxcnc/runtime/var/linuxcnc.var" "/opt/8ax/v5/linuxcnc/var/linuxcnc.var"
  pull_board_owner_file "linuxcnc/runtime/var/tool.tbl" "/opt/8ax/v5/linuxcnc/var/tool.tbl"
}

if [ "$refresh_only" -eq 0 ]; then
  if ! V5_BOARD_BUILD_DIR="$board_build_dir" V5_BOARD_SSH="$board_ssh" V5_BOARD_SSH_PORT="$board_ssh_port" "$repo_root/tools/deploy/precheck_v5_board.sh" "$manifest"; then
    echo "board precheck failed; deploy not started" >&2
    exit 6
  fi
else
  check_board_connection
fi

refresh_board_owner_files

if [ "$refresh_only" -eq 1 ]; then
  exit 0
fi

archive_dir="$temp_dir"
archive="$archive_dir/v5_runtime_deploy_$$.tar"
bundle_dir="$archive_dir/v5_runtime_deploy_bundle_$$"
mkdir -p "$archive_dir"
rm -f "$archive"
rm -rf "$bundle_dir"
install -d "$bundle_dir/tools/deploy" "$bundle_dir/config/deploy"
cp -p "$repo_root/tools/deploy/install_v5_runtime.sh" "$bundle_dir/tools/deploy/install_v5_runtime.sh"
cp -p "$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv" "$bundle_dir/config/deploy/v5_runtime_deploy_manifest.tsv"
tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  copy_manifest_source_to_bundle "$kind" "$source" "$bundle_dir"
done < "$manifest"
(cd "$bundle_dir" && tar -cf "$archive" .)
rm -rf "$bundle_dir"
remote_sh "rm -rf '$remote_root' && mkdir -p '$remote_root'"
scp $scp_legacy_protocol_opt -q $board_ssh_legacy_rsa_opts $board_ssh_key_opts -P "$board_ssh_port" "$archive" "$board_ssh:$remote_root/v5_runtime_deploy.tar"
rm -f "$archive"
remote_sh "tar -m -C '$remote_root' -xf '$remote_root/v5_runtime_deploy.tar' && rm -f '$remote_root/v5_runtime_deploy.tar'"
remote_sh "V5_REPO_ROOT='$remote_root' '$remote_root/tools/deploy/install_v5_runtime.sh' --apply"
