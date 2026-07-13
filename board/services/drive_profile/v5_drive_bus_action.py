#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any, Dict, List, Tuple

import v5_drive_bus_context as context
import v5_drive_bus_contract as contract
from v5_drive_bus_contract import (
    AXIS_ORDER,
    CANONICAL_CSP_MODE,
    DriveActionError,
    OPTIONAL_READ_COMMANDS,
    REQUIRED_READ_COMMANDS,
    SETTINGS_RUNTIME_SCHEMA,
    now_utc,
)
from v5_drive_result import (
    write_json,
    compact_sdo_io,
    compact_recovery_result,
    compact_health,
    compact_readback,
    compact_error_detail,
    compact_target_result,
    compact_precheck_result,
    compact_scan_result,
    compact_action_result_payload,
)
from v5_drive_runtime_store import (
    settings_runtime_key,
    settings_runtime_forbidden_reason,
    _settings_runtime_schema_error,
    _validate_settings_runtime_node,
    validate_settings_runtime_drive_only,
    _sanitize_settings_runtime_node,
    sanitize_settings_runtime_drive_only,
    drive_only_scale_evidence,
    read_runtime_ini_sections,
    runtime_ini_value,
    load_settings_runtime,
    find_runtime_axis,
    saved_zero_counts,
    write_text_atomic,
    update_runtime_ini_raw_limits,
    persist_axis_zero_model,
    persist_settings_runtime,
)
from v5_drive_sdo import (
    run_command,
    run_ethercat_slaves,
    normalize_hex,
    read_resident_snapshot,
    replace_resident_snapshot,
    parse_slave_identity,
    profile_matches,
    select_profile,
    sdo_object_tokens,
    scalar_data_type,
    command_timeout,
    signed_type_bits,
    parse_integer_token,
    parse_upload_value,
    ethercat_upload,
    ethercat_download,
    command_write_supported,
    write_command,
    read_command,
    parse_slave_position_token,
)
from v5_drive_parameter_table import (
    read_parameter_tsv,
    write_parameter_tsv,
    scan_slave_option_token,
    write_scan_self_parameter_table,
    normalize_self_slave_binding,
    load_self_slave_bindings,
    drive_display_slave_key,
    format_drive_display_int,
    runtime_axis_by_slave_position,
    write_drive_parameter_display_rows,
    drive_display_update_from_health,
    format_scan_slave_display,
    DRIVE_DISPLAY_FIELDS,
)
from v5_drive_health import (
    read_scalar_value,
    read_pair_value,
    read_required_state,
    evaluate_drive_health,
    recover_slave_mailbox,
    readback_with_retry,
    set_drive_batch_readback,
    summarize_reset_recovery_cycle,
    fault_reset_readback_with_recovery,
    factory_reset_readback_with_recovery,
    assert_drive_write_safety,
)
from v5_drive_axis_model import (
    target_egear,
    mark_drive_parameters_invalid,
    mark_reset_invalid,
    drive_parameter_axis_value,
    final_encoder_bits,
    target_egear_from_runtime_ini,
    derive_counts_per_unit,
    request_slave_position,
    current_axis_counts,
    axis_zero_verify,
    update_axis_drive_set_evidence,
)
from v5_drive_query import (
    read_drive,
    configured_drive_targets,
)
from v5_drive_bus_context import reset_resident_preload_caches

