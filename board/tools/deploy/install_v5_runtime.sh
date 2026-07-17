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
linuxcnc_package_root="$repo_root/linuxcnc-package-root"
linuxcnc_bundle_allowlist="$repo_root/config/deploy/v5_linuxcnc_runtime_allowlist.tsv"
linuxcnc_bundle_hashes="$repo_root/config/deploy/v5_linuxcnc_deploy_bundle.sha256"
linuxcnc_bundle_enabled=0
linuxcnc_bundle_count=0
restart_scope_requested="${V5_RUNTIME_RESTART_SCOPE:-auto}"
restart_scope=all

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

verify_linuxcnc_deploy_bundle() {
  embedded_allowlist="$linuxcnc_package_root/usr/share/v5-native/linuxcnc-runtime-allowlist.tsv"
  runtime_hashes="$linuxcnc_package_root/usr/share/v5-native/linuxcnc-runtime-files.sha256"
  linuxcnc_rtapi_app="$linuxcnc_package_root/usr/bin/rtapi_app"
  [ -d "$linuxcnc_package_root" ] || {
    echo "missing LinuxCNC deploy bundle: $linuxcnc_package_root" >&2
    exit 7
  }
  for required in "$linuxcnc_bundle_allowlist" "$linuxcnc_bundle_hashes" "$embedded_allowlist" "$runtime_hashes"; do
    [ -r "$required" ] || {
      echo "missing LinuxCNC deploy bundle input: $required" >&2
      exit 7
    }
  done
  cmp -s "$linuxcnc_bundle_allowlist" "$embedded_allowlist" || {
    echo "LinuxCNC deploy allowlist differs from the packaged owner" >&2
    exit 7
  }
  [ -f "$linuxcnc_rtapi_app" ] && [ "$(stat -c '%a' "$linuxcnc_rtapi_app")" = "4755" ] || {
    echo "LinuxCNC deploy bundle rtapi_app must retain setuid mode 4755" >&2
    exit 7
  }

  expected="${TMPDIR:-/tmp}/v5_linuxcnc_expected.$$"
  actual="${TMPDIR:-/tmp}/v5_linuxcnc_actual.$$"
  hashed="${TMPDIR:-/tmp}/v5_linuxcnc_hashed.$$"
  trap 'rm -f "$expected" "$actual" "$hashed"' 0 1 2 15
  awk -F '\t' '
    $0 !~ /^#/ && NF >= 1 {
      if ($1 !~ /^\// || $1 ~ /(^|\/)\.\.?($|\/)/ || index($1, "//")) exit 2
      print "." $1
    }
  ' "$linuxcnc_bundle_allowlist" >"$expected"
  printf '%s\n' \
    ./usr/share/v5-native/linuxcnc-runtime-allowlist.tsv \
    ./usr/share/v5-native/linuxcnc-runtime-files.sha256 \
    ./usr/share/v5-native/linuxcnc-source-identity.txt \
    ./usr/share/v5-native/v5_linuxcnc_source_identity.json >>"$expected"
  LC_ALL=C sort -u "$expected" -o "$expected"
  expected_count=$(wc -l <"$expected")
  (
    cd "$linuxcnc_package_root"
    find . -type f -print | LC_ALL=C sort
  ) >"$actual"
  awk '{print $2}' "$linuxcnc_bundle_hashes" | LC_ALL=C sort >"$hashed"
  actual_count=$(wc -l <"$actual")
  hashed_count=$(wc -l <"$hashed")
  [ "$actual_count" -eq "$expected_count" ] || {
    echo "unexpected LinuxCNC deploy file count: expected=$expected_count actual=$actual_count" >&2
    exit 7
  }
  [ "$hashed_count" -eq "$expected_count" ] || {
    echo "unexpected LinuxCNC deploy hash count: expected=$expected_count actual=$hashed_count" >&2
    exit 7
  }
  cmp -s "$expected" "$actual" || {
    echo "LinuxCNC deploy bundle file set differs from the allowlist" >&2
    exit 7
  }
  cmp -s "$actual" "$hashed" || {
    echo "LinuxCNC deploy hash manifest file set differs from the bundle" >&2
    exit 7
  }
  (
    cd "$linuxcnc_package_root"
    sha256sum -c "$linuxcnc_bundle_hashes"
    sha256sum -c ./usr/share/v5-native/linuxcnc-runtime-files.sha256
  )
  rm -f "$expected" "$actual" "$hashed"
  trap - 0 1 2 15
  linuxcnc_bundle_count=$expected_count
  echo "V5_LINUXCNC_DEPLOY_BUNDLE_OK files=$linuxcnc_bundle_count"
}

