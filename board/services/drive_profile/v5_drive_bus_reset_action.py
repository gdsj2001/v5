from __future__ import annotations

import os
import time
from typing import Any, Dict, List

import v5_drive_enable_window
from v5_drive_bus_contract import DriveActionError, now_utc
from v5_drive_axis_model import mark_reset_invalid
from v5_drive_health import (
    fault_reset_batch,
    precheck_targets_for_write,
    request_full_target_set_state,
)
from v5_drive_parameter_table import (
    drive_display_update_from_health,
    write_drive_parameter_display_rows,
)
from v5_drive_query import configured_drive_targets
from v5_drive_result import compact_error_detail, compact_readback
from v5_drive_runtime_store import persist_settings_runtime
from v5_drive_sdo import write_command

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
