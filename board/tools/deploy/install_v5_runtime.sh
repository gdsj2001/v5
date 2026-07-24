#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
repo_root="${V5_REPO_ROOT:-$default_repo_root}"
manifest="${1:-$repo_root/config/deploy/v5_runtime_deploy_manifest.tsv}"
apply=0
vm_build_root="${VM_BUILD_ROOT:-/root/v5-build}"
parameter_table_snapshot_root="$vm_build_root/temp_parameter_snapshot"
parameter_table_transaction_dir=
parameter_table_transaction_active=0
parameter_table_transaction_count=0
parameter_table_transaction_entry=
linuxcnc_package_root="$repo_root/linuxcnc-package-root"
linuxcnc_bundle_allowlist="$repo_root/config/deploy/v5_linuxcnc_runtime_allowlist.tsv"
linuxcnc_bundle_hashes="$repo_root/config/deploy/v5_linuxcnc_deploy_bundle.sha256"
linuxcnc_bundle_enabled=0
linuxcnc_bundle_count=0
restart_scope_requested="${V5_RUNTIME_RESTART_SCOPE:-auto}"
restart_scope=all
PROC_ROOT=/proc
RUNTIME_PROJECT_ROOT=/opt/8ax/v5
PUBLISHER_ACTUAL_BARRIER=/usr/libexec/8ax/v5_ui_boot_ready.py
PUBLISHER_SNAPSHOT_PATH=/run/8ax_v5_product_ui/ui_input_barrier.json

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
  linuxcnc_native_hal_owner="$linuxcnc_package_root/usr/bin/v5_native_hal_owner"
  linuxcnc_bus_axis_router="$linuxcnc_package_root/usr/lib/linuxcnc/modules/v5_bus_axis_router.so"
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
  for required_native_protocol_owner in \
    "$linuxcnc_native_hal_owner" \
    "$linuxcnc_bus_axis_router"; do
    [ -f "$required_native_protocol_owner" ] || {
      echo "LinuxCNC deploy bundle is missing native protocol owner: $required_native_protocol_owner" >&2
      exit 7
    }
  done

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

parameter_table_transaction_cleanup() {
  [ -n "$parameter_table_transaction_dir" ] || return 0
  case "$parameter_table_transaction_dir" in
    "$parameter_table_snapshot_root"/v5-runtime-*) ;;
    *)
      echo "refusing unsafe parameter snapshot cleanup: $parameter_table_transaction_dir" >&2
      return 1
      ;;
  esac
  rm -rf "$parameter_table_transaction_dir"
  [ ! -e "$parameter_table_transaction_dir" ] || {
    echo "parameter snapshot cleanup failed: $parameter_table_transaction_dir" >&2
    return 1
  }
  rmdir "$parameter_table_snapshot_root" 2>/dev/null || true
  parameter_table_transaction_dir=
}

parameter_table_transaction_restore() {
  restore_failed=0
  [ -n "$parameter_table_transaction_dir" ] || return 0
  for snapshot_entry in "$parameter_table_transaction_dir"/entry-*; do
    [ -d "$snapshot_entry" ] || continue
    IFS= read -r snapshot_destination < "$snapshot_entry/destination" || {
      echo "parameter snapshot destination metadata is unreadable: $snapshot_entry" >&2
      restore_failed=1
      continue
    }
    case "$snapshot_destination" in
      /*|[A-Za-z]:/*) ;;
      *)
        echo "parameter snapshot destination is unsafe: $snapshot_destination" >&2
        restore_failed=1
        continue
        ;;
    esac
    IFS= read -r snapshot_state < "$snapshot_entry/state" || {
      echo "parameter snapshot state metadata is unreadable: $snapshot_entry" >&2
      restore_failed=1
      continue
    }
    if [ "$snapshot_state" = "present" ]; then
      restore_temporary="$snapshot_destination.v5-restore.$$"
      rm -f "$restore_temporary"
      if ! cp -p "$snapshot_entry/original" "$restore_temporary" ||
         ! mv -f "$restore_temporary" "$snapshot_destination" ||
         ! cmp -s "$snapshot_entry/original" "$snapshot_destination" ||
         [ "$(stat -c '%u:%g:%a' "$snapshot_entry/original")" != "$(stat -c '%u:%g:%a' "$snapshot_destination")" ]; then
        rm -f "$restore_temporary"
        echo "parameter owner rollback failed: $snapshot_destination" >&2
        restore_failed=1
      else
        echo "restored parameter owner after failed deploy: $snapshot_destination" >&2
      fi
    elif [ "$snapshot_state" = "absent" ]; then
      rm -f "$snapshot_destination"
      if [ -e "$snapshot_destination" ] || [ -L "$snapshot_destination" ]; then
        echo "new parameter owner rollback failed: $snapshot_destination" >&2
        restore_failed=1
      fi
    else
      echo "parameter snapshot state is invalid: $snapshot_entry/state" >&2
      restore_failed=1
    fi
  done
  [ "$restore_failed" -eq 0 ]
}

parameter_table_transaction_on_exit() {
  transaction_status="$1"
  trap - 0 1 2 15
  transaction_restore_status=0
  transaction_cleanup_status=0
  if [ "$transaction_status" -ne 0 ]; then
    parameter_table_transaction_restore || transaction_restore_status=$?
  fi
  parameter_table_transaction_cleanup || transaction_cleanup_status=$?
  if [ "$transaction_restore_status" -ne 0 ] || [ "$transaction_cleanup_status" -ne 0 ]; then
    transaction_status=9
  fi
  exit "$transaction_status"
}

parameter_table_transaction_begin() {
  [ "$parameter_table_transaction_active" -eq 0 ] || return 0
  case "$parameter_table_snapshot_root" in
    */temp_parameter_snapshot) ;;
    *)
      echo "parameter snapshot root must end in temp_parameter_snapshot: $parameter_table_snapshot_root" >&2
      return 1
      ;;
  esac
  transaction_run_id="v5-runtime-$(date -u +%Y%m%dT%H%M%SZ)-$$"
  parameter_table_transaction_dir="$parameter_table_snapshot_root/$transaction_run_id"
  (umask 077; mkdir -p "$parameter_table_snapshot_root"; mkdir "$parameter_table_transaction_dir")
  parameter_table_transaction_active=1
  trap 'parameter_table_transaction_on_exit "$?"' 0
  trap 'exit 129' 1
  trap 'exit 130' 2
  trap 'exit 143' 15
}

parameter_table_transaction_snapshot() {
  snapshot_destination="$1"
  parameter_table_transaction_begin
  for existing_entry in "$parameter_table_transaction_dir"/entry-*; do
    [ -d "$existing_entry" ] || continue
    IFS= read -r existing_destination < "$existing_entry/destination" || return 1
    if [ "$existing_destination" = "$snapshot_destination" ]; then
      parameter_table_transaction_entry="$existing_entry"
      return 0
    fi
  done
  parameter_table_transaction_count=$((parameter_table_transaction_count + 1))
  snapshot_entry="$parameter_table_transaction_dir/entry-$parameter_table_transaction_count"
  snapshot_staging="$parameter_table_transaction_dir/.entry-$parameter_table_transaction_count.tmp"
  (umask 077; mkdir "$snapshot_staging")
  printf '%s\n' "$snapshot_destination" > "$snapshot_staging/destination"
  if [ -L "$snapshot_destination" ]; then
    echo "parameter owner must not be a symlink: $snapshot_destination" >&2
    return 1
  elif [ -e "$snapshot_destination" ]; then
    [ -f "$snapshot_destination" ] || {
      echo "parameter owner must be a regular file: $snapshot_destination" >&2
      return 1
    }
    cp -p "$snapshot_destination" "$snapshot_staging/original"
    printf 'present\n' > "$snapshot_staging/state"
  else
    printf 'absent\n' > "$snapshot_staging/state"
  fi
  mv "$snapshot_staging" "$snapshot_entry"
  parameter_table_transaction_entry="$snapshot_entry"
}