install_linuxcnc_deploy_bundle() {
  (
    cd "$linuxcnc_package_root"
    find . -type f -print | LC_ALL=C sort
  ) | while IFS= read -r relative; do
    source_path="$linuxcnc_package_root/${relative#./}"
    destination="/${relative#./}"
    temporary="$destination.v5-new.$$"
    source_mode=$(stat -c '%a' "$source_path")
    install -d "$(dirname "$destination")"
    cp -p "$source_path" "$temporary"
    chown root:root "$temporary"
    chmod "$source_mode" "$temporary"
    mv -f "$temporary" "$destination"
  done
  while read -r digest relative extra; do
    [ -z "${extra:-}" ] || {
      echo "bad LinuxCNC deploy hash row: $digest $relative $extra" >&2
      exit 7
    }
    destination="/${relative#./}"
    printf '%s  %s\n' "$digest" "$destination" | sha256sum -c -
  done <"$linuxcnc_bundle_hashes"
  linuxcnc_rtapi_app=/usr/bin/rtapi_app
  [ -f "$linuxcnc_rtapi_app" ] && [ "$(stat -c '%u:%g:%a' "$linuxcnc_rtapi_app")" = "0:0:4755" ] || {
    echo "installed LinuxCNC rtapi_app is not root:root 4755" >&2
    exit 7
  }
  echo "V5_LINUXCNC_DEPLOY_INSTALL_OK files=$linuxcnc_bundle_count"
}

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
if [ -d "$linuxcnc_package_root" ] || [ -e "$linuxcnc_bundle_allowlist" ] || [ -e "$linuxcnc_bundle_hashes" ]; then
  linuxcnc_bundle_enabled=1
  [ -d "$linuxcnc_package_root" ] && [ -r "$linuxcnc_bundle_allowlist" ] && [ -r "$linuxcnc_bundle_hashes" ] || {
    echo "incomplete LinuxCNC deploy bundle" >&2
    exit 7
  }
fi