def precheck_targets_for_write(targets: List[Dict[str, Any]], command_names: List[str], timeout_s: float) -> List[Dict[str, Any]]:
    checks: List[Dict[str, Any]] = []
    for target in targets:
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        try:
            for command_name in command_names:
                command_write_supported(command_name, commands.get(command_name, {}))
            safety = assert_drive_write_safety(target, command_names, timeout_s)
            checks.append({"ok": True, "axis": target.get("axis"), "position": target.get("position"), "safety": safety})
        except DriveActionError as exc:
            checks.append({"ok": False, "axis": target.get("axis"), "position": target.get("position"), "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail})
    failures = [item for item in checks if not item.get("ok")]
    if failures:
        raise DriveActionError("DRIVE_WRITE_PRECHECK_FAILED", "写驱动前置检查失败，未写驱动。", {"failures": failures})
    return checks


def run_factory_reset(timeout_s: float) -> Dict[str, Any]:
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        prechecks = precheck_targets_for_write(targets, ["drive.restore_factory_defaults"], timeout_s)
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail), "write_executed": False, "drive_write_executed": False, "motion_executed": False, "failed_stage": "precheck"}
    target_results: List[Dict[str, Any]] = []
    display_updates: List[Dict[str, Any]] = []
    write_executed = False
    for target in targets:
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        item: Dict[str, Any] = {"axis": target.get("axis"), "position": target.get("position"), "profile_id": (target.get("profile") or {}).get("profile_id", "")}
        try:
            write = write_command(str(target.get("position") or ""), "drive.restore_factory_defaults", commands.get("drive.restore_factory_defaults", {}))
            write_executed = True
            item["factory_reset_write"] = write
            if not write.get("ok"):
                raise DriveActionError("DRIVE_RESET_WRITE_FAILED", "复位驱动 SDO 写入失败。", write)
            mark_reset_invalid(target["axis_cfg"], "drive_restore_factory_defaults")
            item["post_write_op_recovery"] = recover_slave_mailbox(str(target.get("position") or ""), timeout_s)
            recovery = factory_reset_readback_with_recovery(target, timeout_s)
            item["factory_reset_recovery"] = recovery.get("summary", {})
            readback = recovery.get("readback", {})
            item["readback"] = compact_readback(readback)
            if not readback.get("ok"):
                raise DriveActionError("DRIVE_RESET_READBACK_FAILED", "复位驱动后必读 SDO 未完整读回。", compact_readback(readback))
            health = readback.get("health", {})
            axis_cfg = target["axis_cfg"]
            axis_cfg["drive_reset_evidence"] = {
                "ok": True,
                "code": "DRIVE_RESET_TARGET_OK",
                "updated_at": now_utc(),
                "slave_index": target.get("position"),
                "profile_id": item.get("profile_id"),
                "statusword": health.get("statusword"),
                "error_code": health.get("error_code"),
                "egear_numerator_readback": health.get("egear_numerator"),
                "egear_denominator_readback": health.get("egear_denominator"),
            }
            display_updates.append(drive_display_update_from_health(str(target.get("axis") or ""), health, "复位失效", target.get("position")))
            item["ok"] = True
            item["code"] = "DRIVE_RESET_TARGET_OK"
        except DriveActionError as exc:
            display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "复位失败"})
            item.update({"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail)})
        target_results.append(item)

    persist = persist_settings_runtime(runtime) if write_executed else {}
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    failures = [item for item in target_results if not item.get("ok")]
    ok = not failures
    return {
        "ok": ok,
        "code": "DRIVE_RESET_OK" if ok else "DRIVE_RESET_PARTIAL",
        "message_cn": "复位驱动完成，旧电子齿轮/设0证据已失效，请重新设置驱动并保存重启。" if ok else "复位驱动未完整闭合。",
        "targets": target_results,
        "failures": failures,
        "prechecks": prechecks,
        "scan": scan,
        "settings_runtime_writeback": persist,
        "drive_parameter_display_writeback": display_writeback,
        "write_executed": write_executed,
        "drive_write_executed": write_executed,
        "motion_executed": False,
        "restart_required": True,
        "restart_deferred": True,
    }


def run_fault_reset(timeout_s: float) -> Dict[str, Any]:
    try:
        targets, _runtime, scan = configured_drive_targets(timeout_s)
        prechecks = precheck_targets_for_write(targets, ["drive.reset_fault"], timeout_s)
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail), "write_executed": False, "drive_write_executed": False, "motion_executed": False, "failed_stage": "precheck"}
    target_results: List[Dict[str, Any]] = []
    write_executed = False
    for target in targets:
        item: Dict[str, Any] = {"axis": target.get("axis"), "position": target.get("position"), "profile_id": (target.get("profile") or {}).get("profile_id", "")}
        try:
            recovery = fault_reset_readback_with_recovery(target, timeout_s)
            write_executed = True
            write = recovery.get("fault_reset_write", {})
            item["fault_reset_write"] = write
            item["fault_reset_recovery"] = recovery.get("summary", {})
            if not write.get("ok"):
                raise DriveActionError("DRIVE_FAULT_RESET_WRITE_FAILED", "清除故障 SDO 写入失败。", write)
            readback = recovery.get("readback", {})
            item["readback"] = compact_readback(readback)
            if not recovery.get("ok"):
                raise DriveActionError("DRIVE_FAULT_RESET_READBACK_FAILED", "清除故障后 statusword/error_code 未证明 fault 已清。", compact_readback(readback))
            item["ok"] = True
            item["code"] = "DRIVE_FAULT_RESET_TARGET_OK"
        except DriveActionError as exc:
            item.update({"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail)})
        target_results.append(item)
    failures = [item for item in target_results if not item.get("ok")]
    ok = not failures
    return {
        "ok": ok,
        "code": "DRIVE_FAULT_RESET_OK" if ok else "DRIVE_FAULT_RESET_PARTIAL",
        "message_cn": "清除故障完成，statusword/error_code 已读回证明无 fault。" if ok else "清除故障未完整闭合。",
        "targets": target_results,
        "failures": failures,
        "prechecks": prechecks,
        "scan": scan,
        "write_executed": write_executed,
        "drive_write_executed": write_executed,
        "motion_executed": False,
    }


def run_set_drive(timeout_s: float) -> Dict[str, Any]:
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        prechecks = precheck_targets_for_write(targets, ["drive.set_egear", "drive.write_mode"], timeout_s)
        planned: Dict[str, Tuple[Tuple[int, int], Dict[str, Any]]] = {}
        for target in targets:
            egear = target_egear(target)
            planned[str(target.get("position") or "")] = ((egear[0], egear[1]), egear[2])
        mode_pre_by_position: Dict[str, Dict[str, Any]] = {}
        for target in targets:
            position = str(target.get("position") or "")
            mode_pre_state = read_required_state(target, timeout_s)
            mode_pre_by_position[position] = {
                "ok": bool(mode_pre_state.get("ok")),
                "mode": read_scalar_value((mode_pre_state.get("reads") or {}).get("drive.read_mode", {})),
            }
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail), "write_executed": False, "drive_write_executed": False, "motion_executed": False, "failed_stage": "precheck"}
    target_results: List[Dict[str, Any]] = []
    display_updates: List[Dict[str, Any]] = []
    write_executed = False
    for target in targets:
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        position = str(target.get("position") or "")
        egear, egear_source = planned[position]
        item: Dict[str, Any] = {"axis": target.get("axis"), "position": position, "profile_id": (target.get("profile") or {}).get("profile_id", ""), "target_egear": {"numerator": egear[0], "denominator": egear[1], "source": egear_source}, "target_mode": CANONICAL_CSP_MODE}
        try:
            egear_command = commands.get("drive.set_egear", {})
            mode_command = commands.get("drive.write_mode", {})
            egear_write = write_command(position, "drive.set_egear", egear_command, {"numerator": egear[0], "denominator": egear[1]})
            write_executed = True
            item["egear_write"] = egear_write
            if not egear_write.get("ok"):
                raise DriveActionError("DRIVE_EGEAR_WRITE_FAILED", "设置驱动电子齿轮 SDO 写入失败。", egear_write)
            item["mode_pre_readback"] = dict(mode_pre_by_position[position])
            mode_pre = item["mode_pre_readback"].get("mode")
            if mode_pre == CANONICAL_CSP_MODE:
                item["mode_write_status"] = "pre_write_readback_already_at_target"
            else:
                item["mode_write"] = write_command(position, "drive.write_mode", mode_command, CANONICAL_CSP_MODE)
                write_executed = True
                item["mode_write_status"] = "download_ok" if item["mode_write"].get("ok") else "download_sdo_error_needs_batch_readback"
            item["profile_save_required"] = bool(egear_command.get("requires_save_parameters")) or bool(item.get("mode_write") and mode_command.get("requires_save_parameters"))
            item["profile_activation_status"] = "pending_save_parameters" if item["profile_save_required"] else "profile_no_activation_required"
            item.update({"ok": True, "code": "DRIVE_SET_TARGET_WRITTEN_PENDING_BATCH_READBACK"})
        except DriveActionError as exc:
            mark_drive_parameters_invalid(target["axis_cfg"], exc.code.lower(), "write_unverified_readback_failed")
            display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "写入失败"})
            item.update({"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail)})
        target_results.append(item)

    if target_results and all(item.get("ok") for item in target_results):
        for target, item in zip(targets, target_results):
            if not item.get("profile_save_required"):
                continue
            commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
            try:
                command_write_supported("drive.save_parameters", commands.get("drive.save_parameters", {}))
                item["save_parameters_write"] = write_command(
                    str(target.get("position") or ""), "drive.save_parameters", commands.get("drive.save_parameters", {}))
                write_executed = True
                if not item["save_parameters_write"].get("ok"):
                    raise DriveActionError("DRIVE_SAVE_PARAMETERS_FAILED", "驱动 profile 要求保存参数，但 save_parameters 写入失败。", item["save_parameters_write"])
                item["profile_activation_status"] = "save_parameters_written"
            except DriveActionError as exc:
                mark_drive_parameters_invalid(target["axis_cfg"], exc.code.lower(), "write_unverified_readback_failed")
                display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "写入失败"})
                item.update({"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail)})

    if target_results and all(item.get("ok") for item in target_results):
        expectations = {position: (egear, CANONICAL_CSP_MODE) for position, (egear, _source) in planned.items()}
        batch_readback = set_drive_batch_readback(targets, timeout_s, expectations)
        failed_positions = {str(position) for position in (batch_readback.get("failed_positions") or [])}
        recovery_positions = {
            str(position)
            for cycle in (batch_readback.get("cycles") or []) if isinstance(cycle, dict)
            for position in (cycle.get("recovery_positions") or [])
        }
        for target, item in zip(targets, target_results):
            position = str(target.get("position") or "")
            egear, egear_source = planned[position]
            readback = batch_readback.get("readbacks", {}).get(position, {})
            item["batch_readback_attempt_count"] = len(batch_readback.get("cycles", []))
            item["batch_recovery_attempted"] = position in recovery_positions
            target_ok = bool(readback.get("ok")) and position not in failed_positions
            if not target_ok:
                mark_drive_parameters_invalid(target["axis_cfg"], "drive_set_batch_readback_failed", "write_unverified_readback_failed")
                display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "写入失败"})
                item.update({"ok": False, "code": "DRIVE_SET_BATCH_READBACK_FAILED", "message_cn": "设置驱动整批写入后的统一 fresh readback 未闭合。"})
                continue
            if isinstance(item.get("mode_write"), dict) and not item["mode_write"].get("ok"):
                item["mode_write_status"] = "download_sdo_error_but_readback_matched"
            update_axis_drive_set_evidence(target["axis_cfg"], target, egear, readback, egear_source)
            health = readback.get("health", {}) if isinstance(readback.get("health"), dict) else {}
            display_updates.append(drive_display_update_from_health(str(target.get("axis") or ""), health, "已写入", target.get("position")))
            item.update({"readback": compact_readback(readback), "code": "DRIVE_SET_TARGET_OK"})
    persist = persist_settings_runtime(runtime) if write_executed else {}
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    failures = [item for item in target_results if not item.get("ok")]
    ok = not failures
    return {
        "ok": ok,
        "code": "DRIVE_SET_OK" if ok else "DRIVE_SET_PARTIAL",
        "message_cn": "设置驱动完成，电子齿轮和 CSP 模式已按 profile 写入并 fresh readback 一致；请执行保存并重启完成运行态生效。" if ok else "设置驱动未完整闭合。",
        "targets": target_results,
        "failures": failures,
        "prechecks": prechecks,
        "scan": scan,
        "settings_runtime_writeback": persist,
        "drive_parameter_display_writeback": display_writeback,
        "write_executed": write_executed,
        "drive_write_executed": write_executed,
        "motion_executed": False,
        "restart_required": True,
        "restart_deferred": True,
    }


