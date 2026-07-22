#!/usr/bin/env python3
from __future__ import annotations

import os
import time
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
    set_drive_batch_readback,
    fault_reset_batch,
    request_full_target_set_state,
    precheck_targets_for_write,
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
from v5_drive_transaction_identity import (
    capture_drive_transaction_identity,
    verify_drive_transaction_identity,
    compact_drive_transaction_identity,
    invalidate_stale_drive_transaction_evidence,
    planned_drive_transaction as _planned_drive_transaction,
    reload_drive_transaction_identity as _reload_drive_transaction_identity,
)
from v5_drive_bus_context import reset_resident_preload_caches
import v5_drive_enable_window

FACTORY_RESET_FAULT_CLEAR_RETRY_DELAY_S = 10.0


def _run_factory_reset_impl(timeout_s: float, request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = request if isinstance(request, dict) else {}
    run_id = str(request_payload.get("_run_id") or request_payload.get("run_id") or "direct-%d-%d" % (os.getpid(), time.monotonic_ns()))
    window_begin, window_finish = {}, {}
    window_started = False
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        window_begin = v5_drive_enable_window.begin(run_id, timeout_s)
        window_started = True
        prechecks = precheck_targets_for_write(
            targets,
            ["drive.restore_factory_defaults"],
            timeout_s,
            wait_disabled_transition=True,
        )
    except DriveActionError as exc:
        if window_started:
            window_finish = v5_drive_enable_window.finish_safely(
                run_id, timeout_s, restore=False)
        return {
            "ok": False,
            "code": exc.code,
            "message_cn": exc.message_cn,
            "detail": compact_error_detail(exc.detail),
            "write_executed": False,
            "drive_write_executed": False,
            "motion_executed": False,
            "failed_stage": "precheck",
            "drive_write_window": {
                "begin": window_begin,
                "finish": window_finish,
                "restore_requested": False,
            },
        }
    target_results_by_position: Dict[str, Dict[str, Any]] = {}
    display_updates: List[Dict[str, Any]] = []
    restore_writes: Dict[str, Dict[str, Any]] = {}
    reset_invalidated = False
    for target in targets:
        position = str(target.get("position") or "")
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        write = write_command(position, "drive.restore_factory_defaults", commands.get("drive.restore_factory_defaults", {}))
        restore_writes[position] = write
        item: Dict[str, Any] = {
            "axis": target.get("axis"),
            "position": target.get("position"),
            "profile_id": (target.get("profile") or {}).get("profile_id", ""),
            "factory_reset_write": write,
        }
        if write.get("ok"):
            mark_reset_invalid(target["axis_cfg"], "drive_restore_factory_defaults")
            reset_invalidated = True
        target_results_by_position[position] = item

    batch = fault_reset_batch(targets, timeout_s)
    initial_batch = batch
    retry_performed = False
    retry_preop_recovery: Dict[str, Any] | None = None
    if (str(batch.get("code") or "") == "DRIVE_FAULT_RESET_BATCH_FAILED" and
            bool(restore_writes) and
            all(bool(write.get("ok")) for write in restore_writes.values())):
        time.sleep(FACTORY_RESET_FAULT_CLEAR_RETRY_DELAY_S)
        retry_performed = True
        retry_preop_recovery = request_full_target_set_state(
            targets, "PREOP", timeout_s)
        if retry_preop_recovery.get("ok"):
            batch = fault_reset_batch(targets, timeout_s)
    batch_writes = batch.get("writes") if isinstance(batch.get("writes"), dict) else {}
    batch_readbacks = batch.get("readbacks") if isinstance(batch.get("readbacks"), dict) else {}
    batch_failed = {str(position) for position in (batch.get("failed_positions") or [])}
    for target in targets:
        position = str(target.get("position") or "")
        item = target_results_by_position[position]
        readback = batch_readbacks.get(position, {})
        item["fault_reset_write"] = batch_writes.get(position, {})
        item["readback"] = compact_readback(readback)
        if not restore_writes[position].get("ok"):
            item.update({
                "ok": False,
                "code": "DRIVE_RESET_WRITE_FAILED",
                "message_cn": "复位驱动 SDO 写入失败。",
                "detail": compact_error_detail(restore_writes[position]),
            })
        elif position in batch_failed or not readback.get("ok"):
            item.update({
                "ok": False,
                "code": "DRIVE_RESET_READBACK_FAILED",
                "message_cn": "复位驱动后统一 fresh readback 未证明目标无 fault/error。",
                "detail": compact_error_detail(readback or batch),
            })
        else:
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
            item.update({"ok": True, "code": "DRIVE_RESET_TARGET_OK"})
        if not item.get("ok"):
            display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "复位失败"})
    target_results = [target_results_by_position[str(target.get("position") or "")] for target in targets]

    write_executed = bool(restore_writes or batch.get("write_executed"))
    persist = persist_settings_runtime(runtime) if reset_invalidated else {}
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    failures = [item for item in target_results if not item.get("ok")]
    ok = not failures
    result: Dict[str, Any] = {
        "ok": ok,
        "code": "DRIVE_RESET_OK" if ok else "DRIVE_RESET_PARTIAL",
        "message_cn": "复位驱动完成，已保持 Machine Off；旧电子齿轮/设0证据已失效，请重新设置驱动并保存重启。" if ok else "复位驱动未完整闭合，已保持 Machine Off。",
        "targets": target_results,
        "failures": failures,
        "prechecks": prechecks,
        "scan": scan,
        "factory_reset_batch": {
            "code": batch.get("code"),
            "initial_code": initial_batch.get("code"),
            "retry_performed": retry_performed,
            "retry_delay_s": FACTORY_RESET_FAULT_CLEAR_RETRY_DELAY_S if retry_performed else 0.0,
            "retry_preop_recovery": retry_preop_recovery,
            "message_cn": batch.get("message_cn"),
            "recovery_positions": batch.get("recovery_positions", []),
            "recovery": batch.get("recovery"),
            "op_recovery": batch.get("op_recovery"),
        },
        "settings_runtime_writeback": persist,
        "drive_parameter_display_writeback": display_writeback,
        "write_executed": write_executed,
        "drive_write_executed": write_executed,
        "motion_executed": False,
        "restart_required": bool(reset_invalidated),
        "restart_deferred": bool(reset_invalidated),
    }
    window_finish = v5_drive_enable_window.finish_safely(
        run_id, timeout_s, restore=False)
    final_off_confirmed = bool(window_finish.get("ok")) and not bool(
        window_finish.get("final_machine_enabled"))
    if not final_off_confirmed:
        result["ok"] = False
        result["code"] = "DRIVE_RESET_WINDOW_CLOSE_FAILED"
        result["message_cn"] = "复位驱动已结束，但 native 安全窗口未确认最终 Machine Off。"
        result["failures"] = list(result.get("failures") or []) + [{
            "ok": False,
            "code": str(window_finish.get("code") or result["code"]),
            "message_cn": str(window_finish.get("message_cn") or result["message_cn"]),
            "detail": compact_error_detail(window_finish.get("detail")),
        }]
    result["drive_write_window"] = {
        "begin": window_begin,
        "finish": window_finish,
        "restore_requested": False,
        "final_off_confirmed": final_off_confirmed,
    }
    return result