parameter_table_transaction_complete() {
  [ "$parameter_table_transaction_active" -eq 1 ] || return 0
  parameter_table_transaction_cleanup
  trap - 0 1 2 15
  parameter_table_transaction_active=0
}

merge_runtime_seed_tsv() {
  source_path="$1"
  destination="$2"
  mode="$3"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 required to merge runtime seed table: $destination" >&2
    return 5
  fi
  [ -f "$source_path" ] && [ ! -L "$source_path" ] || {
    echo "runtime seed template must be a regular file: $source_path" >&2
    return 5
  }
  install -d "$(dirname "$destination")"
  parameter_table_transaction_snapshot "$destination"
  merged_artifact="$parameter_table_transaction_entry/merged.tsv"
  source_path="$source_path" destination="$destination" mode="$mode" merged_artifact="$merged_artifact" python3 - <<'PY'
import os
import shutil
import stat
from pathlib import Path

src = Path(os.environ["source_path"])
dst = Path(os.environ["destination"])
artifact = Path(os.environ["merged_artifact"])
mode = int(os.environ["mode"], 8)


def read_rows(path: Path, owner: str):
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit("%s parameter table is not UTF-8: %s" % (owner, path)) from exc
    rows = []
    seen = set()
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if (len(parts) != 3 or any(not part for part in parts) or
                any(part != part.strip() for part in parts)):
            raise SystemExit(
                "malformed %s parameter table row: %s:%d" %
                (owner, path, line_no))
        key = (parts[0], parts[1])
        if key in seen:
            raise SystemExit(
                "duplicate %s parameter key: %s:%d %s/%s" %
                (owner, path, line_no, key[0], key[1]))
        seen.add(key)
        rows.append((key[0], key[1], parts[2]))
    if not rows:
        raise SystemExit("empty %s parameter table: %s" % (owner, path))
    return rows


template_rows = read_rows(src, "template")
template_keys = {(axis, field) for axis, field, _ in template_rows}
board_rows = read_rows(dst, "board") if dst.exists() else []
board_values = {
    (axis, field): value
    for axis, field, value in board_rows
    if (axis, field) in template_keys
}
lines = ["# schema=v5.settings.parameter_table.tsv.v1"]
for axis, field, default in template_rows:
    lines.append(
        "%s\t%s\t%s" %
        (axis, field, board_values.get((axis, field), default)))
expected_text = "\n".join(lines) + "\n"
expected_keys = [(axis, field) for axis, field, _ in template_rows]

artifact.write_text(expected_text, encoding="utf-8")
os.chmod(artifact, mode)
artifact_rows = read_rows(artifact, "merged artifact")
if (artifact.read_text(encoding="utf-8") != expected_text or
        [(axis, field) for axis, field, _ in artifact_rows] != expected_keys):
    raise SystemExit("merged parameter deployment artifact validation failed: %s" % artifact)

temporary = dst.with_name(dst.name + ".v5-new.%s" % os.getpid())
try:
    shutil.copyfile(artifact, temporary)
    os.chmod(temporary, mode)
    os.replace(temporary, dst)
    actual_text = dst.read_text(encoding="utf-8")
    actual_rows = read_rows(dst, "installed board")
    actual_keys = [(axis, field) for axis, field, _ in actual_rows]
    actual_mode = stat.S_IMODE(dst.stat().st_mode)
    if (actual_text != expected_text or actual_keys != expected_keys or
            actual_mode != mode):
        raise SystemExit("installed parameter table readback validation failed: %s" % dst)
finally:
    try:
        temporary.unlink()
    except FileNotFoundError:
        pass

print(
    "merged runtime seed table %s -> %s rows=%d kept=%d retired=%d format=ok" %
    (src, dst, len(template_rows), len(board_values),
     len(board_rows) - len(board_values)))
PY
}

merge_runtime_bus_ini_cycle() {
  source_path="$1"
  destination="$2"
  mode="$3"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 required to merge runtime BUS INI cycle identity: $destination" >&2
    return 5
  fi
  [ -f "$source_path" ] && [ ! -L "$source_path" ] || {
    echo "runtime BUS INI template must be a regular file: $source_path" >&2
    return 5
  }
  if [ -L "$destination" ]; then
    echo "runtime BUS INI owner must not be a symlink: $destination" >&2
    return 5
  fi
  install -d "$(dirname "$destination")"
  parameter_table_transaction_snapshot "$destination"
  merged_artifact="$parameter_table_transaction_entry/merged.ini"
  source_path="$source_path" destination="$destination" mode="$mode" merged_artifact="$merged_artifact" python3 - <<'PY'
import os
import shutil
import stat
from pathlib import Path

src = Path(os.environ["source_path"])
dst = Path(os.environ["destination"])
artifact = Path(os.environ["merged_artifact"])
mode = int(os.environ["mode"], 8)
targets = {
    ("EMCMOT", "SERVO_PERIOD"): "1000000",
    ("HAL", "HALFILE"): "/opt/8ax/v5/linuxcnc/hal/v5_bus_1ms.hal",
    ("TRAJ", "ARC_BLEND_GAP_CYCLES"): "8",
}


def parse(path: Path, owner: str):
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit("%s BUS INI is not UTF-8: %s" % (owner, path)) from exc
    lines = text.splitlines()
    section = None
    found = {key: [] for key in targets}
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";")):
            continue
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].strip().upper()
            continue
        if "=" not in line or section is None:
            continue
        raw_key, raw_value = line.split("=", 1)
        key = (section, raw_key.strip().upper())
        if key in found:
            found[key].append((index, raw_value.strip()))
    for key, matches in found.items():
        if len(matches) != 1:
            raise SystemExit(
                "%s BUS INI must contain exactly one [%s] %s: %s count=%d" %
                (owner, key[0], key[1], path, len(matches)))
    return text, lines, found


source_text, source_lines, source_found = parse(src, "template")
for key, expected in targets.items():
    actual = source_found[key][0][1]
    if actual != expected:
        raise SystemExit(
            "template BUS INI cycle identity mismatch [%s] %s=%s expected=%s" %
            (key[0], key[1], actual, expected))

if dst.exists():
    _, output_lines, destination_found = parse(dst, "board")
    for key, expected in targets.items():
        index = destination_found[key][0][0]
        output_lines[index] = "%s = %s" % (key[1], expected)
    expected_text = "\n".join(output_lines) + "\n"
else:
    expected_text = source_text.rstrip("\r\n") + "\n"

artifact.write_text(expected_text, encoding="utf-8")
os.chmod(artifact, mode)
_, _, artifact_found = parse(artifact, "merged artifact")
for key, expected in targets.items():
    if artifact_found[key][0][1] != expected:
        raise SystemExit("merged BUS INI cycle identity validation failed: %s" % artifact)

temporary = dst.with_name(dst.name + ".v5-new.%s" % os.getpid())
try:
    shutil.copyfile(artifact, temporary)
    os.chmod(temporary, mode)
    os.replace(temporary, dst)
    actual_text, _, actual_found = parse(dst, "installed board")
    actual_mode = stat.S_IMODE(dst.stat().st_mode)
    if actual_text != expected_text or actual_mode != mode:
        raise SystemExit("installed BUS INI readback validation failed: %s" % dst)
    for key, expected in targets.items():
        if actual_found[key][0][1] != expected:
            raise SystemExit("installed BUS INI cycle identity mismatch: %s" % dst)
finally:
    try:
        temporary.unlink()
    except FileNotFoundError:
        pass

