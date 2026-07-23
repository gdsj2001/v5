from __future__ import annotations

import os
import time
from typing import Any, Dict, List

import v5_drive_enable_window
from v5_drive_boot_attestation import (
    DriveAttestationError,
    invalidate_drive_generation,
)
from v5_drive_axis_model import (
    mark_drive_parameters_invalid,
    update_axis_drive_set_evidence,
)
from v5_drive_bus_contract import CANONICAL_CSP_MODE, DriveActionError
from v5_drive_health import (
    precheck_targets_for_write,
    read_pair_value,
    read_required_state,
    read_scalar_value,
    set_drive_batch_readback,
)
from v5_drive_parameter_table import (
    drive_display_update_from_health,
    write_drive_parameter_display_rows,
)
from v5_drive_query import configured_drive_targets
from v5_drive_result import compact_error_detail, compact_readback
from v5_drive_runtime_store import persist_settings_runtime
from v5_drive_sdo import command_write_supported, write_command
from v5_drive_transaction_identity import (
    capture_drive_transaction_identity,
    compact_drive_transaction_identity,
    invalidate_stale_drive_transaction_evidence,
    planned_drive_transaction as _planned_drive_transaction,
    reload_drive_transaction_identity as _reload_drive_transaction_identity,
    verify_drive_transaction_identity,
)


def _run_set_drive_preflight_impl(
        timeout_s: float,
        request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    request_payload = request if isinstance(request, dict) else {}
    frozen_identity: Dict[str, Any] = {}
    identity_check: Dict[str, Any] = {}
    stale_evidence_invalidation: Dict[str, Any] = {}
    native_invalidation: Dict[str, Any] = {}
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
        try:
            native_invalidation = invalidate_drive_generation(
                min(max(float(timeout_s), 0.1), 3.0))
        except DriveAttestationError as exc:
            raise DriveActionError(
                "DRIVE_NATIVE_INVALIDATION_FAILED",
                "最终持久输入已校验，但native未确认当前驱动generation失效；保持Machine Off且未形成可用重启结果。",
                {"code": exc.code, "detail": exc.detail}) from exc
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
            "native_invalidation": native_invalidation,
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
            "native_invalidation": native_invalidation,
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
    batch_readback_started_monotonic_ns = 0
    try:
        targets, runtime, scan = configured_drive_targets(timeout_s)
        planned = _planned_drive_transaction(targets)
        frozen_identity = capture_drive_transaction_identity(
            targets, planned, scan, timeout_s)
        if not frozen_identity.get("native_mapping_matches_persistent"):
            raise DriveActionError(
                "DRIVE_BOOT_NATIVE_MAPPING_GENERATION_MISMATCH",
                "重启后的 native 映射 generation 与最终持久映射不一致，未写驱动。",
                compact_drive_transaction_identity(frozen_identity))
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
            "status_slot": int(target.get("status_slot") or 0),
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
        batch_readback_started_monotonic_ns = time.monotonic_ns()
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
        "batch_readback_started_monotonic_ns":
            batch_readback_started_monotonic_ns,
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