def run_factory_reset(timeout_s: float, request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = dict(request) if isinstance(request, dict) else {}
    run_id = str(request_payload.get("_run_id") or request_payload.get("run_id") or
                 "direct-%d-%d" % (os.getpid(), time.monotonic_ns()))
    request_payload["_run_id"] = run_id
    completed = False
    try:
        result = _run_factory_reset_impl(timeout_s, request_payload)
        completed = True
        return result
    finally:
        if not completed:
            v5_drive_enable_window.finish_safely(
                run_id, timeout_s, restore=False)


def run_fault_reset(timeout_s: float) -> Dict[str, Any]:
    try:
        targets, _runtime, scan = configured_drive_targets(timeout_s)
        prechecks = precheck_targets_for_write(targets, ["drive.reset_fault"], timeout_s)
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": compact_error_detail(exc.detail), "write_executed": False, "drive_write_executed": False, "motion_executed": False, "failed_stage": "precheck"}
    batch = fault_reset_batch(targets, timeout_s)
    writes = batch.get("writes") if isinstance(batch.get("writes"), dict) else {}
    write_attempts = batch.get("write_attempts") if isinstance(batch.get("write_attempts"), dict) else {}
    readbacks = batch.get("readbacks") if isinstance(batch.get("readbacks"), dict) else {}
    failed_positions = {str(position) for position in (batch.get("failed_positions") or [])}
    target_results: List[Dict[str, Any]] = []
    for target in targets:
        position = str(target.get("position") or "")
        write = writes.get(position, {})
        readback = readbacks.get(position, {})
        item: Dict[str, Any] = {
            "axis": target.get("axis"),
            "position": target.get("position"),
            "profile_id": (target.get("profile") or {}).get("profile_id", ""),
            "fault_reset_write": write,
            "fault_reset_write_attempts": write_attempts.get(position, []),
            "readback": compact_readback(readback),
        }
        if position not in failed_positions and write.get("ok") and readback.get("ok"):
            item.update({"ok": True, "code": "DRIVE_FAULT_RESET_TARGET_OK"})
        elif not write.get("ok"):
            item.update({"ok": False, "code": "DRIVE_FAULT_RESET_WRITE_FAILED", "message_cn": "清除故障 SDO 写入失败。", "detail": compact_error_detail(write or batch)})
        else:
            item.update({"ok": False, "code": "DRIVE_FAULT_RESET_READBACK_FAILED", "message_cn": "清除故障后 statusword/error_code 未证明 fault 已清。", "detail": compact_error_detail(readback or batch)})
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
        "fault_reset_batch": {
            "code": batch.get("code"),
            "message_cn": batch.get("message_cn"),
            "recovery_positions": batch.get("recovery_positions", []),
            "recovery": batch.get("recovery"),
            "op_recovery": batch.get("op_recovery"),
        },
        "write_executed": bool(batch.get("write_executed")),
        "drive_write_executed": bool(batch.get("write_executed")),
        "motion_executed": False,
    }


