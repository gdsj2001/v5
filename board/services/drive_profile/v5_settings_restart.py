from __future__ import annotations

import copy
import json
import re
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List, Set

import v5_drive_bus_contract as drive_contract
from v5_drive_bus_contract import DriveActionError, finite_float
from v5_drive_parameter_table import normalize_self_slave_binding, read_parameter_tsv
from v5_drive_runtime_store import (
    find_runtime_axis,
    persist_settings_runtime,
    validate_settings_runtime_drive_only,
    write_text_atomic,
)
from v5_settings_action_contract import CANONICAL_CLEAN_RESTART_SERVICES, RUN_DIR


RESTART_HANDOFF_DELAY_S = 1.0
RESTART_GATE_POLL_S = 0.1
RESTART_GATE_TIMEOUT_POLLS = 100
ROTARY_MODEL_AXES = ("A", "B")


def restart_handoff_paths() -> tuple[Path, Path, Path, Path]:
    return (
        RUN_DIR / "settings_clean_restart_handoff.sh",
        RUN_DIR / "settings_clean_restart_handoff.log",
        RUN_DIR / "settings_clean_restart_commit.armed",
        RUN_DIR / "settings_clean_restart_commit.go",
    )


def remove_runtime_marker(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _active_rotary_axis_from_runtime_ini() -> str:
    if not drive_contract.RUNTIME_SETTINGS_INI.is_file():
        raise DriveActionError(
            "SETTINGS_MODEL_RUNTIME_INI_MISSING",
            "运行参数文件不存在，无法在重启前校验旋转轴零点绑定。",
            str(drive_contract.RUNTIME_SETTINGS_INI),
        )
    section = ""
    coordinates = ""
    for raw in drive_contract.RUNTIME_SETTINGS_INI.read_text(
            encoding="utf-8", errors="ignore").splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().upper()
            continue
        if section == "TRAJ" and "=" in line:
            key, value = line.split("=", 1)
            if key.strip().upper() == "COORDINATES":
                coordinates = value.strip().upper()
                break
    axes = re.findall(r"[XYZABCUVW]", coordinates)
    active = [axis for axis in ROTARY_MODEL_AXES if axis in axes]
    if len(active) != 1:
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_COORDINATES_INVALID",
            "运行参数中的 AC/BC 旋转轴无法唯一确定，系统未重启。",
            {"coordinates": coordinates, "active_rotary_axes": active},
        )
    return active[0]


def _active_rotary_axes_from_runtime_ini() -> tuple[str, str]:
    primary = _active_rotary_axis_from_runtime_ini()
    text = drive_contract.RUNTIME_SETTINGS_INI.read_text(
        encoding="utf-8", errors="ignore")
    coordinates = ""
    section = ""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().upper()
        elif section == "TRAJ" and "=" in line:
            key, value = line.split("=", 1)
            if key.strip().upper() == "COORDINATES":
                coordinates = value.upper()
                break
    if "C" not in re.findall(r"[XYZABCUVW]", coordinates):
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_C_AXIS_MISSING",
            "Active AC/BC model does not contain the C rotary axis.",
            {"coordinates": coordinates})
    return primary, "C"


def _runtime_ini_axis_scale(original: str, axis: str) -> tuple[float, str]:
    sections: Dict[str, Dict[str, str]] = {}
    section = ""
    for raw in original.splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().upper()
            sections.setdefault(section, {})
            continue
        if section and "=" in line:
            key, value = line.split("=", 1)
            sections[section][key.strip().upper()] = value.strip()
    coordinates = re.findall(
        r"[XYZABCUVW]", sections.get("TRAJ", {}).get("COORDINATES", "").upper())
    matches = [index for index, letter in enumerate(coordinates) if letter == axis]
    if len(matches) != 1:
        raise DriveActionError(
            "SETTINGS_ROTARY_PROFILE_JOINT_MAPPING_INVALID",
            "Active runtime INI cannot map the rotary axis to one joint.",
            {"axis": axis, "coordinates": coordinates, "matches": matches})
    joint = "JOINT_%d" % matches[0]
    scale = finite_float(sections.get(joint, {}).get("SCALE"))
    if scale is None or scale <= 0.0:
        raise DriveActionError(
            "SETTINGS_ROTARY_PROFILE_INI_SCALE_MISSING",
            "Active runtime INI does not contain a valid rotary joint SCALE.",
            {"axis": axis, "joint": joint, "scale": scale})
    return scale, joint