print("merged runtime BUS INI cycle identity %s -> %s keys=%d format=ok" %
      (src, dst, len(targets)))
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
manifest_state_only=1
manifest_actiond_only=1
manifest_actiond_touched=0
manifest_settings_only=1
manifest_command_gate_only=1
manifest_native_protocol_command_gate=0
manifest_native_protocol_zero_client=0
manifest_wcs_only=1
manifest_backend_only=1
manifest_ethercat_only=1
manifest_gcode_only=1
manifest_cpu_policy_only=1
manifest_runtime_startup_only=1
manifest_cpu_policy_net_core=0
manifest_cpu_policy_net_module=0
manifest_cpu_policy_net_init=0
manifest_cpu_policy_usb_wifi=0
manifest_cpu_policy_relay_payload=0
manifest_cpu_policy_command_gate=0
manifest_cpu_policy_ui=0
manifest_cpu_policy_state=0
manifest_cpu_policy_wcs=0
manifest_cpu_policy_position=0
manifest_cpu_policy_runtime_startup=0
manifest_position_publisher=0
manifest_wcs_publisher=0
manifest_state_publisher=0
manifest_drive_profiles=0
manifest_shm_abi_touched=0
manifest_shm_abi_complete=0
manifest_shm_abi_ui_binary=0
manifest_shm_abi_state_binary=0
manifest_shm_abi_position_binary=0
manifest_shm_abi_wcs_script=0
manifest_shm_abi_cadence=0
manifest_shm_abi_projection=0
manifest_shm_abi_codec=0
manifest_shm_abi_relay=0
manifest_shm_abi_relay_access=0
manifest_shm_abi_relay_stream=0
manifest_shm_abi_boot_ready=0
manifest_shm_abi_boot_inputs=0
manifest_shm_abi_main_cache_contract=0
manifest_shm_abi_python_reader=0
manifest_shm_abi_position_init=0
manifest_shm_abi_wcs_init=0
manifest_shm_abi_state_init=0
manifest_shm_abi_ui_init=0
manifest_shm_abi_required_rows=18
manifest_ethercat_master=0
manifest_ethercat_generic=0
manifest_ethercat_lcec=0
manifest_ethercat_lifecycle=0
manifest_ethercat_touched=0
manifest_ethercat_complete=0
manifest_ethercat_required_rows=4
manifest_bus_cycle_touched=0
manifest_bus_cycle_complete=0
manifest_bus_cycle_ini=0
manifest_bus_cycle_hal=0
manifest_bus_cycle_xml=0
manifest_bus_cycle_readiness_probe=0
manifest_bus_cycle_command_gate_init=0
ethercat_master_destination="/lib/modules/$(uname -r)/ethercat/master/ec_master.ko"
ethercat_generic_destination="/lib/modules/$(uname -r)/ethercat/devices/ec_generic.ko"
manifest_row_count=0
while IFS="$tab" read -r scope_kind scope_source scope_destination scope_mode scope_extra; do
  case "$scope_kind" in
    ''|'#'*) continue ;;
  esac
  manifest_row_count=$((manifest_row_count + 1))
  case "$scope_destination" in
    /opt/8ax/v5/linuxcnc/ini/v5_bus.ini)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "runtime_ini_cycle_merge:linuxcnc/ini/v5_bus.ini:0644" ] || {
        echo "BUS cycle INI requires the registered merge owner and mode 0644" >&2
        exit 6
      }
      manifest_bus_cycle_touched=1
      manifest_bus_cycle_ini=1
      ;;
    /opt/8ax/v5/linuxcnc/hal/v5_bus_*ms.hal)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "linuxcnc:linuxcnc/hal/v5_bus_1ms.hal:0644" ] || {
        echo "BUS cycle HAL requires the unique registered 1ms owner and mode 0644" >&2
        exit 6
      }
      manifest_bus_cycle_touched=1
      manifest_bus_cycle_hal=1
      ;;
    /opt/8ax/v5/linuxcnc/hal/ethercat-conf-*ms.xml)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "linuxcnc:linuxcnc/hal/ethercat-conf-1ms.xml:0644" ] || {
        echo "BUS cycle XML requires the unique registered 1ms owner and mode 0644" >&2
        exit 6
      }
      manifest_bus_cycle_touched=1
      manifest_bus_cycle_xml=1
      ;;
    /usr/libexec/8ax/v5_backend_readiness_probe)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "binary:build/board/app/v5_backend_readiness_probe:0755" ] || {
        echo "BUS cycle readiness requires the registered ARM probe and mode 0755" >&2
        exit 6
      }
      manifest_bus_cycle_readiness_probe=1
      ;;
    /etc/init.d/v5-linuxcnc-command-gate)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "init:services/command_gate/init.d/v5-linuxcnc-command-gate:0755" ] || {
        echo "BUS cycle lifecycle requires the registered Command Gate init and mode 0755" >&2
        exit 6
      }
      manifest_bus_cycle_command_gate_init=1
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_command_gate_server)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "binary:build/board/app/v5_command_gate_server:0755" ] || {
        echo "Command Gate native protocol client requires the registered ARM artifact and mode 0755" >&2
        exit 6
      }
      manifest_native_protocol_command_gate=1
      ;;
    /usr/libexec/8ax/drive_profile/v5_command_gate_zero_client.py)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "module:services/drive_profile/v5_command_gate_zero_client.py:0644" ] || {
        echo "Command Gate Python protocol client requires the registered source and mode 0644" >&2
        exit 6
      }
      manifest_native_protocol_zero_client=1
      ;;
  esac
  case "$scope_destination" in
    "$ethercat_master_destination")
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "kernel_module:build/ethercat/ec_master.ko:0644" ] || {
        echo "EtherCAT master module requires the registered ARM artifact and mode 0644" >&2
        exit 6
      }
      manifest_ethercat_touched=1
      manifest_ethercat_master=1
      ;;
    "$ethercat_generic_destination")
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "kernel_module:build/ethercat/ec_generic.ko:0644" ] || {
        echo "EtherCAT generic module requires the registered ARM artifact and mode 0644" >&2
        exit 6
      }
      manifest_ethercat_touched=1
      manifest_ethercat_generic=1
      ;;
    /usr/lib/linuxcnc/modules/lcec.so)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "binary:build/ethercat/lcec.so:0644" ] || {
        echo "EtherCAT lcec module requires the registered ARM artifact and mode 0644" >&2
        exit 6
      }
      manifest_ethercat_touched=1
      manifest_ethercat_lcec=1
      ;;
    /usr/libexec/8ax/v5_ethercat_backend_lifecycle.sh)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "module:services/command_gate/v5_ethercat_backend_lifecycle.sh:0644" ] || {
        echo "EtherCAT lifecycle requires the registered source and mode 0644" >&2
        exit 6
      }
      manifest_ethercat_touched=1
      manifest_ethercat_lifecycle=1
      ;;
    *)
      manifest_ethercat_only=0
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_lvgl_shell)
      manifest_shm_abi_touched=1
      manifest_shm_abi_ui_binary=1
      ;;
    /usr/libexec/8ax/v5_state_publisher)
      manifest_shm_abi_touched=1
      manifest_shm_abi_state_binary=1
      ;;
    /usr/libexec/8ax/v5_position_status_publisher)
      manifest_shm_abi_touched=1
      manifest_shm_abi_position_binary=1
      ;;
    /usr/libexec/8ax/v5_wcs_status_publisher.py)
      manifest_shm_abi_wcs_script=1
      ;;
    /usr/libexec/8ax/v5_polling_cadence.py)
      manifest_shm_abi_cadence=1
      ;;
    /usr/libexec/8ax/v5_machine_status_projection.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_projection=1
      ;;
    /usr/libexec/8ax/v5_wcs_status_codec.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_codec=1
      ;;
    /usr/libexec/8ax/v5_remote_ui_relay.py)
      manifest_shm_abi_relay=1
      ;;
    /usr/libexec/8ax/v5_remote_ui_relay_access.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_relay_access=1
      ;;
    /usr/libexec/8ax/v5_remote_ui_relay_stream.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_relay_stream=1
      ;;
    /usr/libexec/8ax/v5_ui_boot_ready.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_boot_ready=1
      ;;
    /usr/libexec/8ax/v5_ui_boot_inputs.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_boot_inputs=1
      ;;
    /usr/libexec/8ax/v5_ui_main_cache_contract.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_main_cache_contract=1
      ;;
    /usr/libexec/8ax/v5_status_shm_reader.py)
      manifest_shm_abi_touched=1
      manifest_shm_abi_python_reader=1
      ;;
    /etc/init.d/v5-position-status-publisher)
      manifest_shm_abi_position_init=1
      ;;
    /etc/init.d/v5-wcs-status-publisher)
      manifest_shm_abi_wcs_init=1
      ;;
    /etc/init.d/v5-state-publisher)
      manifest_shm_abi_state_init=1
      ;;
    /etc/init.d/v5-ui-relay)
      manifest_shm_abi_ui_init=1
      ;;
  esac
  case "$scope_destination" in
    /usr/local/sbin/v5_net_core.sh)
      manifest_cpu_policy_net_core=1
      ;;
    /usr/local/sbin/v5_net_cpu_policy.sh)
      manifest_cpu_policy_net_module=1
      ;;
    /etc/init.d/S99v5-net)
      manifest_cpu_policy_net_init=1
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
    /etc/init.d/v5-runtime-startup)
      manifest_cpu_policy_runtime_startup=1
      ;;
    *)
      manifest_cpu_policy_only=0
      ;;
  esac
  case "$scope_destination" in
    /etc/init.d/v5-runtime-startup)
      [ "$scope_kind:$scope_source:$scope_mode" = \
        "init:services/runtime_startup/init.d/v5-runtime-startup:0755" ] || {
        echo "Runtime-startup scope requires the registered init owner and mode 0755" >&2
        exit 6
      }
      ;;
    *)
      manifest_runtime_startup_only=0
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
    /usr/libexec/8ax/v5_position_status_publisher|\
    /usr/libexec/8ax/v5_polling_cadence.py|\
    /etc/init.d/v5-position-status-publisher)
      manifest_position_publisher=1
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_wcs_status_publisher.py|\
    /etc/init.d/v5-wcs-status-publisher)
      manifest_wcs_publisher=1
      ;;
    /usr/libexec/8ax/v5_polling_cadence.py|\
    /usr/libexec/8ax/v5_machine_status_projection.py|\
    /usr/libexec/8ax/v5_wcs_status_codec.py)
      manifest_position_publisher=1
      manifest_wcs_publisher=1
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_state_publisher|\
    /etc/init.d/v5-state-publisher)
      manifest_state_publisher=1
      ;;
    *)
      manifest_state_only=0
      ;;
  esac
  case "$scope_destination" in
    /usr/libexec/8ax/v5_position_status_publisher|\
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
    /usr/libexec/8ax/v5_status_shm_reader.py|\
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
      manifest_actiond_touched=1
      manifest_command_gate_only=0
      ;;
    /opt/8ax/v5/config/drive-profiles/*)
      manifest_ui_only=0
      manifest_actiond_touched=1
      manifest_drive_profiles=1
      manifest_command_gate_only=0
      ;;
    /opt/8ax/v5/config/settings/self_parameter_table.tsv|\
    /opt/8ax/v5/config/settings/drive_parameter_table.tsv|\
    /opt/8ax/phase0_bus5/settings_runtime.json)
      manifest_ui_only=0
      manifest_actiond_touched=1
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
    /opt/8ax/v5/linuxcnc/ini/v5_bus.ini|\
    /opt/8ax/v5/linuxcnc/ini/v5_pulse.ini|\
    /opt/8ax/v5/linuxcnc/ini/v5_local_shmem.nml)
      manifest_ui_only=0
      manifest_actiond_only=0
      manifest_command_gate_only=0
      ;;
    *)
      manifest_ui_only=0
      manifest_actiond_only=0
      manifest_settings_only=0
      manifest_command_gate_only=0
      ;;
  esac
done < "$manifest"

native_protocol_participants=$((
  manifest_native_protocol_command_gate +
  manifest_native_protocol_zero_client +
  linuxcnc_bundle_enabled))
if [ "$native_protocol_participants" -ne 0 ] &&
   { [ "$manifest_native_protocol_command_gate" -ne 1 ] ||
     [ "$manifest_native_protocol_zero_client" -ne 1 ] ||
     [ "$linuxcnc_bundle_enabled" -ne 1 ]; }; then
  echo "native protocol deploy requires Command Gate server, Python zero client, and LinuxCNC owner/router as one atomic bundle" >&2
  exit 8
fi

if [ "$manifest_ethercat_master" -eq 1 ] &&
   [ "$manifest_ethercat_generic" -eq 1 ] &&
   [ "$manifest_ethercat_lcec" -eq 1 ] &&
   [ "$manifest_ethercat_lifecycle" -eq 1 ]; then
  manifest_ethercat_complete=1
fi
if [ "$manifest_ethercat_touched" -eq 1 ] &&
   [ "$manifest_ethercat_complete" -ne 1 ]; then
  echo "EtherCAT deploy requires the complete ec_master/ec_generic/lcec/lifecycle atomic bundle" >&2
  exit 8
fi

if [ "$manifest_shm_abi_ui_binary" -eq 1 ] &&
   [ "$manifest_shm_abi_state_binary" -eq 1 ] &&
   [ "$manifest_shm_abi_position_binary" -eq 1 ] &&
   [ "$manifest_shm_abi_wcs_script" -eq 1 ] &&
   [ "$manifest_shm_abi_cadence" -eq 1 ] &&
   [ "$manifest_shm_abi_projection" -eq 1 ] &&
   [ "$manifest_shm_abi_codec" -eq 1 ] &&
   [ "$manifest_shm_abi_relay" -eq 1 ] &&
   [ "$manifest_shm_abi_relay_access" -eq 1 ] &&
   [ "$manifest_shm_abi_relay_stream" -eq 1 ] &&
   [ "$manifest_shm_abi_boot_ready" -eq 1 ] &&
   [ "$manifest_shm_abi_boot_inputs" -eq 1 ] &&
   [ "$manifest_shm_abi_main_cache_contract" -eq 1 ] &&
   [ "$manifest_shm_abi_python_reader" -eq 1 ] &&
   [ "$manifest_shm_abi_position_init" -eq 1 ] &&
   [ "$manifest_shm_abi_wcs_init" -eq 1 ] &&
   [ "$manifest_shm_abi_state_init" -eq 1 ] &&
   [ "$manifest_shm_abi_ui_init" -eq 1 ]; then
  manifest_shm_abi_complete=1
fi
if [ "$manifest_shm_abi_touched" -eq 1 ] &&
   [ "$manifest_shm_abi_complete" -ne 1 ]; then
  echo "SHM ABI deploy requires the complete Position/State/UI atomic bundle" >&2
  exit 8
fi

if [ "$manifest_bus_cycle_ini" -eq 1 ] &&
   [ "$manifest_bus_cycle_hal" -eq 1 ] &&
   [ "$manifest_bus_cycle_xml" -eq 1 ] &&
   [ "$manifest_bus_cycle_readiness_probe" -eq 1 ] &&
   [ "$manifest_bus_cycle_command_gate_init" -eq 1 ] &&
   [ "$manifest_shm_abi_ui_binary" -eq 1 ] &&
   [ "$manifest_shm_abi_complete" -eq 1 ] &&
   [ "$manifest_ethercat_complete" -eq 1 ] &&
   [ "$linuxcnc_bundle_enabled" -eq 1 ]; then
  manifest_bus_cycle_complete=1
fi
if [ "$manifest_bus_cycle_touched" -eq 1 ] &&
   [ "$manifest_bus_cycle_complete" -ne 1 ]; then
  echo "BUS 1ms cycle deploy requires INI/HAL/XML, current LinuxCNC owner, complete EtherCAT, readiness/Command Gate, and UI/SHM atomic domains" >&2
  exit 8
fi

case "$restart_scope_requested" in
  auto)
    if [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
       [ "$manifest_shm_abi_complete" -eq 1 ] &&
       [ "$manifest_row_count" -eq "$manifest_shm_abi_required_rows" ]; then
      restart_scope=shm_abi
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
       [ "$manifest_row_count" -gt 0 ] &&
       [ "$manifest_gcode_only" -eq 1 ]; then
      restart_scope=gcode
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
       [ "$manifest_row_count" -gt 0 ] &&
       [ "$manifest_ui_only" -eq 1 ]; then
      restart_scope=ui
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_state_only" -eq 1 ]; then
      restart_scope=state
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
         [ "$manifest_row_count" -eq "$manifest_ethercat_required_rows" ] &&
         [ "$manifest_ethercat_only" -eq 1 ] &&
         [ "$manifest_ethercat_complete" -eq 1 ]; then
      restart_scope=ethercat
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_wcs_only" -eq 1 ]; then
      restart_scope=wcs
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -eq 1 ] &&
         [ "$manifest_runtime_startup_only" -eq 1 ]; then
      restart_scope=runtime_startup
    elif [ "$linuxcnc_bundle_enabled" -eq 0 ] &&
         [ "$manifest_row_count" -gt 0 ] &&
         [ "$manifest_cpu_policy_only" -eq 1 ] &&
         [ "$manifest_cpu_policy_net_core" -eq 1 ] &&
         [ "$manifest_cpu_policy_net_module" -eq 1 ] &&
         [ "$manifest_cpu_policy_net_init" -eq 1 ] &&
         [ "$manifest_cpu_policy_usb_wifi" -eq 1 ] &&
         [ "$manifest_cpu_policy_relay_payload" -eq 1 ] &&
         [ "$manifest_cpu_policy_command_gate" -eq 1 ] &&
         [ "$manifest_cpu_policy_ui" -eq 1 ] &&
       [ "$manifest_cpu_policy_state" -eq 1 ] &&
       [ "$manifest_cpu_policy_wcs" -eq 1 ] &&
       [ "$manifest_cpu_policy_position" -eq 1 ] &&
       [ "$manifest_cpu_policy_runtime_startup" -eq 1 ]; then
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
  shm_abi)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_shm_abi_complete" -ne 1 ] ||
       [ "$manifest_row_count" -ne "$manifest_shm_abi_required_rows" ]; then
      echo "SHM-ABI restart scope requires exactly the complete Position/State/UI atomic bundle and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=shm_abi
    ;;
  state)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_state_only" -ne 1 ]; then
      echo "State-publisher restart scope requires a non-empty State-publisher-only manifest and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=state
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
  ethercat)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -ne "$manifest_ethercat_required_rows" ] ||
       [ "$manifest_ethercat_only" -ne 1 ] ||
       [ "$manifest_ethercat_complete" -ne 1 ]; then
      echo "EtherCAT restart scope requires exactly the registered ec_master/ec_generic/lcec/lifecycle bundle" >&2
      exit 8
    fi
    restart_scope=ethercat
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
  runtime_startup)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -ne 1 ] ||
       [ "$manifest_runtime_startup_only" -ne 1 ]; then
      echo "Runtime-startup restart scope requires exactly the registered runtime-startup init row and no LinuxCNC bundle" >&2
      exit 8
    fi
    restart_scope=runtime_startup
    ;;
  cpu_policy)
    if [ "$linuxcnc_bundle_enabled" -ne 0 ] ||
       [ "$manifest_row_count" -eq 0 ] ||
       [ "$manifest_cpu_policy_only" -ne 1 ] ||
       [ "$manifest_cpu_policy_net_core" -ne 1 ] ||
       [ "$manifest_cpu_policy_net_module" -ne 1 ] ||
       [ "$manifest_cpu_policy_net_init" -ne 1 ] ||
       [ "$manifest_cpu_policy_usb_wifi" -ne 1 ] ||
       [ "$manifest_cpu_policy_relay_payload" -ne 1 ] ||
       [ "$manifest_cpu_policy_command_gate" -ne 1 ] ||
       [ "$manifest_cpu_policy_ui" -ne 1 ] ||
       [ "$manifest_cpu_policy_state" -ne 1 ] ||
       [ "$manifest_cpu_policy_wcs" -ne 1 ] ||
       [ "$manifest_cpu_policy_position" -ne 1 ] ||
       [ "$manifest_cpu_policy_runtime_startup" -ne 1 ]; then
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
    echo "unsupported V5_RUNTIME_RESTART_SCOPE: $restart_scope_requested (expected auto, gcode, ui, shm_abi, state, actiond, command_gate, backend, ethercat, wcs, runtime_startup, cpu_policy, settings, or all)" >&2
    exit 8
    ;;
esac
echo "V5_RUNTIME_RESTART_SCOPE scope=$restart_scope rows=$manifest_row_count shm_abi_touched=$manifest_shm_abi_touched shm_abi_complete=$manifest_shm_abi_complete ethercat_touched=$manifest_ethercat_touched ethercat_complete=$manifest_ethercat_complete bus_cycle_touched=$manifest_bus_cycle_touched bus_cycle_complete=$manifest_bus_cycle_complete gcode_only=$manifest_gcode_only ui_only=$manifest_ui_only state_only=$manifest_state_only actiond_only=$manifest_actiond_only command_gate_only=$manifest_command_gate_only backend_only=$manifest_backend_only wcs_only=$manifest_wcs_only runtime_startup_only=$manifest_runtime_startup_only cpu_policy_only=$manifest_cpu_policy_only settings_only=$manifest_settings_only"

stop_position_publisher_before_backend() {
  if [ -x /etc/init.d/v5-position-status-publisher ]; then
    /etc/init.d/v5-position-status-publisher stop
  fi
}

stop_wcs_publisher_before_backend() {
  [ -x /etc/init.d/v5-wcs-status-publisher ] || {
    echo "WCS status publisher init is missing before backend restart" >&2
    return 1
  }
  /etc/init.d/v5-wcs-status-publisher stop
}

stop_backend_publishers_before_backend() {
  stop_position_publisher_before_backend
  stop_wcs_publisher_before_backend
}

stop_ethercat_modules_before_install() {
  [ -x /etc/init.d/v5-linuxcnc-command-gate ] || {
    echo "LinuxCNC command-gate init is missing before EtherCAT module deploy" >&2
    return 1
  }
  [ -x /etc/init.d/ethercat ] || {
    echo "EtherCAT init is missing before module deploy" >&2
    return 1
  }
  stop_backend_publishers_before_backend
  if ! /etc/init.d/v5-linuxcnc-command-gate stop; then
    if grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules; then
      echo "Command Gate stop failed while EtherCAT modules remained loaded" >&2
      return 1
    fi
    echo "Command Gate stop reported failure after the backend was already unloaded; continuing verified idempotent teardown"
  fi
  for process in rtapi_app linuxcncsvr milltask io linuxcncrsh v5_command_gate_server; do
    if pidof "$process" >/dev/null 2>&1; then
      echo "LinuxCNC/Command Gate process remained active before atomic replacement: $process" >&2
      return 1
    fi
  done
  /etc/init.d/ethercat stop
  if grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules; then
    echo "EtherCAT kernel modules remained loaded before atomic replacement" >&2
    return 1
  fi
}

restart_runtime_event_dag() {
  [ -x /etc/init.d/v5-runtime-startup ] || {
    echo "runtime event DAG owner is missing after install" >&2
    return 1
  }
  /etc/init.d/v5-runtime-startup restart
}

wait_publisher_actual_barrier() {
  active_ini=""
  record_active_ini() {
    found="$1"
    case "$active_ini:$found" in
      :bus) active_ini=bus ;;
      :pulse) active_ini=pulse ;;
      bus:bus|pulse:pulse) ;;
      *) active_ini=conflict ;;
    esac
  }
  for cmdline in "$PROC_ROOT"/[0-9]*/cmdline; do
    [ -r "$cmdline" ] || continue
    command=$(tr '\000' ' ' <"$cmdline" 2>/dev/null || true)
    case "$command" in
      *milltask*"$RUNTIME_PROJECT_ROOT/linuxcnc/ini/v5_bus.ini"*) record_active_ini bus ;;
      *milltask*"$RUNTIME_PROJECT_ROOT/linuxcnc/ini/v5_pulse.ini"*) record_active_ini pulse ;;
    esac
  done
  case "$active_ini" in
    bus) expected_ini="$RUNTIME_PROJECT_ROOT/linuxcnc/ini/v5_bus.ini" ;;
    pulse)
      echo "publisher actual barrier rejects disabled Pulse runtime mode" >&2
      return 1
      ;;
    conflict)
      echo "publisher actual barrier found conflicting BUS/Pulse motion owners" >&2
      return 1
      ;;
    *)
      echo "publisher actual barrier found no canonical BUS/Pulse motion owner" >&2
      return 1
      ;;
  esac
  [ -x "$PUBLISHER_ACTUAL_BARRIER" ] || {
    echo "publisher actual barrier is missing: $PUBLISHER_ACTUAL_BARRIER" >&2
    return 1
  }
  "$PUBLISHER_ACTUAL_BARRIER" \
    --pre-ui-inputs --expected-ini "$expected_ini" \
    --publisher-snapshot-path "$PUBLISHER_SNAPSHOT_PATH" \
    --timeout 120
}