def _run_set_drive_preflight_impl(
        timeout_s: float,
        request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = request if isinstance(request, dict) else {}
    frozen_identity: Dict[str, Any] = {}
    identity_check: Dict[str, Any] = {}
    stale_evidence_invalidation: Dict[str, Any] = {}
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        planned = _planned_drive_transaction(targets)
        frozen_identity = capture_drive_transaction_identity(
            targets, planned, scan, timeout_s)
        identity_check = verify_drive_transaction_identity(
            frozen_identity,
            _reload_drive_transaction_identity(timeout_s),
            "settings_drive_preflight")
        stale_rows = invalidate_stale_drive_transaction_evidence(
            targets, frozen_identity)
        if stale_rows:
            runtime_writeback = persist_settings_runtime(runtime)
            display_writeback = write_drive_parameter_display_rows([{
                "axis": item.get("axis"),
                "position": item.get("position"),
                "write_status": "待重启写入",
            } for item in stale_rows])
            stale_evidence_invalidation = {
                "ok": bool(runtime_writeback.get("ok")) and bool(
                    display_writeback.get("ok")),
                "code": "DRIVE_STALE_EVIDENCE_INVALIDATED",
                "targets": stale_rows,
                "settings_runtime_writeback": runtime_writeback,
                "drive_parameter_display_writeback": display_writeback,
            }
            if not stale_evidence_invalidation["ok"]:
                raise DriveActionError(
                    "DRIVE_STALE_EVIDENCE_INVALIDATION_FAILED",
                    "旧电子齿轮证据失效写回未闭合，未形成待重启结果。",
                    stale_evidence_invalidation)
        target_results: List[Dict[str, Any]] = []
        for target in targets:
            position = str(target.get("position") or "")
            commands = target.get("commands") if isinstance(
                target.get("commands"), dict) else {}
            command_write_supported(
                "drive.set_egear", commands.get("drive.set_egear", {}))
            command_write_supported(
                "drive.write_mode", commands.get("drive.write_mode", {}))
            egear, source = planned[position]
            target_results.append({
                "ok": True,
                "code": "DRIVE_SET_PREFLIGHT_TARGET_OK",
                "axis": str(target.get("axis") or ""),
                "position": position,
                "status_slot": int(target.get("status_slot") or 0),
                "profile_id": str((target.get("profile") or {}).get(
                    "profile_id") or ""),
                "target_egear": {
                    "numerator": int(egear[0]),
                    "denominator": int(egear[1]),
                    "source": source,
                },
                "target_mode": CANONICAL_CSP_MODE,
            })
        return {
            "ok": True,
            "code": "DRIVE_SET_RESTART_REQUIRED",
            "message_cn": "驱动最终参数已校验并保存，重启后统一写入并回读。",
            "display_message_cn": "参数已保存，重启后统一写入并回读。",
            "targets": target_results,
            "scan": scan,
            "trigger": str(request_payload.get("trigger") or
                           "settings_set_drive"),
            "drive_transaction_identity": compact_drive_transaction_identity(
                frozen_identity),
            "drive_transaction_checks": {"preflight": identity_check},
            "stale_evidence_invalidation": stale_evidence_invalidation,
            "write_executed": False,
            "drive_write_executed": False,
            "motion_executed": False,
            "restart_required": True,
            "restart_deferred": True,
        }
    except DriveActionError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "message_cn": exc.message_cn,
            "display_message_cn": exc.message_cn,
            "detail": compact_error_detail(exc.detail),
            "drive_transaction_identity": compact_drive_transaction_identity(
                frozen_identity),
            "drive_transaction_checks": {"preflight": identity_check},
            "stale_evidence_invalidation": stale_evidence_invalidation,
            "write_executed": False,
            "drive_write_executed": False,
            "motion_executed": False,
            "restart_required": False,
            "restart_deferred": False,
            "failed_stage": "persistent_preflight",
        }