def sync_active_rotary_wcheckpoint_profiles_for_restart() -> Dict[str, Any]:
    active_axes = _active_rotary_axes_from_runtime_ini()
    original = drive_contract.RUNTIME_SETTINGS_INI.read_text(
        encoding="utf-8", errors="ignore")
    try:
        runtime = json.loads(
            drive_contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError(
            "SETTINGS_ROTARY_PROFILE_RUNTIME_OWNER_INVALID",
            "Drive runtime owner is unavailable; rotary Crev was not committed.",
            "%s: %s" % (type(exc).__name__, exc))
    validate_settings_runtime_drive_only(runtime)
    expected: Dict[str, int] = {}
    for axis in active_axes:
        axis_cfg, _axis_index = find_runtime_axis(runtime, axis)
        zero = axis_cfg.get("zero_model")
        counts_per_unit = finite_float(
            zero.get("counts_per_unit")) if isinstance(zero, dict) else None
        runtime_counts_per_rev = finite_float(
            axis_cfg.get("rotary_load_counts_per_rev"))
        if counts_per_unit is None or counts_per_unit <= 0.0:
            raise DriveActionError(
                "SETTINGS_ROTARY_PROFILE_SCALE_MISSING",
                "Rotary zero owner does not contain a valid counts_per_unit.",
                {"axis": axis, "counts_per_unit": counts_per_unit})
        runtime_scale, joint = _runtime_ini_axis_scale(original, axis)
        if abs(counts_per_unit - runtime_scale) > max(
                1.0e-6, abs(runtime_scale) * 1.0e-9):
            raise DriveActionError(
                "SETTINGS_ROTARY_PROFILE_SCALE_CHAIN_MISMATCH",
                "Rotary zero owner does not match active runtime INI SCALE.",
                {"axis": axis, "joint": joint,
                 "runtime_ini_scale": runtime_scale,
                 "zero_counts_per_unit": counts_per_unit})
        counts_per_rev_float = counts_per_unit * 360.0
        counts_per_rev = int(round(counts_per_rev_float))
        if (counts_per_rev <= 0 or
                abs(counts_per_rev_float - counts_per_rev) > 1.0e-6 or
                runtime_counts_per_rev is None or
                abs(runtime_counts_per_rev - counts_per_rev) > 1.0e-6):
            raise DriveActionError(
                "SETTINGS_ROTARY_PROFILE_CREV_MISMATCH",
                "Rotary runtime Crev does not match active SCALE.",
                {"axis": axis, "counts_per_unit": counts_per_unit,
                 "expected_counts_per_rev": counts_per_rev,
                 "runtime_counts_per_rev": runtime_counts_per_rev})
        expected[axis] = counts_per_rev

    section = ""
    writes = {axis: 0 for axis in active_axes}
    out = []
    for raw in original.splitlines():
        stripped = raw.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].strip().upper()
            out.append(raw)
            continue
        axis = section[5:] if section.startswith("AXIS_") else ""
        if axis in expected and "=" in raw:
            key, _value = raw.split("=", 1)
            if key.strip().upper() == "WCHECKPOINT_COUNTS_PER_REV":
                out.append("%s = %d" % (key.strip(), expected[axis]))
                writes[axis] += 1
                continue
        out.append(raw)
    if any(count != 1 for count in writes.values()):
        raise DriveActionError(
            "SETTINGS_ROTARY_PROFILE_CREV_OWNER_MISSING",
            "Each active rotary axis requires one wcheckpoint Crev owner.",
            {"writes": writes})
    updated = "\n".join(out) + "\n"
    changed = updated != original
    if changed:
        write_text_atomic(drive_contract.RUNTIME_SETTINGS_INI, updated)
    return {
        "ok": True,
        "code": "SETTINGS_ROTARY_PROFILE_CREV_SYNCED" if changed else
                "SETTINGS_ROTARY_PROFILE_CREV_VALID",
        "active_axes": list(active_axes),
        "counts_per_rev": expected,
        "changed": changed,
    }


def _fresh_rotary_slave_bindings() -> Dict[str, str]:
    bindings: Dict[str, str] = {}
    for axis, field, value in read_parameter_tsv(drive_contract.SELF_PARAMETER_TABLE):
        axis_name = str(axis or "").upper()
        if axis_name in ROTARY_MODEL_AXES and field == "slave":
            bindings[axis_name] = normalize_self_slave_binding(axis_name, value)
    if set(bindings) != set(ROTARY_MODEL_AXES):
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_BINDINGS_MISSING",
            "A/B 旋转轴从站绑定不完整，系统未重启。",
            {"bindings": bindings},
        )
    return bindings


def _int_position(value: Any) -> int | None:
    text = str(value if value is not None else "").strip()
    if not re.fullmatch(r"\d+", text):
        return None
    return int(text)


