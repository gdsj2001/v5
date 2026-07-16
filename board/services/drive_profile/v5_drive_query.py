from __future__ import annotations

from typing import Any, Dict, List, Tuple

from v5_drive_bus_contract import AXIS_ORDER, OPTIONAL_READ_COMMANDS, REQUIRED_READ_COMMANDS, DriveActionError
from v5_drive_health import evaluate_drive_health, recover_full_target_set_mailbox
from v5_drive_parameter_table import (
    drive_display_update_from_health,
    load_self_slave_bindings,
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

def read_targets_stage(targets: List[Dict[str, Any]], scan: Dict[str, Any]) -> Dict[str, Any]:
    states = {
        str(item.get("position") or ""): str(item.get("state") or "").upper()
        for item in (scan.get("slaves") or []) if isinstance(item, dict)
    }
    reads_by_position: Dict[str, Dict[str, Any]] = {
        str(target.get("position") or ""): {} for target in targets}
    for name in REQUIRED_READ_COMMANDS:
        for target in targets:
            position = str(target.get("position") or "")
            commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
            reads_by_position[position][name] = read_command(
                position, name, commands.get(name, {}), True)
    for name in OPTIONAL_READ_COMMANDS:
        for target in targets:
            position = str(target.get("position") or "")
            commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
            command = commands.get(name)
            if isinstance(command, dict) and command.get("supported") is not False:
                reads_by_position[position][name] = read_command(position, name, command, False)
    results: List[Dict[str, Any]] = []
    unavailable_positions: List[str] = []
    for target in targets:
        position = str(target.get("position") or "")
        reads = reads_by_position[position]
        required_ok = all(bool(reads.get(name, {}).get("ok")) for name in REQUIRED_READ_COMMANDS)
        slave_state = states.get(position, "")
        target_ok = required_ok and slave_state == "OP"
        profile = target.get("profile") if isinstance(target.get("profile"), dict) else {}
        results.append({
            "ok": target_ok,
            "axis": target.get("axis"),
            "position": target.get("position"),
            "slave_state": slave_state,
            "identity": target.get("identity", {}),
            "profile_id": profile.get("profile_id", ""),
            "selected_map_path": profile.get("profile_map_path", ""),
            "selected_map_sha256": profile.get("profile_map_sha256", ""),
            "map_source": profile.get("map_source", ""),
            "reads": reads,
            "health": evaluate_drive_health(reads),
        })
        if not target_ok:
            unavailable_positions.append(position)
    return {"targets": results, "unavailable_positions": unavailable_positions, "scan": scan}


def read_drive(timeout_s: float) -> Dict[str, Any]:
    try:
        targets, _runtime, scan = configured_drive_targets(timeout_s)
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail, "read_attempted": False, "write_executed": False, "motion_executed": False}
    first_stage = read_targets_stage(targets, scan)
    final_stage = first_stage
    recovery: Dict[str, Any] | None = None
    recovery_positions = list(dict.fromkeys(first_stage["unavailable_positions"]))
    if recovery_positions:
        recovery = recover_full_target_set_mailbox(targets, timeout_s)
        if recovery.get("ok"):
            fresh_scan = run_ethercat_slaves(timeout_s)
            if fresh_scan.get("ok"):
                final_stage = read_targets_stage(targets, fresh_scan)
    display_updates: List[Dict[str, Any]] = []
    failures: List[Dict[str, Any]] = []
    for item in final_stage["targets"]:
        health = item.get("health") if isinstance(item.get("health"), dict) else {}
        display_updates.append(drive_display_update_from_health(
            str(item.get("axis") or ""), health,
            "读回实际" if item.get("ok") and health.get("ok") else "读回失败",
            item.get("position")))
        if not item.get("ok"):
            failures.append({
                "position": item.get("position"),
                "code": "DRIVE_READ_PARTIAL",
                "slave_state": item.get("slave_state"),
            })
    ok = bool(final_stage["targets"]) and not failures
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    return {
        "ok": ok,
        "code": "DRIVE_READ_OK" if ok else "DRIVE_READ_PARTIAL",
        "message_cn": "读取驱动完成。" if ok else "读取驱动未完整闭合。",
        "targets": final_stage["targets"],
        "failures": failures,
        "recovery_positions": recovery_positions,
        "recovery": recovery,
        "snapshot_generated_at": (scan.get("profile_snapshot") or {}).get("generated_at"),
        "snapshot_profile_count": (scan.get("profile_snapshot") or {}).get("profile_count"),
        "drive_parameter_display_writeback": display_writeback,
        "read_attempted": True,
        "write_executed": False,
        "motion_executed": False,
    }


