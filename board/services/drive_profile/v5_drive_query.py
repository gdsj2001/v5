from __future__ import annotations

from typing import Any, Dict, List, Tuple

from v5_drive_bus_contract import AXIS_ORDER, OPTIONAL_READ_COMMANDS, REQUIRED_READ_COMMANDS, DriveActionError
from v5_drive_health import evaluate_drive_health
from v5_drive_parameter_table import (
    drive_display_update_from_health,
    load_self_slave_bindings,
    runtime_axis_by_slave_position,
    write_drive_parameter_display_rows,
)
from v5_drive_runtime_store import load_settings_runtime
from v5_drive_sdo import (
    parse_slave_identity,
    parse_slave_position_token,
    read_command,
    read_resident_snapshot,
    run_ethercat_slaves,
    select_profile,
)

def read_drive(timeout_s: float) -> Dict[str, Any]:
    scan = run_ethercat_slaves(timeout_s)
    if not scan.get("ok"):
        scan.update({"read_attempted": False})
        return scan
    if not scan.get("slaves"):
        return {"ok": False, "code": "DRIVE_READ_NO_SLAVES", "message_cn": "未扫描到 EtherCAT 从站，读取驱动未执行。", "slaves": [], "read_attempted": False, "write_executed": False, "motion_executed": False}
    try:
        snapshot = read_resident_snapshot()
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail, "read_attempted": False, "write_executed": False, "motion_executed": False}
    targets: List[Dict[str, Any]] = []
    failures: List[Dict[str, Any]] = []
    axis_by_position = runtime_axis_by_slave_position()
    display_updates: List[Dict[str, Any]] = []
    for slave in scan.get("slaves", []):
        position = str(slave.get("position") or "")
        try:
            identity = parse_slave_identity(position, min(timeout_s, 3.0))
            if not identity.get("identity_ok"):
                raise DriveActionError("DRIVE_SLAVE_IDENTITY_MISSING", "真实从站身份读取不完整，未选择 profile。", identity)
            profile = select_profile(identity, snapshot)
            commands = profile.get("commands", {}) if isinstance(profile.get("commands"), dict) else {}
            reads: Dict[str, Any] = {}
            target_ok = True
            for name in REQUIRED_READ_COMMANDS:
                item = read_command(position, name, commands.get(name, {}), True)
                reads[name] = item
                target_ok = target_ok and bool(item.get("ok"))
            for name in OPTIONAL_READ_COMMANDS:
                command = commands.get(name)
                if isinstance(command, dict) and command.get("supported") is not False:
                    reads[name] = read_command(position, name, command, False)
            axis = axis_by_position.get(position, "")
            health = evaluate_drive_health(reads)
            display_updates.append(drive_display_update_from_health(axis, health, "读回实际" if target_ok and health.get("ok") else "读回失败", position))
            targets.append({"ok": target_ok, "axis": axis, "position": position, "identity": identity, "profile_id": profile.get("profile_id", ""), "selected_map_path": profile.get("profile_map_path", ""), "selected_map_sha256": profile.get("profile_map_sha256", ""), "map_source": profile.get("map_source", ""), "reads": reads})
            if not target_ok:
                failures.append({"position": position, "code": "DRIVE_READ_PARTIAL"})
        except DriveActionError as exc:
            failure = {"position": position, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail}
            axis = axis_by_position.get(position, "")
            display_updates.append({"axis": axis, "position": position, "write_status": "读回失败"})
            targets.append({"ok": False, "axis": axis, **failure})
            failures.append(failure)
    ok = bool(targets) and not failures
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    return {"ok": ok, "code": "DRIVE_READ_OK" if ok else "DRIVE_READ_PARTIAL", "message_cn": "读取驱动完成。" if ok else "读取驱动未完整闭合。", "targets": targets, "failures": failures, "snapshot_generated_at": snapshot.get("generated_at"), "snapshot_profile_count": snapshot.get("profile_count"), "drive_parameter_display_writeback": display_writeback, "read_attempted": True, "write_executed": False, "motion_executed": False}