manifest_ui_only=1
manifest_actiond_only=1
manifest_settings_only=1
manifest_command_gate_only=1
manifest_wcs_only=1
manifest_backend_only=1
manifest_gcode_only=1
manifest_cpu_policy_only=1
manifest_cpu_policy_net_core=0
manifest_cpu_policy_usb_wifi=0
manifest_cpu_policy_relay_payload=0
manifest_cpu_policy_command_gate=0
manifest_cpu_policy_ui=0
manifest_cpu_policy_state=0
manifest_cpu_policy_wcs=0
manifest_cpu_policy_position=0
manifest_position_publisher=0
manifest_drive_profiles=0
manifest_row_count=0
while IFS="$tab" read -r scope_kind scope_source scope_destination scope_mode scope_extra; do
  case "$scope_kind" in
    ''|'#'*) continue ;;
  esac
  manifest_row_count=$((manifest_row_count + 1))
  case "$scope_destination" in
    /usr/local/sbin/v5_net_core.sh)
      manifest_cpu_policy_net_core=1
      ;;
    /usr/local/sbin/v5_usb_wifi_apply.sh)
      manifest_cpu_policy_usb_wifi=1
      ;;
    /usr/libexec/8ax/v5_remote_ui_shared_payload.py)
      manifest_cpu_policy_relay_payload=1
      ;;
    /etc/init.d/v5-linuxcnc-command-gate)
      manifest_cpu_policy_command_gate=1
      ;;
    /etc/init.d/v5-ui-relay)
      manifest_cpu_policy_ui=1
      ;;
    /etc/init.d/v5-state-publisher)
      manifest_cpu_policy_state=1
      ;;
    /etc/init.d/v5-wcs-status-publisher)
      manifest_cpu_policy_wcs=1
      ;;
    /etc/init.d/v5-position-status-publisher)
      manifest_cpu_policy_position=1
      manifest_position_publisher=1
      ;;
    *)
      manifest_cpu_policy_only=0
      ;;
  esac
  case "$scope_destination" in
    /opt/8ax/v5/gcode/golden/cc-ac.ngc|/opt/8ax/v5/gcode/golden/cc-bc.ngc)
      ;;
    *)
      manifest_gcode_only=0
      ;;
  esac
  case "$scope_destination" in
    /usr/lib/linuxcnc/modules/cia402.so)
      if [ "$scope_kind:$scope_source:$scope_mode" != "binary:build/board/app/cia402.so:0644" ]; then
        echo "cia402 backend module requires the registered ARM build artifact and mode 0644" >&2
        exit 6
      fi
      ;;
    *)
      manifest_backend_only=0
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_position_status_publisher.py|\
    /usr/libexec/8ax/v5_polling_cadence.py|\
    /etc/init.d/v5-position-status-publisher)
      manifest_position_publisher=1
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_position_status_publisher.py|\
    /usr/libexec/8ax/v5_wcs_status_publisher.py|\
    /usr/libexec/8ax/v5_polling_cadence.py|\
    /usr/libexec/8ax/v5_machine_status_projection.py|\
    /usr/libexec/8ax/v5_wcs_status_codec.py|\
    /usr/libexec/8ax/v5_native_operator_error_map.py|\
    /etc/init.d/v5-wcs-status-publisher|\
    /etc/init.d/v5-position-status-publisher)
      manifest_ui_only=0
      manifest_actiond_only=0
      manifest_settings_only=0
      manifest_command_gate_only=0
      ;;
    *)
      manifest_wcs_only=0
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_lvgl_shell|\
    /usr/libexec/8ax/v5_remote_ui_*|\
    /usr/libexec/8ax/v5_ui_*|\
    /etc/init.d/v5-ui-relay)
      manifest_actiond_only=0
      manifest_settings_only=0
      manifest_command_gate_only=0
      ;;
    /usr/libexec/8ax/drive_profile/*|\
    /usr/libexec/8ax/auth_download/*|\
    /etc/init.d/v5-settings-actiond)
      manifest_ui_only=0
      manifest_command_gate_only=0
      ;;
    /opt/8ax/v5/config/drive-profiles/*)
      manifest_ui_only=0
      manifest_drive_profiles=1
      manifest_command_gate_only=0
      ;;
    /opt/8ax/phase0_bus5/settings_runtime.json)
      manifest_ui_only=0
      manifest_actiond_only=0
      manifest_command_gate_only=0
      ;;
    /usr/libexec/8ax/v5_command_gate_server|\
    /usr/libexec/8ax/v5_command_gate_drive_window|\
    /usr/libexec/8ax/v5_ethercat_backend_lifecycle.sh|\
    /etc/init.d/v5-linuxcnc-command-gate)
      manifest_ui_only=0
      manifest_actiond_only=0
      ;;
    *)
      manifest_ui_only=0
      manifest_actiond_only=0
      manifest_settings_only=0
      manifest_command_gate_only=0
      ;;
  esac
done < "$manifest"

case "$restart_scope_requested" in
  auto)
    if [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
       [ "$manifest_row_count" -gt 0 ] &&
       [ "$manifest_gcode_only" -eq 1 ]; then
      restart_scope=gcode
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
       [ "$manifest_row_count" -gt 0 ] &&
       [ "$manifest_ui_only" -eq 1 ]; then
      restart_scope=ui
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_actiond_only" -eq 1 ]; then
      restart_scope=actiond
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_command_gate_only" -eq 1 ]; then
      restart_scope=command_gate
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -eq 1 ] &&
         [ "$manifest_backend_only" -eq 1 ]; then
      restart_scope=backend
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_wcs_only" -eq 1 ]; then
      restart_scope=wcs
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_cpu_policy_only" -eq 1 ] &&
         [ "$manifest_cpu_policy_net_core" -eq 1 ] &&
         [ "$manifest_cpu_policy_usb_wifi" -eq 1 ] &&
         [ "$manifest_cpu_policy_relay_payload" -eq 1 ] &&
         [ "$manifest_cpu_policy_command_gate" -eq 1 ] &&
         [ "$manifest_cpu_policy_ui" -eq 1 ] &&
         [ "$manifest_cpu_policy_state" -eq 1 ] &&
         [ "$manifest_cpu_policy_wcs" -eq 1 ] &&
         [ "$manifest_cpu_policy_position" -eq 1 ]; then
      restart_scope=cpu_policy
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_settings_only" -eq 1 ]; then
      restart_scope=settings
    fi
    ;;
  gcode)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_gcode_only" -ne 1 ]; then
      echo "G-code restart scope requires a non-empty cc-ac/cc-bc manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=gcode
    ;;
  ui)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_ui_only" -ne 1 ]; then
      echo "UI-only restart scope requires a non-empty UI-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=ui
    ;;
  actiond)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_actiond_only" -ne 1 ]; then
      echo "Actiond-only restart scope requires a non-empty actiond-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=actiond
    ;;
  command_gate)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_command_gate_only" -ne 1 ]; then
      echo "Command-gate restart scope requires a non-empty command-gate-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=command_gate
    ;;
  backend)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -ne 1 ] ||
       [ "$manifest_backend_only" -ne 1 ]; then
      echo "Backend restart scope requires exactly one registered backend-module row and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=backend
    ;;
  wcs)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_wcs_only" -ne 1 ]; then
      echo "WCS-publisher restart scope requires a non-empty WCS-publisher-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=wcs
    ;;
  cpu_policy)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_cpu_policy_only" -ne 1 ] ||
       [ "$manifest_cpu_policy_net_core" -ne 1 ] ||
       [ "$manifest_cpu_policy_usb_wifi" -ne 1 ] ||
       [ "$manifest_cpu_policy_relay_payload" -ne 1 ] ||
       [ "$manifest_cpu_policy_command_gate" -ne 1 ] ||
       [ "$manifest_cpu_policy_ui" -ne 1 ] ||
       [ "$manifest_cpu_policy_state" -ne 1 ] ||
       [ "$manifest_cpu_policy_wcs" -ne 1 ] ||
       [ "$manifest_cpu_policy_position" -ne 1 ]; then
      echo "CPU-policy restart scope requires the complete registered CPU-policy manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=cpu_policy
    ;;
  settings)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_settings_only" -ne 1 ]; then
      echo "Settings restart scope requires a non-empty settings-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=settings
    ;;
  all)
    restart_scope=all
    ;;
  *)
    echo "unsupported V5_RUNTIME_RESTART_SCOPE: $restart_scope_requested (expected auto, gcode, ui, actiond, command_gate, backend, wcs, cpu_policy, settings, or all)" >&2
    exit 8
    ;;
esac
echo "V5_RUNTIME_RESTART_SCOPE scope=$restart_scope rows=$manifest_row_count gcode_only=$manifest_gcode_only ui_only=$manifest_ui_only actiond_only=$manifest_actiond_only command_gate_only=$manifest_command_gate_only backend_only=$manifest_backend_only wcs_only=$manifest_wcs_only cpu_policy_only=$manifest_cpu_policy_only settings_only=$manifest_settings_only"

stop_position_publisher_before_backend() {
  if [ -x /etc/init.d/v5-position-status-publisher ]; then
    /etc/init.d/v5-position-status-publisher stop
  fi
}

restart_position_publisher_after_backend() {
  [ -x /etc/init.d/v5-position-status-publisher ] || {
    echo "position status publisher init is missing after backend restart" >&2
    return 1
  }
  /etc/init.d/v5-position-status-publisher restart
}

if [ "$apply" -eq 1 ] &&
   [ "$restart_scope" = "wcs" ] &&
   [ "$manifest_position_publisher" -eq 1 ]; then
  for pin in motion.feed-override spindle.0.override; do
    halcmd getp "$pin" >/dev/null 2>&1 || {
      echo "position publisher focused deploy requires an active native override pin: $pin; deploy the verified LinuxCNC package first" >&2
      exit 8
    }
  done
fi

if [ "$apply" -eq 1 ] && [ "$linuxcnc_bundle_enabled" -eq 1 ]; then
  verify_linuxcnc_deploy_bundle
  [ -x /etc/init.d/v5-linuxcnc-command-gate ] || {
    echo "LinuxCNC command-gate init is missing before deploy" >&2
    exit 7
  }
  stop_position_publisher_before_backend
  /etc/init.d/v5-linuxcnc-command-gate stop
  install_linuxcnc_deploy_bundle
fi
if [ "$apply" -eq 1 ] && [ "$restart_scope" = "backend" ]; then
  [ -x /etc/init.d/v5-linuxcnc-command-gate ] || {
    echo "LinuxCNC command-gate init is missing before backend module deploy" >&2
    exit 7
  }
  stop_position_publisher_before_backend
  /etc/init.d/v5-linuxcnc-command-gate stop
fi
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
  if [ "$restart_scope" = "backend" ]; then
    temporary="$destination.v5-new.$$"
    install -m "$mode" "$source_path" "$temporary"
    chown root:root "$temporary"
    mv -f "$temporary" "$destination"
  else
    install -m "$mode" "$source_path" "$destination"
  fi
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
  enable_boot_service v5-position-status-publisher 92 18
  enable_boot_service v5-wcs-status-publisher 93 17
  enable_boot_service v5-state-publisher 94 16
  enable_boot_service v5-ui-relay 95 15
  enable_boot_service v5-settings-actiond 96 14
  enable_boot_service v5-touch-diagnostics 97 13
  enable_boot_service v5-remote-ssh 98 12
}

retired_pid_matches_path() {
  retired_pid="$1"
  retired_path="$2"
  [ -r "/proc/$retired_pid/cmdline" ] || return 1
  tr '\000' '\n' <"/proc/$retired_pid/cmdline" | grep -Fqx "$retired_path"
}

stop_retired_runtime_path() {
  retired_path="$1"
  for cmdline in /proc/[0-9]*/cmdline; do
    [ -r "$cmdline" ] || continue
    retired_pid=${cmdline#/proc/}
    retired_pid=${retired_pid%/cmdline}
    retired_pid_matches_path "$retired_pid" "$retired_path" || continue
    kill "$retired_pid" 2>/dev/null || true
    retired_wait=0
    while retired_pid_matches_path "$retired_pid" "$retired_path" && [ "$retired_wait" -lt 20 ]; do
      retired_wait=$((retired_wait + 1))
      sleep 0.1
    done
    if retired_pid_matches_path "$retired_pid" "$retired_path"; then
      kill -KILL "$retired_pid" 2>/dev/null || true
      retired_wait=0
      while retired_pid_matches_path "$retired_pid" "$retired_path" && [ "$retired_wait" -lt 20 ]; do
        retired_wait=$((retired_wait + 1))
        sleep 0.1
      done
    fi
    if retired_pid_matches_path "$retired_pid" "$retired_path"; then
      echo "retired runtime process did not stop: $retired_path pid=$retired_pid" >&2
      exit 7
    fi
  done
}