def preload_resident_state() -> Dict[str, Any]:
    status: Dict[str, Any] = {"schema": "v5.drive_bus_action.resident_preload.v1"}
    reset_resident_preload_caches()
    context.resident_preload_active = True
    try:
        try:
            runtime = load_settings_runtime()
            status["settings_runtime_loaded"] = True
            status["settings_runtime_axis_count"] = len(runtime.get("axes", [])) if isinstance(runtime.get("axes"), list) else 0
        except DriveActionError as exc:
            status["settings_runtime_loaded"] = False
            status["settings_runtime_error"] = exc.code
        try:
            snapshot = read_resident_snapshot()
            status["drive_snapshot_loaded"] = True
            status["drive_snapshot_profile_count"] = len(snapshot.get("profiles", [])) if isinstance(snapshot.get("profiles"), list) else 0
        except DriveActionError as exc:
            status["drive_snapshot_loaded"] = False
            status["drive_snapshot_error"] = exc.code
        try:
            sections = read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
            status["runtime_ini_loaded"] = bool(sections)
            status["runtime_ini_section_count"] = len(sections)
        except DriveActionError as exc:
            status["runtime_ini_loaded"] = False
            status["runtime_ini_error"] = exc.code
        try:
            bindings = load_self_slave_bindings()
            status["self_slave_bindings_loaded"] = True
            status["self_slave_binding_count"] = len(bindings)
        except DriveActionError as exc:
            status["self_slave_bindings_loaded"] = False
            status["self_slave_bindings_error"] = exc.code
    except BaseException:
        reset_resident_preload_caches()
        context.resident_preload_active = False
        raise
    ok = bool(status.get("settings_runtime_loaded") and status.get("drive_snapshot_loaded") and status.get("runtime_ini_loaded") and status.get("self_slave_bindings_loaded"))
    status["ok"] = ok
    status["preload_complete"] = ok
    if not ok:
        reset_resident_preload_caches()
        context.resident_preload_active = False
        status["code"] = "DRIVE_ACTION_RESIDENT_PRELOAD_INCOMPLETE"
        status["message_cn"] = "驱动动作常驻内存闭包未完整预载，已 fail-closed。"
    else:
        context.resident_preload_active = True
    return status


