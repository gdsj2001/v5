#!/bin/sh
set -eu

repo_root="${V5_REPO_ROOT:-/root/Desktop/v5}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
apply=0

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
  if [ "$apply" -eq 0 ]; then
    printf 'deploy %s %s -> %s mode=%s\n' "$kind" "$source_path" "$destination" "$mode"
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
  enable_boot_service v5-rtcp-status-publisher 92 18
  enable_boot_service v5-wcs-status-publisher 93 17
  enable_boot_service v5-state-publisher 94 16
  enable_boot_service v5-ui-relay 95 15
  enable_boot_service v5-settings-actiond 96 14
  enable_boot_service v5-touch-diagnostics 97 13
}

cleanup_retired_runtime_files() {
  rm -f /run/8ax_v5_drive/settings_self_parameter_table.json
  rm -f /opt/8ax/v5/config/settings/microkernel_parameter_table.tsv
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
  /etc/init.d/v5-rtcp-status-publisher restart
  /etc/init.d/v5-wcs-status-publisher restart
  /etc/init.d/v5-state-publisher restart
  /etc/init.d/v5-ui-relay restart
  /etc/init.d/v5-settings-actiond restart
  /etc/init.d/v5-touch-diagnostics restart
else
  echo "dry-run only; pass --apply to install files and restart v5 init services"
fi