cleanup_retired_runtime_files() {
  for retired_init in /etc/init.d/v5-rtcp-status-publisher /etc/init.d/v5-g53-geometry-memory-owner; do
    if [ -x "$retired_init" ]; then
      "$retired_init" stop
    fi
  done
  for retired_path in \
    /usr/libexec/8ax/v5_rtcp_status_publisher.py \
    /usr/libexec/8ax/v5_g53_geometry_memory_owner.py \
    /usr/libexec/8ax/v5_native_safety_latch_owner.py
  do
    stop_retired_runtime_path "$retired_path"
  done
  rm -f /run/8ax/v5_rtcp_status_publisher.pid /run/8ax/v5_g53_geometry_memory_owner.pid
  rm -f /run/8ax_v5_drive/settings_self_parameter_table.json
  rm -f /opt/8ax/v5/config/settings/microkernel_parameter_table.tsv
  rm -f /opt/8ax/v5/gcode/golden/cc.ngc
  rm -f /tmp/v5_golden/cc.ngc
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_calibration.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_restart.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_runtime.py
  rm -f /usr/libexec/8ax/v5_rtcp_status_publisher.py
  rm -f /usr/libexec/8ax/v5_g53_geometry_memory_owner.py
  rm -f /usr/libexec/8ax/v5_native_safety_latch_owner.py
  rm -f /etc/init.d/v5-rtcp-status-publisher
  rm -f /etc/init.d/v5-g53-geometry-memory-owner
  for level in 0 1 2 3 4 5 6; do
    rm -f "/etc/rc${level}.d"/[SK]??v5-rtcp-status-publisher
    rm -f "/etc/rc${level}.d"/[SK]??v5-g53-geometry-memory-owner
  done
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
  if [ "$restart_scope" = "gcode" ]; then
    rm -f /opt/8ax/v5/gcode/golden/cc.ngc
  elif [ "$restart_scope" = "ui" ]; then
    enable_boot_service v5-ui-relay 95 15
    /etc/init.d/v5-ui-relay restart
  elif [ "$restart_scope" = "actiond" ]; then
    enable_boot_service v5-settings-actiond 96 14
    [ "$manifest_drive_profiles" -eq 0 ] || install_runtime_drive_profiles
    /etc/init.d/v5-settings-actiond restart
  elif [ "$restart_scope" = "command_gate" ]; then
    enable_boot_service v5-linuxcnc-command-gate 91 19
    /etc/init.d/v5-linuxcnc-command-gate restart-native
  elif [ "$restart_scope" = "backend" ]; then
    enable_boot_service v5-linuxcnc-command-gate 91 19
    enable_boot_service v5-position-status-publisher 92 18
    /etc/init.d/v5-linuxcnc-command-gate start
    restart_position_publisher_after_backend
  elif [ "$restart_scope" = "wcs" ]; then
    enable_boot_service v5-position-status-publisher 92 18
    enable_boot_service v5-wcs-status-publisher 93 17
    /etc/init.d/v5-position-status-publisher restart
    /etc/init.d/v5-wcs-status-publisher restart
  elif [ "$restart_scope" = "cpu_policy" ]; then
    enable_boot_service v5-linuxcnc-command-gate 91 19
    enable_boot_service v5-position-status-publisher 92 18
    enable_boot_service v5-wcs-status-publisher 93 17
    enable_boot_service v5-state-publisher 94 16
    enable_boot_service v5-ui-relay 95 15
    (
      LOG=/run/8ax/v5_cpu_policy.log
      . /usr/local/sbin/v5_net_core.sh
      apply_network_cpu_isolation
    )
    /etc/init.d/v5-linuxcnc-command-gate restart-native
    /etc/init.d/v5-position-status-publisher restart
    /etc/init.d/v5-wcs-status-publisher restart
    /etc/init.d/v5-state-publisher restart
    /etc/init.d/v5-ui-relay restart
  elif [ "$restart_scope" = "settings" ]; then
    enable_boot_service v5-linuxcnc-command-gate 91 19
    enable_boot_service v5-position-status-publisher 92 18
    enable_boot_service v5-wcs-status-publisher 93 17
    enable_boot_service v5-state-publisher 94 16
    enable_boot_service v5-settings-actiond 96 14
    [ "$manifest_drive_profiles" -eq 0 ] || install_runtime_drive_profiles
    stop_position_publisher_before_backend
    /etc/init.d/v5-linuxcnc-command-gate restart
    restart_position_publisher_after_backend
    /etc/init.d/v5-wcs-status-publisher restart
    /etc/init.d/v5-state-publisher restart
    /etc/init.d/v5-settings-actiond restart
  else
    enable_boot_services
    cleanup_retired_runtime_files
    install_runtime_drive_profiles
    stop_position_publisher_before_backend
    /etc/init.d/v5-linuxcnc-command-gate restart
    restart_position_publisher_after_backend
    /etc/init.d/v5-wcs-status-publisher restart
    /etc/init.d/v5-state-publisher restart
    /etc/init.d/v5-ui-relay restart
    /etc/init.d/v5-settings-actiond restart
    /etc/init.d/v5-touch-diagnostics restart
    /etc/init.d/v5-remote-ssh restart
  fi
else
  echo "dry-run only; pass --apply to install files and restart scope=$restart_scope"
fi