def _zero_evidence_positions(zero_model: Dict[str, Any]) -> Set[int]:
    positions: Set[int] = set()
    direct = _int_position(zero_model.get("slave_position"))
    if direct is not None:
        positions.add(direct)
    drive_position = zero_model.get("drive_position")
    if not isinstance(drive_position, dict):
        return positions
    for key in ("position", "slave_position", "slave_index"):
        parsed = _int_position(drive_position.get(key))
        if parsed is not None:
            positions.add(parsed)
    readback = drive_position.get("readback")
    upload = readback.get("upload") if isinstance(readback, dict) else None
    argv: List[Any] = upload.get("argv", []) if isinstance(upload, dict) else []
    for index, token in enumerate(argv[:-1]):
        if str(token) == "-p":
            parsed = _int_position(argv[index + 1])
            if parsed is not None:
                positions.add(parsed)
    return positions


def migrate_active_rotary_drive_owner_for_restart() -> Dict[str, Any]:
    active_axis = _active_rotary_axis_from_runtime_ini()
    inactive_axis = "B" if active_axis == "A" else "A"
    bindings = _fresh_rotary_slave_bindings()
    active_position = _int_position(bindings.get(active_axis))
    if active_position is None or bindings.get(inactive_axis) != "NAT":
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_BINDING_CONFLICT",
            "当前 AC/BC 型号与 A/B 从站绑定不一致，系统未重启。",
            {"active_axis": active_axis, "bindings": bindings},
        )
    try:
        runtime = json.loads(
            drive_contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError(
            "SETTINGS_MODEL_RUNTIME_OWNER_INVALID",
            "驱动私域运行参数无法读取，系统未重启。",
            "%s: %s" % (type(exc).__name__, exc),
        )
    validate_settings_runtime_drive_only(runtime)
    axis_configs: Dict[str, Dict[str, Any]] = {}
    candidates: List[tuple[str, Dict[str, Any], Dict[str, Any], str, str]] = []
    for axis in ROTARY_MODEL_AXES:
        axis_cfg, _ = find_runtime_axis(runtime, axis)
        axis_configs[axis] = axis_cfg
        zero_model = axis_cfg.get("zero_model")
        if not isinstance(zero_model, dict):
            continue
        if _zero_evidence_positions(zero_model) != {active_position}:
            continue
        candidates.append((
            axis,
            axis_cfg,
            zero_model,
            str(zero_model.get("captured_at") or ""),
            str(zero_model.get("zero_run_id") or ""),
        ))
    if not candidates:
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_ZERO_EVIDENCE_MISSING",
            "当前物理从站尚未保存真实零点，系统未重启。",
            {"active_axis": active_axis, "slave_position": active_position},
        )
    candidates.sort(key=lambda item: (item[3], item[4]), reverse=True)
    newest_key = (candidates[0][3], candidates[0][4])
    newest = [
        item for item in candidates if (item[3], item[4]) == newest_key
    ]
    if len(newest) > 1:
        first_zero = json.dumps(
            newest[0][2], ensure_ascii=False, sort_keys=True)
        if any(
                json.dumps(item[2], ensure_ascii=False, sort_keys=True) !=
                first_zero for item in newest[1:]):
            raise DriveActionError(
                "SETTINGS_MODEL_ROTARY_ZERO_OWNER_AMBIGUOUS",
                "同一物理从站存在多个无法裁决的零点 owner，系统未重启。",
                {
                    "active_axis": active_axis,
                    "slave_position": active_position,
                    "candidate_axes": [item[0] for item in newest],
                    "captured_at": newest_key[0],
                    "zero_run_id": newest_key[1],
                },
            )
        donor = next(
            (item for item in newest if item[0] == active_axis), newest[0])
    else:
        donor = newest[0]
    donor_axis, donor_cfg, zero_model, _captured_at, _zero_run_id = donor
    zero_counts = finite_float(zero_model.get("zero_counts"))
    counts_per_unit = finite_float(zero_model.get("counts_per_unit"))
    raw_zero_position = finite_float(zero_model.get("raw_zero_position"))
    if (zero_counts is None or counts_per_unit is None or
            counts_per_unit == 0.0 or raw_zero_position is None):
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_ZERO_EVIDENCE_INCOMPLETE",
            "当前物理从站零点证据不完整，系统未重启。",
            {"active_axis": active_axis, "slave_position": active_position},
        )
    expected_raw = zero_counts / counts_per_unit
    if abs(raw_zero_position - expected_raw) > max(
            1.0e-9, abs(expected_raw) * 1.0e-9):
        raise DriveActionError(
            "SETTINGS_MODEL_ROTARY_ZERO_SCALE_MISMATCH",
            "当前物理从站零点 count 与比例证据不一致，系统未重启。",
            {
                "active_axis": active_axis,
                "slave_position": active_position,
                "zero_counts": zero_counts,
                "counts_per_unit": counts_per_unit,
                "raw_zero_position": raw_zero_position,
            },
        )
    drive_position = zero_model.get("drive_position")
    writeback = donor_cfg.get("zero_model_writeback")
    owner_is_canonical = (
        donor_axis == active_axis and
        set(axis_configs[inactive_axis].keys()) == {"axis"} and
        _int_position(zero_model.get("slave_position")) == active_position and
        (not isinstance(drive_position, dict) or (
            "axis" not in drive_position and
            _int_position(
                drive_position.get("slave_position")) == active_position)) and
        isinstance(writeback, dict) and
        writeback.get("code") ==
            "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_MIGRATED" and
        _int_position(writeback.get("slave_position")) == active_position)
    changed = not owner_is_canonical
    if changed:
        migrated_cfg = copy.deepcopy(donor_cfg)
        migrated_cfg["axis"] = active_axis
        migrated_zero = copy.deepcopy(zero_model)
        migrated_zero["slave_position"] = active_position
        migrated_drive_position = migrated_zero.get("drive_position")
        if isinstance(migrated_drive_position, dict):
            migrated_drive_position.pop("axis", None)
            migrated_drive_position["slave_position"] = active_position
        migrated_cfg["zero_model"] = migrated_zero
        drive_readback = migrated_cfg.get("drive_readback")
        if isinstance(drive_readback, dict):
            drive_readback["axis_name"] = active_axis
        migrated_cfg["zero_model_writeback"] = {
            "ok": True,
            "code": "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_MIGRATED",
            "written_at": now_utc(),
            "settings_runtime_json": str(
                drive_contract.SETTINGS_RUNTIME_JSON),
            "owner_kind": "physical_slave",
            "slave_position": active_position,
            "active_axis": active_axis,
            "donor_axis": donor_axis,
        }
        axes = runtime.get("axes", [])
        for index, item in enumerate(axes):
            item_axis = (
                str(item.get("axis") or "").upper()
                if isinstance(item, dict) else "")
            if item_axis == active_axis:
                axes[index] = migrated_cfg
            elif item_axis == inactive_axis:
                axes[index] = {"axis": inactive_axis}
        persist_settings_runtime(runtime)
        reread = json.loads(
            drive_contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
        reread_active, _ = find_runtime_axis(reread, active_axis)
        reread_inactive, _ = find_runtime_axis(reread, inactive_axis)
        reread_zero = (
            reread_active.get("zero_model")
            if isinstance(reread_active.get("zero_model"), dict) else {})
        if (_zero_evidence_positions(reread_zero) != {active_position} or
                "zero_model" in reread_inactive):
            raise DriveActionError(
                "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_READBACK_MISMATCH",
                "物理从站零点 owner 迁移后回读不一致，系统未重启。",
                {
                    "active_axis": active_axis,
                    "inactive_axis": inactive_axis,
                    "slave_position": active_position,
                },
            )
    return {
        "ok": True,
        "code": (
            "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_MIGRATED" if changed else
            "SETTINGS_MODEL_ROTARY_DRIVE_OWNER_VALID"),
        "axis": active_axis,
        "inactive_axis": inactive_axis,
        "slave_position": active_position,
        "donor_axis": donor_axis,
        "zero_counts": zero_counts,
        "counts_per_unit": counts_per_unit,
        "raw_zero_position": raw_zero_position,
        "changed": changed,
    }


def run_restart_handoff(action: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    try:
        rotary_drive_owner = migrate_active_rotary_drive_owner_for_restart()
        rotary_profile = sync_active_rotary_wcheckpoint_profiles_for_restart()
    except DriveActionError as exc:
        return {
            "schema": "v5.settings_action_result.v1",
            "generated_at": now_utc(),
            "action": action,
            "owner": spec.get("owner", ""),
            "ok": False,
            "code": exc.code,
            "message_cn": exc.message_cn,
            "detail": exc.detail,
            "write_executed": False,
            "motion_executed": False,
            "restart_executed": False,
            "restart_commit_required": False,
        }
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    handoff_script, handoff_log, armed_marker, go_marker = restart_handoff_paths()
    remove_runtime_marker(armed_marker)
    remove_runtime_marker(go_marker)
    service_list = " ".join(CANONICAL_CLEAN_RESTART_SERVICES)
    handoff_script.write_text("""#!/bin/sh
set -u
LOG="%s"
ARMED="%s"
GO="%s"
exec >>"$LOG" 2>&1
echo "clean_restart_handoff armed $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
polls=0
while [ ! -f "$GO" ]; do
  if [ "$polls" -ge %d ]; then
    echo "clean_restart_handoff gate_timeout $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
    rm -f "$ARMED"
    exit 111
  fi
  sleep %.1f
  polls=$((polls + 1))
done
rm -f "$GO" "$ARMED"
echo "clean_restart_handoff acknowledged $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
sleep %.1f
sync || true
for svc in %s; do
  if [ -x "/etc/init.d/$svc" ]; then
    echo "stop $svc"
    "/etc/init.d/$svc" stop || true
  fi
done
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill "$pid" 2>/dev/null || true
done
sleep 0.3
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill -KILL "$pid" 2>/dev/null || true
done
rm -f /run/8ax/*.pid
rm -f /run/8ax_v5_product_ui/*.sock
rm -f /dev/shm/v3_status_shm
rm -f /dev/shm/v5_native_*.bin
rm -f /run/8ax_v5_drive/drive_profile_resident_snapshot.json
rm -f /run/8ax_v5_product_ui/settings_actiond_events.jsonl
rm -f /run/8ax_v5_product_ui/touch_events.jsonl
sync || true
echo "clean_restart_handoff reboot $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
if command -v reboot >/dev/null 2>&1; then
  reboot -f
elif [ -x /sbin/reboot ]; then
  /sbin/reboot -f
else
  echo b >/proc/sysrq-trigger
fi
""" % (
        str(handoff_log),
        str(armed_marker),
        str(go_marker),
        RESTART_GATE_TIMEOUT_POLLS,
        RESTART_GATE_POLL_S,
        RESTART_HANDOFF_DELAY_S,
        service_list,
    ), encoding="utf-8")
    handoff_script.chmod(0o755)
    result = {
        "schema": "v5.settings_action_result.v1",
        "generated_at": now_utc(),
        "action": action,
        "owner": spec.get("owner", ""),
        "ok": True,
        "code": "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED",
        "message_cn": "系统级重启已准备，等待关闭结果窗后提交。",
        "display_message_cn": "系统级重启已准备，点击关闭后黑屏并重启。",
        "write_executed": bool(
            rotary_drive_owner.get("changed") or
            rotary_profile.get("changed")),
        "motion_executed": False,
        "restart_executed": False,
        "restart_commit_required": True,
        "clean_restart_equivalent": "board_reboot_after_ui_ack",
        "handoff_script": str(handoff_script),
        "handoff_log": str(handoff_log),
        "stop_order": CANONICAL_CLEAN_RESTART_SERVICES,
        "rotary_drive_owner": rotary_drive_owner,
        "rotary_wcheckpoint_profile": rotary_profile,
    }
    return result


def commit_restart_handoff() -> Dict[str, Any]:
    handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    if not handoff_script.is_file():
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_HANDOFF_NOT_PREPARED",
            "detail": str(handoff_script),
        }
    remove_runtime_marker(go_marker)
    try:
        armed_marker.write_text("armed\n", encoding="utf-8")
    except Exception as exc:
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_ARM_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    try:
        subprocess.Popen(
            ["/bin/sh", str(handoff_script)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except Exception as exc:
        remove_runtime_marker(armed_marker)
        return {
            "ok": False,
            "accepted": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_SPAWN_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    return {
        "ok": True,
        "accepted": True,
        "code": "SETTINGS_SAVE_RESTART_COMMIT_ACK",
        "handoff_script": str(handoff_script),
        "handoff_delay_s": RESTART_HANDOFF_DELAY_S,
        "gate_armed": True,
    }


def release_restart_handoff() -> Dict[str, Any]:
    _handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    if not armed_marker.is_file():
        return {
            "ok": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_GATE_NOT_ARMED",
        }
    try:
        armed_marker.replace(go_marker)
    except Exception as exc:
        return {
            "ok": False,
            "code": "SETTINGS_SAVE_RESTART_COMMIT_GATE_RELEASE_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
        }
    return {
        "ok": True,
        "code": "SETTINGS_SAVE_RESTART_COMMIT_GATE_RELEASED",
    }


def abort_restart_handoff() -> None:
    _handoff_script, _handoff_log, armed_marker, go_marker = restart_handoff_paths()
    remove_runtime_marker(armed_marker)
    remove_runtime_marker(go_marker)
