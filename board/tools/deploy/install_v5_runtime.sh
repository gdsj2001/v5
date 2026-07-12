#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
apply=0
project_root="$repo_root"
case "$project_root" in
  */board) project_root="${project_root%/board}" ;;
  *\\board) project_root="${project_root%\\board}" ;;
esac
parameter_table_backup_dir="${V5_PARAMETER_TABLE_BACKUP_DIR:-$project_root/bak}"

if [ "${2:-}" = "--apply" ]; then
  apply=1
fi
if [ "${1:-}" = "--apply" ]; then
  manifest="$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv"
  apply=1
fi

if [ ! -r "$manifest" ]; then
  echo "missing deploy manifest: $manifest" >&2
  exit 2
fi

merge_runtime_seed_tsv() {
  source_path="$1"
  destination="$2"
  mode="$3"
  if [ ! -e "$destination" ]; then
    install -d "$(dirname "$destination")"
    install -m "$mode" "$source_path" "$destination"
    return
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 required to merge runtime seed table: $destination" >&2
    exit 5
  fi
  install -d "$(dirname "$destination")"
  source_path="$source_path" destination="$destination" mode="$mode" parameter_table_backup_dir="$parameter_table_backup_dir" python3 - <<'PY'
import os
import shutil
import time
from pathlib import Path

src = Path(os.environ["source_path"])
dst = Path(os.environ["destination"])
mode = int(os.environ["mode"], 8)
backup_dir = Path(os.environ["parameter_table_backup_dir"])


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
                raise SystemExit("bad local parameter table row: %s:%d" % (path, line_no))
            continue
        key = (parts[0], parts[1])
        if strict and key in seen:
            raise SystemExit("duplicate local parameter key: %s:%d %s/%s" % (path, line_no, key[0], key[1]))
        seen.add(key)
        rows.append((key[0], key[1], parts[2]))
    if strict and not rows:
        raise SystemExit("empty local parameter table: %s" % path)
    return rows


local_rows = read_rows(src, True)
local_keys = {(axis, field) for axis, field, _ in local_rows}
board_values = {}
for axis, field, value in read_rows(dst, False):
    if (axis, field) in local_keys:
        board_values[(axis, field)] = value

lines = ["# schema=v5.settings.parameter_table.tsv.v1"]
for axis, field, default in local_rows:
    lines.append("%s\t%s\t%s" % (axis, field, board_values.get((axis, field), default)))
expected_text = "\n".join(lines) + "\n"

tmp = dst.with_name(dst.name + ".tmp")
stamp = time.strftime("%Y%m%dT%H%M%S")
backup_dir.mkdir(parents=True, exist_ok=True)
backup = backup_dir / ("%s.bak.%s" % (dst.name, stamp))
if backup.exists():
    backup = backup_dir / ("%s.bak.%s.%s" % (dst.name, stamp, os.getpid()))
shutil.copy2(dst, backup)
tmp.write_text(expected_text, encoding="utf-8")
os.chmod(tmp, mode)
os.replace(tmp, dst)
actual_text = dst.read_text(encoding="utf-8")
actual_rows = read_rows(dst, True)
actual_keys = [(axis, field) for axis, field, _ in actual_rows]
expected_keys = [(axis, field) for axis, field, _ in local_rows]
if actual_text != expected_text or actual_keys != expected_keys:
    shutil.copy2(backup, dst)
    raise SystemExit("merged parameter table validation failed; restored backup: %s" % backup)
print("merged runtime seed table %s -> %s rows=%d kept=%d backup=%s format=ok" % (src, dst, len(local_rows), len(board_values), backup))
PY
}

tab=$(printf '\t')
while IFS="$tab" read -r kind source destination mode extra; do
  case "$kind" in
    ''|'#'*) continue ;;
  esac
  if [ -n "${extra:-}" ] || [ -z "${source:-}" ] || [ -z "${destination:-}" ] || [ -z "${mode:-}" ]; then
    echo "bad manifest row: $kind $source $destination $mode ${extra:-}" >&2
    exit 3
  fi
  source_path="$repo_root/$source"
  if [ ! -e "$source_path" ]; then
    echo "missing deploy source: $source_path" >&2
    exit 4
  fi
  if [ "$kind" = "runtime_seed_merge" ]; then
    case "$source:$destination:$mode" in
      "config/settings/self_parameter_table.tsv:/opt/8ax/v5/config/settings/self_parameter_table.tsv:0644") ;;
      *)
        echo "runtime_seed_merge is only allowed for self_parameter_table.tsv: $source -> $destination mode=$mode" >&2
        exit 6
        ;;
    esac
  fi
  if [ "$apply" -eq 0 ]; then
    printf 'deploy %s %s -> %s mode=%s\n' "$kind" "$source_path" "$destination" "$mode"
    continue
  fi
  if [ "$kind" = "runtime_seed_merge" ]; then
    merge_runtime_seed_tsv "$source_path" "$destination" "$mode"
    continue
  fi
  if [ "$kind" = "runtime_seed" ] && [ -e "$destination" ]; then
    printf 'preserve runtime seed %s -> %s (exists)\n' "$source" "$destination"
    continue
  fi
  install -d "$(dirname "$destination")"
  install -m "$mode" "$source_path" "$destination"