writer_pid_matches_path() {
  writer_pid="$1"
  writer_path="$2"
  [ -r "$PROC_ROOT/$writer_pid/cmdline" ] || return 1
  tr '\000' '\n' <"$PROC_ROOT/$writer_pid/cmdline" | grep -Fqx "$writer_path"
}

stop_writer_before_upgrade() {
  writer_service="$1"
  writer_path="$2"
  writer_init="/etc/init.d/$writer_service"
  if [ -x "$writer_init" ]; then
    "$writer_init" stop || return 1
  fi
  for writer_cmdline in "$PROC_ROOT"/[0-9]*/cmdline; do
    [ -r "$writer_cmdline" ] || continue
    writer_pid=${writer_cmdline#"$PROC_ROOT"/}
    writer_pid=${writer_pid%/cmdline}
    writer_pid_matches_path "$writer_pid" "$writer_path" || continue
    kill "$writer_pid" || return 1
    writer_wait=0
    while writer_pid_matches_path "$writer_pid" "$writer_path" && [ "$writer_wait" -lt 20 ]; do
      writer_wait=$((writer_wait + 1))
      sleep 0.1
    done
    if writer_pid_matches_path "$writer_pid" "$writer_path"; then
      echo "writer did not stop before upgrade: $writer_path pid=$writer_pid" >&2
      return 1
    fi
  done
}

stop_affected_writers_before_install() {
  if [ "$manifest_position_publisher" -eq 1 ]; then
    stop_writer_before_upgrade \
      v5-position-status-publisher \
      /usr/libexec/8ax/v5_position_status_publisher
  fi
  if [ "$manifest_wcs_publisher" -eq 1 ]; then
    stop_writer_before_upgrade \
      v5-wcs-status-publisher \
      /usr/libexec/8ax/v5_wcs_status_publisher.py
  fi
  if [ "$manifest_state_publisher" -eq 1 ]; then
    stop_writer_before_upgrade \
      v5-state-publisher \
      /usr/libexec/8ax/v5_state_publisher
  fi
}

stop_settings_actiond_before_install() {
  stop_writer_before_upgrade \
    v5-settings-actiond \
    /usr/libexec/8ax/drive_profile/v5_settings_actiond.py
}

stop_shm_abi_domain_before_install() {
  if [ -x /etc/init.d/v5-ui-relay ]; then
    /etc/init.d/v5-ui-relay stop
  fi
  stop_writer_before_upgrade \
    v5-state-publisher \
    /usr/libexec/8ax/v5_state_publisher
  stop_writer_before_upgrade \
    v5-wcs-status-publisher \
    /usr/libexec/8ax/v5_wcs_status_publisher.py
  stop_writer_before_upgrade \
    v5-position-status-publisher \
    /usr/libexec/8ax/v5_position_status_publisher
}

wait_position_shm_abi_readback() {
  /usr/bin/python3 - <<'PY'
import mmap
import struct
import time
from pathlib import Path

path = Path("/dev/shm/v5_native_position_status.bin")
deadline = time.monotonic() + 30.0
last_error = "not_read"

def fnv1a(payload):
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

while time.monotonic() < deadline:
    try:
        if path.stat().st_size != 256:
            raise RuntimeError("size")
        with path.open("rb") as handle:
            with mmap.mmap(handle.fileno(), 256, access=mmap.ACCESS_READ) as page:
                before = struct.unpack_from("<I", page, 24)[0]
                payload = page[:256]
                after = struct.unpack_from("<I", page, 24)[0]
        (magic, version, size, valid_mask, _axes, writer_identity,
         sequence, _reserved, source_time, source_generation) = (
            struct.unpack_from("<8IQQ", payload, 0))
        unit_per_count = struct.unpack_from("<5d", payload, 128)
        following_error = struct.unpack_from("<5d", payload, 168)
        display_digits = struct.unpack_from("<5B", payload, 208)
        expected_crc = struct.unpack_from("<I", payload, 248)[0]
        age_ns = time.monotonic_ns() - source_time
        if before != after or sequence != before or sequence == 0 or sequence & 1:
            raise RuntimeError("seqlock")
        if (magic, version, size) != (0x56504F53, 3, 256):
            raise RuntimeError("header")
        if writer_identity == 0 or source_generation == 0 or source_time == 0:
            raise RuntimeError("identity")
        if valid_mask & 0x3 != 0x3:
            raise RuntimeError("valid_mask")
        if (not all(value > 0.0 for value in unit_per_count) or
                display_digits != (3, 3, 3, 3, 3) or
                not all(value == value for value in following_error)):
            raise RuntimeError("display_metadata")
        if fnv1a(payload[:248]) != expected_crc:
            raise RuntimeError("crc")
        if age_ns < 0 or age_ns > 2_000_000_000:
            raise RuntimeError("freshness")
        print(
            "V5_POSITION_ABI_READBACK_OK "
            f"magic=0x{magic:08x} version={version} size={size} "
            f"writer_identity={writer_identity} seq={sequence} "
            f"source_generation={source_generation} age_ns={age_ns} "
            f"valid_mask=0x{valid_mask:x} crc=ok")
        raise SystemExit(0)
    except (OSError, RuntimeError, struct.error, ValueError) as exc:
        last_error = str(exc)
        time.sleep(0.05)
raise SystemExit("V5_POSITION_ABI_READBACK_FAILED " + last_error)
PY
}

wait_state_shm_abi_readback() {
  /usr/bin/python3 - <<'PY'
import mmap
import struct
import time
import zlib
from pathlib import Path

path = Path("/dev/shm/v3_status_shm")
deadline = time.monotonic() + 30.0
last_error = "not_read"
required_mask = (1 << 0) | (1 << 1) | (1 << 8) | (1 << 9)
while time.monotonic() < deadline:
    try:
        if path.stat().st_size != 7136:
            raise RuntimeError("size")
        with path.open("rb") as handle:
            with mmap.mmap(handle.fileno(), 7136, access=mmap.ACCESS_READ) as page:
                before = struct.unpack_from("<I", page, 24)[0]
                payload = page[:7136]
                after = struct.unpack_from("<I", page, 24)[0]
        (magic, version, header_size, total_size, payload_size, _flags,
         sequence, expected_crc) = struct.unpack_from("<8I", payload, 0)
        status_epoch = struct.unpack_from("<Q", payload, 32)[0]
        valid_mask = struct.unpack_from("<I", payload, 40)[0]
        writer_identity = struct.unpack_from("<I", payload, 48)[0]
        source_time = struct.unpack_from("<Q", payload, 56)[0]
        source_generation = struct.unpack_from("<Q", payload, 64)[0]
        scene_generation = struct.unpack_from("<Q", payload, 72)[0]
        unit_per_count = struct.unpack_from("<5d", payload, 160)
        following_error = struct.unpack_from("<5d", payload, 200)
        display_digits = struct.unpack_from("<5B", payload, 240)
        cpu_generation = struct.unpack_from("<Q", payload, 944)[0]
        cpu_time = struct.unpack_from("<Q", payload, 952)[0]
        scene_build = struct.unpack_from("<Q", payload, 1024)[0]
        scene_flags = struct.unpack_from("<I", payload, 1052)[0]
        contour_error = struct.unpack_from("<d", payload, 7128)[0]
        age_ns = time.monotonic_ns() - source_time
        actual_crc = zlib.crc32(payload[:24])
        actual_crc = zlib.crc32(payload[32:], actual_crc) & 0xffffffff
        if before != after or sequence != before or sequence == 0 or sequence & 1:
            raise RuntimeError("seqlock")
        if (magic, version, header_size, total_size, payload_size) != (
                0x56355348, 4, 7136, 7136, 7104):
            raise RuntimeError("header")
        if (valid_mask & required_mask) != required_mask:
            raise RuntimeError("valid_mask")
        if (writer_identity == 0 or status_epoch == 0 or source_time == 0 or
                source_generation == 0 or scene_generation == 0 or
                cpu_generation == 0 or cpu_time == 0 or
                scene_build == 0 or not (scene_flags & 1)):
            raise RuntimeError("identity")
        if scene_flags & (1 << 5) and (
                not math.isfinite(contour_error) or contour_error < 0.0):
            raise RuntimeError("contour_error")
        if (not all(value > 0.0 for value in unit_per_count) or
                display_digits != (3, 3, 3, 3, 3) or
                not all(value == value for value in following_error)):
            raise RuntimeError("display_metadata")
        if actual_crc != expected_crc:
            raise RuntimeError("crc")
        if age_ns < 0 or age_ns > 2_000_000_000:
            raise RuntimeError("freshness")
        print(
            "V5_STATE_ABI_READBACK_OK "
            f"magic=0x{magic:08x} version={version} size={total_size} "
            f"writer_identity={writer_identity} seq={sequence} "
            f"source_generation={source_generation} "
            f"scene_generation={scene_generation} "
            f"scene_build={scene_build} "
            f"cpu_generation={cpu_generation} age_ns={age_ns} "
            f"valid_mask=0x{valid_mask:x} crc=ok")
        raise SystemExit(0)
    except (OSError, RuntimeError, struct.error, ValueError) as exc:
        last_error = str(exc)
        time.sleep(0.05)
raise SystemExit("V5_STATE_ABI_READBACK_FAILED " + last_error)
PY
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
fi
if [ "$apply" -eq 1 ]; then
  if [ "$manifest_shm_abi_complete" -eq 1 ]; then
    stop_shm_abi_domain_before_install
  else
    stop_affected_writers_before_install
  fi
  if [ "$manifest_actiond_touched" -eq 1 ] ||
     [ "$manifest_bus_cycle_touched" -eq 1 ]; then
    stop_settings_actiond_before_install
  fi
fi
if [ "$apply" -eq 1 ] && [ "$manifest_ethercat_complete" -eq 1 ]; then
  stop_ethercat_modules_before_install
elif [ "$apply" -eq 1 ] &&
     { [ "$linuxcnc_bundle_enabled" -eq 1 ] ||
       [ "$restart_scope" = "backend" ] ||
       [ "$restart_scope" = "settings" ]; }; then
  [ -x /etc/init.d/v5-linuxcnc-command-gate ] || {
    echo "LinuxCNC command-gate init is missing before deploy" >&2
    exit 7
  }
  stop_backend_publishers_before_backend
  /etc/init.d/v5-linuxcnc-command-gate stop
fi
if [ "$apply" -eq 1 ] && [ "$linuxcnc_bundle_enabled" -eq 1 ]; then
  install_linuxcnc_deploy_bundle
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
      "config/settings/drive_parameter_table.tsv:/opt/8ax/v5/config/settings/drive_parameter_table.tsv:0644") ;;
      *)
        echo "runtime_seed_merge is only allowed for registered parameter templates: $source -> $destination mode=$mode" >&2
        exit 6
        ;;
    esac
  fi
  if [ "$kind" = "runtime_ini_cycle_merge" ]; then
    case "$source:$destination:$mode" in
      "linuxcnc/ini/v5_bus.ini:/opt/8ax/v5/linuxcnc/ini/v5_bus.ini:0644") ;;
      *)
        echo "runtime_ini_cycle_merge is only allowed for the registered BUS INI cycle owner: $source -> $destination mode=$mode" >&2
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
  if [ "$kind" = "runtime_ini_cycle_merge" ]; then
    merge_runtime_bus_ini_cycle "$source_path" "$destination" "$mode"
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
if [ "$apply" -eq 1 ] && [ "$manifest_shm_abi_position_binary" -eq 1 ]; then
  rm -f /usr/libexec/8ax/v5_position_status_publisher.py
  rm -f /usr/libexec/8ax/__pycache__/v5_position_status_publisher.*.pyc