def result_path(action: str) -> Path:
    names = {"scan": "drive_scan_result.json", "factory-reset": "drive_factory_reset_result.json", "read": "drive_read_result.json", "fault-reset": "drive_fault_reset_result.json", "set-drive": "drive_set_result.json", "axis-zero": "settings_axis_zero_result.json"}
    return contract.RUN_DIR / names.get(action, "drive_action_result.json")


def run_action(action: str, timeout_s: float = 8.0, write_result_file: bool = True, request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    if action == "scan":
        result = run_ethercat_slaves(timeout_s)
        write_scan_self_parameter_table(result)
        if result.get("ok"):
            result["display_message_cn"] = format_scan_slave_display(result)
    elif action == "read":
        result = read_drive(timeout_s)
    elif action == "axis-zero":
        try:
            result = axis_zero_verify(request or {}, timeout_s)
        except DriveActionError as exc:
            result = {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "display_message_cn": exc.message_cn, "detail": exc.detail, "write_executed": False, "motion_executed": False}
    elif action == "factory-reset":
        result = run_factory_reset(timeout_s)
    elif action == "fault-reset":
        result = run_fault_reset(timeout_s)
    elif action == "set-drive":
        result = run_set_drive(timeout_s)
    else:
        result = {
            "ok": False,
            "code": "DRIVE_ACTION_UNKNOWN",
            "message_cn": "未知驱动设置动作，未访问驱动。",
            "action": action,
            "write_executed": False,
            "motion_executed": False,
        }
    result.update({"schema": "v5.drive_bus_action.v1", "generated_at": now_utc(), "action": action})
    if write_result_file:
        write_json(result_path(action), result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 drive bus settings actions")
    parser.add_argument("--action", required=True, choices=("scan", "factory-reset", "read", "fault-reset", "set-drive", "axis-zero"))
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()
    preload_result = preload_resident_state()
    if not preload_result.get("ok"):
        print(json.dumps(preload_result, ensure_ascii=False, indent=2))
        return 1
    result = run_action(args.action, args.timeout, True, {"axis": os.environ.get("V5_AXIS_ZERO_AXIS", "X"), "slave_index": os.environ.get("V5_AXIS_ZERO_SLAVE", ""), "driver_mode": "bus", "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero"})
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("ok") else 1