def configured_drive_targets(timeout_s: float) -> Tuple[List[Dict[str, Any]], Dict[str, Any], Dict[str, Any]]:
    scan = run_ethercat_slaves(timeout_s)
    if not scan.get("ok"):
        raise DriveActionError(str(scan.get("code") or "DRIVE_SCAN_FAILED"), str(scan.get("message_cn") or "扫描从站失败，未写驱动。"), scan)
    runtime = load_settings_runtime()
    self_bindings = load_self_slave_bindings()
    slaves_by_position: Dict[str, Dict[str, Any]] = {}
    for slave in scan.get("slaves", []):
        if isinstance(slave, dict):
            position = parse_slave_position_token(slave.get("position"))
            if position:
                slaves_by_position[position] = slave
    failures: List[Dict[str, Any]] = []
    runtime_by_axis: Dict[str, Tuple[int, Dict[str, Any]]] = {}
    axes = runtime.get("axes") if isinstance(runtime.get("axes"), list) else []
    for axis_index, axis_cfg in enumerate(axes):
        if not isinstance(axis_cfg, dict):
            failures.append({"axis": "", "code": "DRIVE_TARGET_RUNTIME_AXIS_INVALID", "message_cn": "drive runtime 轴配置非法，未写驱动。", "detail": {"axis_index": axis_index}})
            continue
        axis = str(axis_cfg.get("axis") or "").strip().upper()
        if axis not in AXIS_ORDER:
            failures.append({"axis": axis, "code": "DRIVE_TARGET_RUNTIME_AXIS_INVALID", "message_cn": "drive runtime 轴名非法，未写驱动。", "detail": {"axis_index": axis_index, "axis": axis}})
            continue
        if axis in runtime_by_axis:
            failures.append({"axis": axis, "code": "DRIVE_TARGET_RUNTIME_AXIS_DUPLICATE", "message_cn": "drive runtime 中轴配置重复，未写驱动。", "detail": {"axis": axis, "axis_index": axis_index}})
            continue
        runtime_by_axis[axis] = (axis_index, axis_cfg)

    candidates: List[Tuple[str, int, Dict[str, Any], str]] = []
    seen_positions: set[str] = set()
    for axis in AXIS_ORDER:
        try:
            if axis not in self_bindings:
                raise DriveActionError(
                    "DRIVE_TARGET_SELF_SLAVE_MISSING",
                    "resident self owner 缺少轴从站绑定，未写驱动。",
                    {"axis": axis},
                )
            binding_source = "resident_self_parameter_table"
            raw_binding = self_bindings[axis]
            if str(raw_binding or "").strip().upper() == "NAT":
                continue
            runtime_entry = runtime_by_axis.get(axis)
            if runtime_entry is None:
                raise DriveActionError(
                    "DRIVE_TARGET_RUNTIME_AXIS_MISSING",
                    "resident self owner 中已绑定的轴缺少 drive runtime 配置，未写驱动。",
                    {"axis": axis},
                )
            axis_index, axis_cfg = runtime_entry
            position = parse_slave_position_token(raw_binding)
            if not position:
                raise DriveActionError(
                    "DRIVE_TARGET_SELF_SLAVE_INVALID",
                    "resident self owner 的轴从站绑定非法，未写驱动。",
                    {"axis": axis, "slave": raw_binding},
                )
            if position in seen_positions:
                raise DriveActionError("DRIVE_TARGET_DUPLICATE_SLAVE", "多个轴绑定到同一从站，未写驱动。", {"axis": axis, "slave_index": position})
            seen_positions.add(position)
            if position not in slaves_by_position:
                raise DriveActionError("DRIVE_TARGET_SLAVE_NOT_FOUND", "轴绑定的真实 EtherCAT 从站不存在，未写驱动。", {"axis": axis, "slave_index": position})
            candidates.append((axis, axis_index, axis_cfg, position))
        except DriveActionError as exc:
            failures.append({"axis": axis, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail})
    if failures:
        raise DriveActionError("DRIVE_TARGET_PRECHECK_FAILED", "驱动目标轴预检失败，未写驱动。", {"failures": failures})
    if not candidates:
        raise DriveActionError("DRIVE_TARGETS_EMPTY", "当前 resident 轴从站绑定没有可写入的 BUS 从站，未写驱动。", {"axis_count": len(runtime_by_axis), "self_slave_bindings": self_bindings, "slaves": scan.get("slaves", [])})

    snapshot = read_resident_snapshot()
    targets: List[Dict[str, Any]] = []
    for axis, axis_index, axis_cfg, position in candidates:
        try:
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
    frozen_scan = dict(scan)
    frozen_scan["profile_snapshot"] = {
        "generated_at": snapshot.get("generated_at"),
        "profile_count": snapshot.get("profile_count"),
    }
    return targets, runtime, frozen_scan