done < "$manifest"

enable_boot_service() {
  name="$1"
  start_prio="$2"
  stop_prio="$3"
  init_target="../init.d/$name"
  for level in 2 3 4 5; do
    dir="/etc/rc${level}.d"
    [ -d "$dir" ] || continue
    rm -f "$dir"/S??"$name"
    ln -sf "$init_target" "$dir/S${start_prio}${name}"
  done
  for level in 0 1 6; do
    dir="/etc/rc${level}.d"
    [ -d "$dir" ] || continue
    rm -f "$dir"/K??"$name"
    ln -sf "$init_target" "$dir/K${stop_prio}${name}"
  done
}

enable_boot_services() {
  enable_boot_service v5-linuxcnc-command-gate 91 19
  enable_boot_service v5-g53-geometry-memory-owner 92 18
  enable_boot_service v5-rtcp-status-publisher 93 17
  enable_boot_service v5-wcs-status-publisher 94 16
  enable_boot_service v5-state-publisher 95 15
  enable_boot_service v5-ui-relay 96 14
  enable_boot_service v5-settings-actiond 97 13
  enable_boot_service v5-touch-diagnostics 98 12
}

cleanup_retired_runtime_files() {
  rm -f /run/8ax_v5_drive/settings_self_parameter_table.json
  rm -f /opt/8ax/v5/config/settings/microkernel_parameter_table.tsv
  rm -f /opt/8ax/v5/gcode/golden/cc.ngc
  rm -f /tmp/v5_golden/cc.ngc
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_calibration.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_restart.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_runtime.py
}

install_runtime_drive_profiles() {
  [ -x /usr/bin/python3 ] || return 0
  python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path

items = []
public = Path('/opt/8ax/v5/config/drive-profiles/public/driver_profile_map.json')
if public.is_file():
    items.append((public, Path('/opt/8ax/drive-profiles/public/driver_profile_map.json')))
private_dir = Path('/opt/8ax/v5/config/drive-profiles/private')
if private_dir.is_dir():
    private_maps = sorted(private_dir.glob('*driver_profile_map.json'))
    if private_maps:
        items.append((private_maps[0], Path('/opt/8ax/drive-profiles/private/driver_profile_map.json')))
for src, dst in items:
    raw = src.read_bytes()
    payload = json.loads(raw.decode('utf-8'))
    schema = payload.get('schema') or payload.get('schema_version')
    profiles = payload.get('profiles') if isinstance(payload, dict) else None
    if schema != 'v5-driver-profile-map-v1' or not isinstance(profiles, list) or not profiles:
        raise SystemExit('invalid v5 drive profile map: %s schema=%s' % (src, schema))
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + '.tmp')
    tmp.write_bytes(raw)
    os.replace(tmp, dst)
    sha = hashlib.sha256(raw).hexdigest().upper()
    side = dst.with_name(dst.name + '.sha256')
    stmp = side.with_name(side.name + '.tmp')
    stmp.write_text('%s  %s\n' % (sha, dst.name), encoding='ascii')
    os.replace(stmp, side)
    print('OK runtime drive profile %s -> %s profiles=%d sha256=%s' % (src, dst, len(profiles), sha))
PY
  if [ -x /usr/libexec/8ax/drive_profile/v5_drive_profile_resident_snapshot.py ]; then
    /usr/libexec/8ax/drive_profile/v5_drive_profile_resident_snapshot.py --profile-root /opt/8ax/drive-profiles --out /run/8ax_v5_drive/drive_profile_resident_snapshot.json
  fi
}

if [ "$apply" -eq 1 ]; then
  enable_boot_services
  cleanup_retired_runtime_files
  install_runtime_drive_profiles
  /etc/init.d/v5-linuxcnc-command-gate restart
  /etc/init.d/v5-g53-geometry-memory-owner restart
  /etc/init.d/v5-rtcp-status-publisher restart
  /etc/init.d/v5-wcs-status-publisher restart
  /etc/init.d/v5-state-publisher restart
  /etc/init.d/v5-ui-relay restart
  /etc/init.d/v5-settings-actiond restart
  /etc/init.d/v5-touch-diagnostics restart
else
  echo "dry-run only; pass --apply to install files and restart v5 init services"
fi