def configured_drive_targets(timeout_s: float) -> Tuple[List[Dict[str, Any]], Dict[str, Any], Dict[str, Any]]:
    scan = run_ethercat_slaves(timeout_s)
    if not scan.get("ok"):
        raise DriveActionError(str(scan.get("code") or "DRIVE_SCAN_FAILED"), str(scan.get("message_cn") or "扫描从站失败，未写驱动。"), scan)
    runtime = load_settings_runtime()
    self_bindings = load_self_slave_bindings()
    snapshot = read_resident_snapshot()
    slaves_by_position: Dict[str, Dict[str, Any]] = {}
    for slave in scan.get("slaves", []):
        if isinstance(slave, dict):
            position = parse_slave_position_token(slave.get("position"))
            if position:
                slaves_by_position[position] = slave
    targets: List[Dict[str, Any]] = []
    failures: List[Dict[str, Any]] = []
    seen_positions: set[str] = set()
    axes = runtime.get("axes") if isinstance(runtime.get("axes"), list) else []
    for axis_index, axis_cfg in enumerate(axes):
        if not isinstance(axis_cfg, dict):
            continue
        axis = str(axis_cfg.get("axis") or (AXIS_ORDER[axis_index] if axis_index < len(AXIS_ORDER) else "AXIS_%d" % axis_index)).upper()
        try:
            if axis in self_bindings:
                binding_source = "self_parameter_table"
                raw_binding = self_bindings.get(axis, "")
            else:
                binding_source = "settings_runtime"
                raw_binding = axis_cfg.get("slave_index") if axis_cfg.get("slave_index") is not None else axis_cfg.get("slave")
            if str(raw_binding or "").strip().upper() == "NAT":
                continue
            position = parse_slave_position_token(raw_binding)
            if not position:
                continue
            if position in seen_positions:
                raise DriveActionError("DRIVE_TARGET_DUPLICATE_SLAVE", "多个轴绑定到同一从站，未写驱动。", {"axis": axis, "slave_index": position})
            seen_positions.add(position)
            if position not in slaves_by_position:
                raise DriveActionError("DRIVE_TARGET_SLAVE_NOT_FOUND", "轴绑定的真实 EtherCAT 从站不存在，未写驱动。", {"axis": axis, "slave_index": position})
            identity = parse_slave_identity(position, min(timeout_s, 3.0))
            if not identity.get("identity_ok"):
                raise DriveActionError("DRIVE_SLAVE_IDENTITY_MISSING", "真实从站身份读取不完整，未写驱动。", identity)
            profile = select_profile(identity, snapshot)
            selected_id = str(profile.get("profile_id") or "")
            expected_id = str(axis_cfg.get("drive_profile_id") or axis_cfg.get("profile_id") or "")
            if expected_id and selected_id and expected_id != selected_id:
                raise DriveActionError("DRIVE_PROFILE_BINDING_MISMATCH", "轴绑定 profile 与真实从站匹配 profile 不一致，未写驱动。", {"axis": axis, "expected_profile_id": expected_id, "selected_profile_id": selected_id, "slave_index": position})
            commands = profile.get("commands", {}) if isinstance(profile.get("commands"), dict) else {}
            targets.append({
                "axis": axis,
                "axis_index": axis_index,
                "axis_cfg": axis_cfg,
                "position": position,
                "axis_slave_binding_source": binding_source,
                "slave": slaves_by_position[position],
                "identity": identity,
                "profile": profile,
                "commands": commands,
            })
        except DriveActionError as exc:
            failures.append({"axis": axis, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail})
    if failures:
        raise DriveActionError("DRIVE_TARGET_PRECHECK_FAILED", "驱动目标轴预检失败，未写驱动。", {"failures": failures})
    if not targets:
        raise DriveActionError("DRIVE_TARGETS_EMPTY", "当前轴参数从站绑定没有可写入的 BUS 从站，未写驱动。", {"axis_count": len(axes), "self_slave_bindings": self_bindings, "slaves": scan.get("slaves", [])})
    return targets, runtime, scan