def _run_boot_drive_apply_impl(
        timeout_s: float,
        request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = request if isinstance(request, dict) else {}
    run_id = str(request_payload.get("_run_id") or request_payload.get("run_id") or "direct-%d-%d" % (os.getpid(), time.monotonic_ns()))
    window_begin, window_finish = {}, {}
    window_started = False
    frozen_identity: Dict[str, Any] = {}
    prewrite_identity_check: Dict[str, Any] = {}
    postreadback_identity_check: Dict[str, Any] = {}
    stale_evidence_invalidation: Dict[str, Any] = {}
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        planned = _planned_drive_transaction(targets)
        frozen_identity = capture_drive_transaction_identity(
            targets, planned, scan, timeout_s)
        stale_rows = invalidate_stale_drive_transaction_evidence(
            targets, frozen_identity)
        if stale_rows:
            stale_runtime_writeback = persist_settings_runtime(runtime)
            stale_display_writeback = write_drive_parameter_display_rows([{
                "axis": item.get("axis"),
                "position": item.get("position"),
                "write_status": "待设置驱动",
            } for item in stale_rows])
            stale_evidence_invalidation = {
                "ok": bool(stale_display_writeback.get("ok")),
                "code": "DRIVE_STALE_EVIDENCE_INVALIDATED",
                "targets": stale_rows,
                "settings_runtime_writeback": stale_runtime_writeback,
                "drive_parameter_display_writeback": stale_display_writeback,
            }
            if not stale_evidence_invalidation["ok"]:
                raise DriveActionError(
                    "DRIVE_STALE_EVIDENCE_INVALIDATION_FAILED",
                    "旧电子齿轮证据失效写回未闭合，未写驱动。",
                    stale_evidence_invalidation)
        window_begin = v5_drive_enable_window.begin(run_id, timeout_s)
        window_started = True
        prechecks = precheck_targets_for_write(
            targets,
            ["drive.set_egear", "drive.write_mode"],
            timeout_s,
            wait_disabled_transition=True,
        )
        prewrite_identity_check = verify_drive_transaction_identity(
            frozen_identity,
            _reload_drive_transaction_identity(timeout_s),
            "before_first_write_sdo")
        actual_pre_by_position: Dict[str, Dict[str, Any]] = {}
        for target in targets:
            position = str(target.get("position") or "")
            actual_pre_state = read_required_state(target, timeout_s)
            reads = actual_pre_state.get("reads") or {}
            egear_pre = read_pair_value(reads.get("drive.read_egear", {}))
            actual_pre_by_position[position] = {
                "ok": bool(actual_pre_state.get("ok")),
                "mode": read_scalar_value(reads.get("drive.read_mode", {})),
                "egear": egear_pre,
            }
    except DriveActionError as exc:
        if window_started:
            window_finish = v5_drive_enable_window.finish_safely(run_id, timeout_s, restore=False)
        return {
            "ok": False, "code": exc.code, "message_cn": exc.message_cn,
            "detail": compact_error_detail(exc.detail), "write_executed": False,
            "drive_write_executed": False, "motion_executed": False, "failed_stage": "precheck",
            "drive_transaction_identity": compact_drive_transaction_identity(frozen_identity),
            "drive_transaction_checks": {
                "prewrite": prewrite_identity_check,
                "postreadback": postreadback_identity_check,
            },
            "stale_evidence_invalidation": stale_evidence_invalidation,
            "drive_write_window": {"begin": window_begin, "finish": window_finish}}
    target_results: List[Dict[str, Any]] = []
    display_updates: List[Dict[str, Any]] = []
    write_executed = False
    for target in targets:
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        position = str(target.get("position") or "")
        egear, egear_source = planned[position]
        item: Dict[str, Any] = {
            "axis": target.get("axis"),
            "position": position,
            "axis_slave_binding_source": target.get("axis_slave_binding_source", ""),
            "profile_id": (target.get("profile") or {}).get("profile_id", ""),
            "target_egear": {"numerator": egear[0], "denominator": egear[1], "source": egear_source},
            "target_mode": CANONICAL_CSP_MODE,
        }
        try:
            egear_command = commands.get("drive.set_egear", {})
            mode_command = commands.get("drive.write_mode", {})
            item["actual_pre_readback"] = dict(
                actual_pre_by_position[position])
            if actual_pre_by_position[position].get("egear") == egear:
                item["egear_write_status"] = "pre_write_readback_already_at_target"
            else:
                egear_write = write_command(
                    position, "drive.set_egear", egear_command,
                    {"numerator": egear[0], "denominator": egear[1]})
                write_executed = True
                item["egear_write"] = egear_write
                if not egear_write.get("ok"):
                    raise DriveActionError(
                        "DRIVE_EGEAR_WRITE_FAILED",
                        "启动时电子齿轮 SDO 写入失败。", egear_write)
            item["mode_pre_readback"] = dict(
                actual_pre_by_position[position])
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
        identity_failure = None
        try:
            postreadback_identity_check = verify_drive_transaction_identity(
                frozen_identity,
                _reload_drive_transaction_identity(timeout_s),
                "after_batch_fresh_readback")
        except DriveActionError as exc:
            identity_failure = exc
            postreadback_identity_check = {
                "ok": False,
                "stage": "after_batch_fresh_readback",
                "code": exc.code,
                "message_cn": exc.message_cn,
                "detail": compact_error_detail(exc.detail),
            }
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
            item["readback"] = compact_readback(readback)
            if identity_failure is not None:
                mark_drive_parameters_invalid(
                    target["axis_cfg"], "drive_transaction_identity_changed",
                    "write_unverified_readback_failed")
                display_updates.append({
                    "axis": str(target.get("axis") or ""),
                    "position": target.get("position"),
                    "write_status": "待设置驱动",
                })
                item.update({
                    "ok": False,
                    "code": identity_failure.code,
                    "message_cn": identity_failure.message_cn,
                    "detail": compact_error_detail(identity_failure.detail),
                })
                continue
            target_ok = bool(readback.get("ok")) and position not in failed_positions
            if not target_ok:
                mark_drive_parameters_invalid(target["axis_cfg"], "drive_set_batch_readback_failed", "write_unverified_readback_failed")
                display_updates.append({"axis": str(target.get("axis") or ""), "position": target.get("position"), "write_status": "写入失败"})
                item.update({"ok": False, "code": "DRIVE_SET_BATCH_READBACK_FAILED", "message_cn": "设置驱动整批写入后的统一 fresh readback 未闭合。"})
                continue
            if isinstance(item.get("mode_write"), dict) and not item["mode_write"].get("ok"):
                item["mode_write_status"] = "download_sdo_error_but_readback_matched"
            update_axis_drive_set_evidence(
                target["axis_cfg"], target, egear, readback, egear_source,
                compact_drive_transaction_identity(frozen_identity))
            health = readback.get("health", {}) if isinstance(readback.get("health"), dict) else {}
            display_updates.append(drive_display_update_from_health(str(target.get("axis") or ""), health, "已写入", target.get("position")))
            item["code"] = "DRIVE_SET_TARGET_OK"
    trigger = str(request_payload.get("trigger") or "post_restart_boot")
    evidence_updated = bool(
        target_results and all(item.get("ok") for item in target_results))
    persist = persist_settings_runtime(runtime) if evidence_updated else {}
    display_writeback = write_drive_parameter_display_rows(display_updates) if display_updates else {}
    failures = [item for item in target_results if not item.get("ok")]
    ok = not failures
    result: Dict[str, Any] = {
        "ok": ok,
        "code": "DRIVE_SET_OK" if ok else "DRIVE_SET_PARTIAL",
        "message_cn": "启动时电子齿轮和 CSP 模式已与最终 INI/profile 一致并完成整批 fresh readback；当前保持 Machine Off。" if ok else "启动驱动应用未完整闭合，保持 Machine Off。",
        "targets": target_results,
        "failures": failures,
        "prechecks": prechecks,
        "scan": scan,
        "trigger": trigger,
        "drive_transaction_identity": compact_drive_transaction_identity(frozen_identity),
        "drive_transaction_checks": {
            "prewrite": prewrite_identity_check,
            "postreadback": postreadback_identity_check,
        },
        "stale_evidence_invalidation": stale_evidence_invalidation,
        "settings_runtime_writeback": persist,
        "drive_parameter_display_writeback": display_writeback,
        "write_executed": write_executed,
        "drive_write_executed": write_executed,
        "motion_executed": False,
        "restart_required": False,
        "restart_deferred": False,
    }
    if postreadback_identity_check.get("code") == "DRIVE_TRANSACTION_IDENTITY_CHANGED":
        result["code"] = "DRIVE_TRANSACTION_IDENTITY_CHANGED"
        result["message_cn"] = "启动整批读回期间最终模型、映射或比例身份发生变化，旧电子齿轮证据已失效并保持 Machine Off。"
    restore_requested = False
    restore_expected = False
    window_finish = v5_drive_enable_window.finish_safely(
        run_id, timeout_s, restore=False)
    machine_off_confirmed = bool(window_finish.get("ok")) and not bool(
        window_finish.get("final_machine_enabled"))
    restore_confirmed = machine_off_confirmed
    if not window_finish.get("ok"):
        result["ok"] = False
        result["code"] = "DRIVE_WRITE_WINDOW_CLOSE_FAILED"
        result["message_cn"] = "设置驱动已保持去使能，但 native 写驱动窗口终态未确认。"
        result["failures"] = list(result.get("failures") or []) + [{
            "ok": False, "code": str(window_finish.get("code") or result["code"]),
            "message_cn": str(window_finish.get("message_cn") or result["message_cn"]),
            "detail": compact_error_detail(window_finish.get("detail"))}]
    elif not machine_off_confirmed:
        result["ok"] = False
        result["code"] = "DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED"
        result["message_cn"] = "设置驱动读回成功，但 fresh actual 未确认 Machine Off，结果作废。"
        result["failures"] = list(result.get("failures") or []) + [{
            "ok": False,
            "code": str(window_finish.get("code") or result["code"]),
            "message_cn": result["message_cn"],
            "detail": {
                "initial_machine_enabled": window_begin.get("initial_machine_enabled"),
                "final_machine_enabled": window_finish.get("final_machine_enabled"),
            },
        }]
    else:
        if result["ok"]:
            result["restart_required"] = False
            result["restart_deferred"] = False
            result["message_cn"] = (
                "启动时电子齿轮和 CSP 模式与最终参数一致，整批 fresh readback 完成；"
                "当前继续保持 Machine Off。")
    result["drive_write_window"] = {
        "begin": window_begin,
        "finish": window_finish,
        "restore_requested": restore_requested,
        "restore_expected": restore_expected,
        "restore_confirmed": restore_confirmed,
        "machine_off_confirmed": machine_off_confirmed,
    }
    return result


def run_set_drive(
        timeout_s: float,
        request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    return _run_set_drive_preflight_impl(timeout_s, request)


def run_boot_drive_apply(
        timeout_s: float,
        request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = dict(request) if isinstance(request, dict) else {}
    run_id = str(request_payload.get("_run_id") or request_payload.get("run_id") or
                 "direct-%d-%d" % (os.getpid(), time.monotonic_ns()))
    request_payload["_run_id"] = run_id
    completed = False
    try:
        result = _run_boot_drive_apply_impl(timeout_s, request_payload)
        completed = True
        return result
    finally:
        if not completed:
            v5_drive_enable_window.finish_safely(
                run_id, timeout_s, restore=False)


def run_axis_zero(timeout_s: float, request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = dict(request) if isinstance(request, dict) else {}
    run_id = str(request_payload.get("_run_id") or request_payload.get("run_id") or
                 "direct-%d-%d" % (os.getpid(), time.monotonic_ns()))
    request_payload["_run_id"] = run_id
    try:
        return axis_zero_verify(request_payload, timeout_s)
    except DriveActionError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "message_cn": exc.message_cn,
            "display_message_cn": exc.message_cn,
            "detail": compact_error_detail(exc.detail),
            "write_executed": False,
            "drive_write_executed": False,
            "motion_executed": False,
            "failed_stage": "axis_zero",
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
    names = {"scan": "drive_scan_result.json", "factory-reset": "drive_factory_reset_result.json", "read": "drive_read_result.json", "fault-reset": "drive_fault_reset_result.json", "set-drive": "drive_set_result.json", "boot-apply": "drive_boot_apply_result.json", "axis-zero": "settings_axis_zero_result.json"}
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
        result = run_axis_zero(timeout_s, request)
    elif action == "factory-reset":
        result = run_factory_reset(timeout_s, request)
    elif action == "fault-reset":
        result = run_fault_reset(timeout_s)
    elif action == "set-drive":
        result = run_set_drive(timeout_s, request)
    elif action == "boot-apply":
        result = run_boot_drive_apply(timeout_s, request)
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