fi
if [ "$apply" -eq 1 ] && [ "$manifest_ethercat_complete" -eq 1 ]; then
  [ -x /sbin/depmod ] || {
    echo "required module dependency tool is missing: /sbin/depmod" >&2
    exit 7
  }
  /sbin/depmod -a
fi

enable_auxiliary_boot_service() {
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

disable_boot_service() {
  name="$1"
  for level in 0 1 2 3 4 5 6; do
    dir="/etc/rc${level}.d"
    [ -d "$dir" ] || continue
    rm -f "$dir"/S??"$name" "$dir"/K??"$name"
  done
}

enable_runtime_startup_boot_graph() {
  [ -x /etc/init.d/v5-runtime-startup ] || {
    echo "runtime startup orchestrator is missing" >&2
    return 1
  }
  for service in v5-linuxcnc-command-gate v5-position-status-publisher \
      v5-wcs-status-publisher v5-state-publisher v5-ui-relay v5-settings-actiond; do
    disable_boot_service "$service"
  done
  enable_auxiliary_boot_service v5-runtime-startup 05 14
}

disable_unconditional_ethercat_autostart() {
  for level in 2 3 4 5; do
    dir="/etc/rc${level}.d"
    [ -d "$dir" ] || continue
    rm -f "$dir"/S??ethercat
  done
}

enable_auxiliary_boot_services() {
  enable_auxiliary_boot_service v5-touch-diagnostics 97 13
  enable_auxiliary_boot_service v5-remote-ssh 98 12
}

apply_cpu_policy_after_install() {
  [ "$manifest_cpu_policy_net_core" -eq 1 ] &&
    [ "$manifest_cpu_policy_net_module" -eq 1 ] &&
    [ "$manifest_cpu_policy_net_init" -eq 1 ] &&
    [ "$manifest_cpu_policy_runtime_startup" -eq 1 ] || {
      echo "installed CPU policy is incomplete" >&2
      return 1
    }
  /usr/bin/taskset -c 1 /bin/sh -c '
    LOG=/run/8ax/v5_cpu_policy.log
    . /usr/local/sbin/v5_net_core.sh
    apply_network_cpu_isolation && enforce_dropbear_cpu1_affinity
  '
}

cpu_policy_manifest_touched() {
  [ "$manifest_cpu_policy_net_core" -eq 1 ] ||
    [ "$manifest_cpu_policy_net_module" -eq 1 ] ||
    [ "$manifest_cpu_policy_net_init" -eq 1 ] ||
    [ "$manifest_cpu_policy_relay_payload" -eq 1 ] ||
    [ "$manifest_cpu_policy_command_gate" -eq 1 ] ||
    [ "$manifest_cpu_policy_ui" -eq 1 ] ||
    [ "$manifest_cpu_policy_state" -eq 1 ] ||
    [ "$manifest_cpu_policy_wcs" -eq 1 ] ||
    [ "$manifest_cpu_policy_position" -eq 1 ] ||
    [ "$manifest_cpu_policy_runtime_startup" -eq 1 ]
}

if [ "$apply" -eq 1 ] && [ "$manifest_cpu_policy_command_gate" -eq 1 ]; then
  disable_unconditional_ethercat_autostart
fi

start_shm_abi_domain_after_install() {
  if ! pidof rtapi_app >/dev/null 2>&1; then
    [ -x /etc/init.d/v5-linuxcnc-command-gate ] || {
      echo "LinuxCNC command-gate init is missing before SHM domain recovery" >&2
      return 1
    }
    /etc/init.d/v5-linuxcnc-command-gate start
    pidof rtapi_app >/dev/null 2>&1 || {
      echo "LinuxCNC realtime owner did not recover before SHM domain start" >&2
      return 1
    }
  fi
  /etc/init.d/v5-position-status-publisher start
  wait_position_shm_abi_readback
  /etc/init.d/v5-wcs-status-publisher start
  /etc/init.d/v5-state-publisher start
  wait_state_shm_abi_readback
  wait_publisher_actual_barrier
  /etc/init.d/v5-ui-relay start
  echo "V5_SHM_ABI_ATOMIC_RESTART_OK scope=position,state,ui-relay"
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

cleanup_retired_bus_cycle_files() {
  retired_root="${1:-}"
  case "$retired_root" in
    ""|/*) ;;
    *)
      echo "retired BUS cycle cleanup root must be empty or absolute: $retired_root" >&2
      return 1
      ;;
  esac
  for retired_relative in \
    /opt/8ax/v5/linuxcnc/hal/v5_bus_2ms.hal \
    /opt/8ax/v5/linuxcnc/hal/ethercat-conf-2ms.xml
  do
    retired_path="$retired_root$retired_relative"
    parameter_table_transaction_snapshot "$retired_path" || return 1
    rm -f -- "$retired_path" || return 1
    if [ -e "$retired_path" ] || [ -L "$retired_path" ]; then
      echo "retired BUS cycle file removal failed: $retired_path" >&2
      return 1
    fi
  done
}

cleanup_retired_drive_tuning_files() {
  retired_root="${1:-}"
  case "$retired_root" in
    ""|/*) ;;
    *)
      echo "refusing unsafe retired drive tuning root: $retired_root" >&2
      return 1
      ;;
  esac
  settings_runtime="$retired_root/opt/8ax/phase0_bus5/settings_runtime.json"
  if [ -f "$settings_runtime" ]; then
    command -v python3 >/dev/null 2>&1 || {
      echo "python3 required to migrate retired drive tuning evidence" >&2
      return 1
    }
    parameter_table_transaction_snapshot "$settings_runtime" || return 1
    python3 - "$settings_runtime" <<'PY'
import json
import os
import stat
import sys
from pathlib import Path

path = Path(sys.argv[1])
payload = json.loads(path.read_text(encoding="utf-8"))
if payload.get("schema") != "re.v3.settings_runtime.drive_only.v1":
    raise SystemExit("unexpected settings_runtime schema during retired drive tuning migration")
if not isinstance(payload.get("axes"), list):
    raise SystemExit("settings_runtime axes missing during retired drive tuning migration")

removed = 0
def migrate(node):
    global removed
    if isinstance(node, dict):
        if "velocity_feedforward_evidence" in node:
            del node["velocity_feedforward_evidence"]
            removed += 1
        for value in node.values():
            migrate(value)
    elif isinstance(node, list):
        for value in node:
            migrate(value)

migrate(payload)
if removed:
    original = path.stat()
    temporary = path.with_name(path.name + ".retired-drive-tuning.tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, stat.S_IMODE(original.st_mode))
        os.replace(temporary, path)
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY)
        except OSError:
            directory_fd = None
        if directory_fd is not None:
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
print("retired drive tuning evidence removed=%d path=%s" % (removed, path))
PY
  fi
  rm -f \
    "$retired_root/usr/libexec/8ax/drive_profile/v5_drive_feedforward_action.py" \
    "$retired_root/usr/libexec/8ax/drive_profile/v5_drive_feedforward_recovery.py" \
    "$retired_root/run/8ax_v5_drive_profile/drive_feedforward_result.json"
  rm -f \
    "$retired_root/usr/libexec/8ax/drive_profile/__pycache__"/v5_drive_feedforward_action.*.pyc \
    "$retired_root/usr/libexec/8ax/drive_profile/__pycache__"/v5_drive_feedforward_recovery.*.pyc
}

cleanup_retired_runtime_files() {
  cleanup_retired_drive_tuning_files
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
  cleanup_retired_bus_cycle_files
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_calibration.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_restart.py
  rm -f /opt/8ax/tools/v5_touch_calibration/v5_touch_window_runtime.py
  rm -f /usr/libexec/8ax/v5_rtcp_status_publisher.py
  rm -f /usr/libexec/8ax/v5_g53_geometry_memory_owner.py
  rm -f /usr/libexec/8ax/v5_native_safety_latch_owner.py
  rm -f /usr/libexec/8ax/v5_ui_cache_queue_contract.py
  rm -f /usr/libexec/8ax/drive_profile/v5_bus_zero_resident_gate.py
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
  enable_runtime_startup_boot_graph
  if [ "$restart_scope" = "gcode" ]; then
    rm -f /opt/8ax/v5/gcode/golden/cc.ngc
  elif [ "$restart_scope" = "ui" ]; then
    rm -f /usr/libexec/8ax/v5_ui_cache_queue_contract.py
    /etc/init.d/v5-ui-relay restart
  elif [ "$restart_scope" = "shm_abi" ]; then
    start_shm_abi_domain_after_install
  elif [ "$restart_scope" = "state" ]; then
    /etc/init.d/v5-state-publisher restart
  elif [ "$restart_scope" = "actiond" ]; then
    cleanup_retired_drive_tuning_files
    [ "$manifest_drive_profiles" -eq 0 ] || install_runtime_drive_profiles
    parameter_table_transaction_complete
    /etc/init.d/v5-settings-actiond restart
  elif [ "$restart_scope" = "command_gate" ]; then
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "backend" ]; then
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "ethercat" ]; then
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "wcs" ]; then
    /etc/init.d/v5-position-status-publisher restart
    /etc/init.d/v5-wcs-status-publisher restart
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "runtime_startup" ]; then
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "cpu_policy" ]; then
    apply_cpu_policy_after_install
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  elif [ "$restart_scope" = "settings" ]; then
    cleanup_retired_drive_tuning_files
    [ "$manifest_drive_profiles" -eq 0 ] || install_runtime_drive_profiles
    parameter_table_transaction_complete
    restart_runtime_event_dag
    wait_publisher_actual_barrier
  else
    enable_auxiliary_boot_services
    cleanup_retired_runtime_files
    install_runtime_drive_profiles
    if cpu_policy_manifest_touched; then
      apply_cpu_policy_after_install
    fi
    parameter_table_transaction_complete
    restart_runtime_event_dag
    wait_publisher_actual_barrier
    /etc/init.d/v5-touch-diagnostics restart
    /etc/init.d/v5-remote-ssh restart
  fi
  parameter_table_transaction_complete
else
  echo "dry-run only; pass --apply to install files and restart scope=$restart_scope"
fi
