from __future__ import annotations

import time
from typing import Any, Dict, List, Tuple

from v5_drive_bus_contract import (
    DriveActionError,
    REQUIRED_READ_COMMANDS,
    STATUSWORD_FAULT_BIT,
    STATUSWORD_OPERATION_ENABLED_BIT,
)
from v5_drive_result import compact_read_item, compact_readback, compact_sdo_io
from v5_drive_sdo import (
    command_write_supported,
    read_command,
    run_command,
    run_ethercat_slaves,
    write_command,
)

SET_DRIVE_BATCH_READBACK_ATTEMPTS = 4
SET_DRIVE_BATCH_REREAD_DELAY_S = 0.25
FAULT_RESET_BATCH_SETTLE_S = 0.25
DRIVE_DISABLE_TRANSITION_TIMEOUT_S = 2.0
DRIVE_DISABLE_TRANSITION_POLL_S = 0.025

def read_scalar_value(read_item: Dict[str, Any]) -> int | None:
    upload = read_item.get("upload") if isinstance(read_item.get("upload"), dict) else {}
    value = upload.get("value")
    try:
        return int(value)
    except Exception:
        return None


def read_pair_value(read_item: Dict[str, Any]) -> Tuple[int | None, int | None]:
    numerator = read_item.get("numerator") if isinstance(read_item.get("numerator"), dict) else {}
    denominator = read_item.get("denominator") if isinstance(read_item.get("denominator"), dict) else {}
    try:
        num = int(numerator.get("value"))
    except Exception:
        num = None
    try:
        den = int(denominator.get("value"))
    except Exception:
        den = None
    return num, den


def read_required_state(target: Dict[str, Any], timeout_s: float) -> Dict[str, Any]:
    position = str(target.get("position") or "")
    commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
    reads: Dict[str, Any] = {}
    ok = True
    for name in REQUIRED_READ_COMMANDS:
        item = read_command(position, name, commands.get(name, {}), True)
        reads[name] = item
        ok = ok and bool(item.get("ok"))
    aux_error_command = commands.get("drive.read_aux_error_code")
    if isinstance(aux_error_command, dict) and aux_error_command.get("supported") is not False:
        reads["drive.read_aux_error_code"] = read_command(position, "drive.read_aux_error_code", aux_error_command, False)
    velocity_command = commands.get("drive.read_actual_velocity")
    if isinstance(velocity_command, dict) and velocity_command.get("supported") is not False:
        reads["drive.read_actual_velocity"] = read_command(position, "drive.read_actual_velocity", velocity_command, False)
    return {"ok": ok, "reads": reads}


def evaluate_drive_health(reads: Dict[str, Any],
                          expected_egear: Tuple[int, int] | None = None,
                          expected_mode: int | None = None,
                          require_positive_egear: bool = True) -> Dict[str, Any]:
    failures: List[Dict[str, Any]] = []
    statusword = read_scalar_value(reads.get("drive.read_statusword", {}))
    error_code = read_scalar_value(reads.get("drive.read_error_code", {}))
    aux_error_code = read_scalar_value(reads.get("drive.read_aux_error_code", {}))
    mode = read_scalar_value(reads.get("drive.read_mode", {}))
    actual_position = read_scalar_value(reads.get("drive.read_actual_position", {}))
    egear_num, egear_den = read_pair_value(reads.get("drive.read_egear", {}))
    if statusword is None:
        failures.append({"field": "statusword", "code": "DRIVE_STATUSWORD_READ_FAILED"})
    elif statusword & STATUSWORD_FAULT_BIT:
        failures.append({"field": "statusword", "code": "DRIVE_STATUSWORD_FAULT_BIT_SET", "value": statusword})
    if error_code is None:
        failures.append({"field": "error_code", "code": "DRIVE_ERROR_CODE_READ_FAILED"})
    elif error_code != 0:
        failure = {"field": "error_code", "code": "DRIVE_ERROR_CODE_NONZERO", "value": error_code}
        if aux_error_code is not None:
            failure["aux_error_code"] = aux_error_code
        failures.append(failure)
    elif aux_error_code not in (None, 0):
        failures.append({"field": "aux_error_code", "code": "DRIVE_AUX_ERROR_CODE_NONZERO", "value": aux_error_code})
    if actual_position is None:
        failures.append({"field": "actual_position", "code": "DRIVE_ACTUAL_POSITION_READ_FAILED"})
    if require_positive_egear and (egear_num is None or egear_den is None or egear_num <= 0 or egear_den <= 0):
        failures.append({"field": "egear", "code": "DRIVE_EGEAR_READBACK_INVALID", "numerator": egear_num, "denominator": egear_den})
    if expected_egear and (egear_num != expected_egear[0] or egear_den != expected_egear[1]):
        failures.append({"field": "egear", "code": "DRIVE_EGEAR_READBACK_MISMATCH", "expected": expected_egear, "actual": (egear_num, egear_den)})
    if expected_mode is not None and mode != expected_mode:
        failures.append({"field": "mode", "code": "DRIVE_MODE_READBACK_MISMATCH", "expected": expected_mode, "actual": mode})
    return {
        "ok": not failures,
        "failures": failures,
        "statusword": statusword,
        "error_code": error_code,
        "aux_error_code": aux_error_code,
        "mode_of_operation": mode,
        "actual_position_counts": actual_position,
        "egear_numerator": egear_num,
        "egear_denominator": egear_den,
    }


def readback_once(target: Dict[str, Any],
                  timeout_s: float,
                  expected_egear: Tuple[int, int] | None = None,
                  expected_mode: int | None = None) -> Dict[str, Any]:
    state = read_required_state(target, timeout_s)
    health = evaluate_drive_health(
        state.get("reads", {}), expected_egear, expected_mode)
    compact_reads = {
        name: compact_read_item(item)
        for name, item in (state.get("reads", {}) or {}).items()
    }
    snapshot = {
        "attempt": 1,
        "read_ok": bool(state.get("ok")),
        "health": health,
        "reads": compact_reads,
    }
    return {
        "ok": bool(state.get("ok")) and bool(health.get("ok")),
        "attempts": [snapshot],
        "health": health,
    }


def request_full_target_set_state(targets: List[Dict[str, Any]], state: str,
                                  timeout_s: float) -> Dict[str, Any]:
    requested_state = str(state or "").upper()
    target_positions = [str(target.get("position") or "") for target in targets]
    if (not target_positions or any(not position for position in target_positions)
            or len(set(target_positions)) != len(target_positions)):
        return {
            "ok": False,
            "code": "DRIVE_STATE_TARGET_SET_INVALID",
            "requested_state": requested_state,
            "operations": [],
            "target_positions": target_positions,
            "scanned_positions": [],
            "failed_positions": list(dict.fromkeys(target_positions)),
        }
    scan = run_ethercat_slaves(min(timeout_s, 5.0))
    scanned_positions = [
        str(item.get("position") or "")
        for item in (scan.get("slaves") or []) if isinstance(item, dict)
    ] if scan.get("ok") else []
    if (not scan.get("ok") or len(scanned_positions) != len(target_positions)
            or set(scanned_positions) != set(target_positions)):
        return {
            "ok": False,
            "code": "DRIVE_STATE_TARGET_SET_MISMATCH",
            "requested_state": requested_state,
            "operations": [],
            "target_positions": target_positions,
            "scanned_positions": scanned_positions,
            "failed_positions": target_positions,
        }
    op = run_command(
        ["ethercat", "states", requested_state], min(timeout_s, 5.0))
    compact = compact_sdo_io(op)
    compact.update({
        "requested_state": requested_state,
        "positions": target_positions,
        "scope": "full_scanned_target_set",
    })
    operations = [compact]
    poll_count = 0
    actual_states: Dict[str, str] = {}
    failed_positions = list(target_positions)
    state_readback_ok = False
    if op.get("ok"):
        deadline = time.monotonic() + max(0.5, min(timeout_s, 5.0))
        while True:
            remaining = deadline - time.monotonic()
            scan_timeout = max(0.1, min(remaining, 1.0))
            actual_scan = run_ethercat_slaves(scan_timeout)
            poll_count += 1
            actual_states = {
                str(item.get("position") or ""):
                    str(item.get("state") or "").upper()
                for item in (actual_scan.get("slaves") or [])
                if isinstance(item, dict)
            } if actual_scan.get("ok") else {}
            failed_positions = [
                position for position in target_positions
                if actual_states.get(position) != requested_state
            ]
            if (actual_scan.get("ok")
                    and set(actual_states) == set(target_positions)
                    and not failed_positions):
                state_readback_ok = True
                break
            if remaining <= 0:
                break
            time.sleep(min(0.1, remaining))
    ok = bool(op.get("ok")) and state_readback_ok
    return {
        "ok": ok,
        "code": (
            "DRIVE_STATE_TARGET_SET_OK" if ok
            else (str(op.get("code") or "DRIVE_STATE_TARGET_SET_FAILED")
                  if not op.get("ok")
                  else "DRIVE_STATE_TARGET_SET_READBACK_TIMEOUT")
        ),
        "requested_state": requested_state,
        "operations": operations,
        "target_positions": target_positions,
        "scanned_positions": scanned_positions,
        "actual_states": actual_states,
        "state_readback_ok": state_readback_ok,
        "poll_count": poll_count,
        "failed_positions": failed_positions,
    }


def recover_full_target_set_mailbox(targets: List[Dict[str, Any]], timeout_s: float) -> Dict[str, Any]:
    operations: List[Dict[str, Any]] = []
    for state in ("PREOP", "OP"):
        result = request_full_target_set_state(targets, state, timeout_s)
        operations.extend(result.get("operations") or [])
        if not result.get("ok"):
            return {
                "ok": False,
                "code": str(result.get("code") or "DRIVE_TARGET_SET_RECOVERY_FAILED"),
                "operations": operations,
                "failed_positions": list(result.get("failed_positions") or []),
            }
    return {
        "ok": bool(operations),
        "code": "DRIVE_TARGET_SET_RECOVERY_OK",
        "operations": operations,
        "failed_positions": [],
    }


def batch_required_read_failures(readback: Dict[str, Any]) -> List[Dict[str, str]]:
    attempts = readback.get("attempts") if isinstance(readback.get("attempts"), list) else []
    last_attempt = attempts[-1] if attempts and isinstance(attempts[-1], dict) else {}
    reads = last_attempt.get("reads") if isinstance(last_attempt.get("reads"), dict) else {}
    failures: List[Dict[str, str]] = []
    for command_name in REQUIRED_READ_COMMANDS:
        item = reads.get(command_name) if isinstance(reads.get(command_name), dict) else {}
        if not bool(item.get("ok")):
            failures.append({"command": command_name, "code": str(item.get("code") or "DRIVE_REQUIRED_SDO_READ_FAILED")})
    return failures


def batch_first_failure(required_failures: List[Dict[str, str]], health: Dict[str, Any]) -> Dict[str, Any] | None:
    if required_failures:
        return dict(required_failures[0])
    failures = health.get("failures") if isinstance(health.get("failures"), list) else []
    return dict(failures[0]) if failures and isinstance(failures[0], dict) else None


def set_drive_batch_readback(targets: List[Dict[str, Any]],
                             timeout_s: float,
                             expectations: Dict[str, Tuple[Tuple[int, int], int]]) -> Dict[str, Any]:
    cycles: List[Dict[str, Any]] = []
    final_readbacks: Dict[str, Dict[str, Any]] = {}
    final_failed_positions: List[str] = []
    first_failures: Dict[str, Dict[str, Any]] = {}
    for attempt in range(1, SET_DRIVE_BATCH_READBACK_ATTEMPTS + 1):
        scan = run_ethercat_slaves(min(timeout_s, 5.0))
        if not scan.get("ok"):
            return {"ok": False, "code": str(scan.get("code") or "DRIVE_SCAN_FAILED"), "scan": scan, "cycles": cycles, "readbacks": final_readbacks, "failed_positions": [str(target.get("position") or "") for target in targets]}
        states = {str(item.get("position") or ""): str(item.get("state") or "").upper() for item in (scan.get("slaves") or []) if isinstance(item, dict)}
        readbacks: Dict[str, Dict[str, Any]] = {}
        actual_fault_positions: List[str] = []
        failed_targets: List[Dict[str, Any]] = []
        target_summaries: List[Dict[str, Any]] = []
        for target in targets:
            position = str(target.get("position") or "")
            expected_egear, expected_mode = expectations[position]
            readback = readback_once(
                target, timeout_s, expected_egear, expected_mode)
            slave_state = states.get(position, "")
            health = readback.get("health") if isinstance(readback.get("health"), dict) else {}
            required_failures = batch_required_read_failures(readback)
            readback["slave_state"] = slave_state
            readback["required_read_failures"] = required_failures
            first_failure = batch_first_failure(required_failures, health)
            if not readback.get("ok") and first_failure is not None and position not in first_failures:
                first_failures[position] = first_failure
            if position in first_failures:
                readback["first_failure"] = first_failures[position]
            readbacks[position] = readback
            failure_codes = {str(item.get("code") or "") for item in (health.get("failures") or []) if isinstance(item, dict)}
            has_actual_fault = bool(failure_codes & {"DRIVE_STATUSWORD_FAULT_BIT_SET", "DRIVE_ERROR_CODE_NONZERO", "DRIVE_AUX_ERROR_CODE_NONZERO"})
            if has_actual_fault:
                actual_fault_positions.append(position)
            if slave_state != "OP" or not readback.get("ok"):
                failed_targets.append(target)
            target_summaries.append({
                "position": position,
                "slave_state": slave_state,
                "readback_ok": bool(readback.get("ok")),
                "required_read_failures": required_failures,
                "health_failure_codes": sorted(failure_codes),
            })
        final_readbacks = readbacks
        final_failed_positions = [str(target.get("position") or "") for target in failed_targets]
        all_readback_ok = bool(readbacks) and all(bool(item.get("ok")) for item in readbacks.values())
        cycle: Dict[str, Any] = {
            "attempt": attempt,
            "all_targets_op": all(states.get(str(target.get("position") or "")) == "OP" for target in targets),
            "all_readback_ok": all_readback_ok,
            "actual_fault_positions": actual_fault_positions,
            "targets": target_summaries,
        }
        cycles.append(cycle)
        if not failed_targets and all_readback_ok:
            return {"ok": True, "code": "DRIVE_SET_BATCH_READBACK_OK", "cycles": cycles, "readbacks": final_readbacks, "failed_positions": []}
        if actual_fault_positions:
            break
        if attempt < SET_DRIVE_BATCH_READBACK_ATTEMPTS:
            time.sleep(SET_DRIVE_BATCH_REREAD_DELAY_S)
    return {"ok": False, "code": "DRIVE_SET_BATCH_READBACK_FAILED", "cycles": cycles, "readbacks": final_readbacks, "failed_positions": final_failed_positions}


def fault_reset_batch_snapshot(targets: List[Dict[str, Any]],
                               timeout_s: float,
                               expectations: Dict[str, Tuple[Tuple[int, int], int]] | None = None) -> Dict[str, Any]:
    scan = run_ethercat_slaves(min(timeout_s, 5.0))
    states = {
        str(item.get("position") or ""): str(item.get("state") or "").upper()
        for item in (scan.get("slaves") or []) if isinstance(item, dict)
    } if scan.get("ok") else {}
    readbacks: Dict[str, Dict[str, Any]] = {}
    mailbox_unavailable_positions: List[str] = []
    not_op_positions: List[str] = []
    fault_positions: List[str] = []
    failed_positions: List[str] = []
    for target in targets:
        position = str(target.get("position") or "")
        expected_egear, expected_mode = (expectations or {}).get(position, (None, None))
        readback = readback_once(
            target, timeout_s, expected_egear, expected_mode)
        health = readback.get("health") if isinstance(readback.get("health"), dict) else {}
        required_failures = batch_required_read_failures(readback)
        failure_codes = {
            str(item.get("code") or "") for item in (health.get("failures") or [])
            if isinstance(item, dict)
        }
        slave_state = states.get(position, "")
        readback["slave_state"] = slave_state
        readback["required_read_failures"] = required_failures
        readbacks[position] = readback
        if (not scan.get("ok") or
                slave_state not in {"PREOP", "SAFEOP", "OP"} or
                required_failures):
            mailbox_unavailable_positions.append(position)
        if slave_state != "OP":
            not_op_positions.append(position)
        if failure_codes & {
                "DRIVE_STATUSWORD_FAULT_BIT_SET", "DRIVE_ERROR_CODE_NONZERO",
                "DRIVE_AUX_ERROR_CODE_NONZERO"}:
            fault_positions.append(position)
        if (not readback.get("ok") or
                position in mailbox_unavailable_positions or
                position in not_op_positions):
            failed_positions.append(position)
    return {
        "scan": scan,
        "readbacks": readbacks,
        "mailbox_unavailable_positions": mailbox_unavailable_positions,
        "not_op_positions": not_op_positions,
        "fault_positions": fault_positions,
        "failed_positions": failed_positions,
    }


def fault_reset_batch(targets: List[Dict[str, Any]], timeout_s: float) -> Dict[str, Any]:
    target_by_position = {str(target.get("position") or ""): target for target in targets}
    recovery: Dict[str, Any] | None = None
    recovery_positions: List[str] = []
    baseline = fault_reset_batch_snapshot(targets, timeout_s)
    if baseline["mailbox_unavailable_positions"]:
        recovery_positions = list(dict.fromkeys(
            baseline["mailbox_unavailable_positions"]))
        recovery = request_full_target_set_state(targets, "PREOP", timeout_s)
        if recovery.get("ok"):
            time.sleep(FAULT_RESET_BATCH_SETTLE_S)
            baseline = fault_reset_batch_snapshot(targets, timeout_s)
    if baseline["mailbox_unavailable_positions"]:
        return {
            "ok": False, "code": "DRIVE_FAULT_RESET_BATCH_PREFLIGHT_FAILED",
            "message_cn": "清除故障前目标从站 mailbox/必读 SDO 未完整就绪，未写驱动。",
            "baseline": baseline, "writes": {}, "write_attempts": {},
            "readbacks": baseline["readbacks"], "recovery": recovery,
            "recovery_positions": recovery_positions,
            "op_recovery": None,
            "failed_positions": list(dict.fromkeys(
                baseline["mailbox_unavailable_positions"])),
            "write_executed": False,
        }
    expectations: Dict[str, Tuple[Tuple[int, int], int]] = {}
    invalid_baseline: List[str] = []
    for position, readback in baseline["readbacks"].items():
        drive_health = readback.get("health") if isinstance(readback.get("health"), dict) else {}
        egear = (drive_health.get("egear_numerator"), drive_health.get("egear_denominator"))
        mode = drive_health.get("mode_of_operation")
        if not all(isinstance(value, int) for value in (*egear, mode)) or egear[0] <= 0 or egear[1] <= 0:
            invalid_baseline.append(position)
        else:
            expectations[position] = ((int(egear[0]), int(egear[1])), int(mode))
    if invalid_baseline:
        return {
            "ok": False, "code": "DRIVE_FAULT_RESET_BASELINE_INVALID",
            "message_cn": "清除故障前电子齿轮/模式 fresh 基线不完整，未写驱动。",
            "baseline": baseline, "writes": {}, "write_attempts": {},
            "readbacks": baseline["readbacks"], "recovery": recovery,
            "recovery_positions": recovery_positions,
            "failed_positions": invalid_baseline, "write_executed": False,
        }
    writes: Dict[str, Dict[str, Any]] = {}
    write_attempts: Dict[str, List[Dict[str, Any]]] = {}
    for position, target in target_by_position.items():
        commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
        write = write_command(position, "drive.reset_fault", commands.get("drive.reset_fault", {}))
        writes[position] = write
        write_attempts[position] = [write]
    time.sleep(FAULT_RESET_BATCH_SETTLE_S)
    op_recovery = request_full_target_set_state(targets, "OP", timeout_s)
    time.sleep(FAULT_RESET_BATCH_SETTLE_S)
    final = fault_reset_batch_snapshot(targets, timeout_s, expectations)
    failed_positions = list(dict.fromkeys(
        [position for position, write in writes.items() if not write.get("ok")]
        + list(op_recovery.get("failed_positions") or [])
        + final["failed_positions"]))
    ok = not failed_positions
    return {
        "ok": ok,
        "code": "DRIVE_FAULT_RESET_BATCH_OK" if ok else "DRIVE_FAULT_RESET_BATCH_FAILED",
        "message_cn": "全部目标故障已清除并完成统一 fresh readback。" if ok else "清除故障后仍有目标未通过统一 fresh readback。",
        "baseline": baseline,
        "writes": writes,
        "write_attempts": write_attempts,
        "readbacks": final["readbacks"],
        "final_snapshot": final,
        "recovery": recovery,
        "recovery_positions": recovery_positions,
        "op_recovery": op_recovery,
        "failed_positions": failed_positions,
        "write_executed": bool(writes),
    }


def assert_drive_write_safety(target: Dict[str, Any], command_names: List[str], timeout_s: float) -> Dict[str, Any]:
    commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
    needs_disabled = any(bool(commands.get(name, {}).get("requires_disabled")) for name in command_names if isinstance(commands.get(name), dict))
    needs_no_motion = any(bool(commands.get(name, {}).get("requires_no_motion")) for name in command_names if isinstance(commands.get(name), dict))
    status_read = read_command(str(target.get("position") or ""), "drive.read_statusword", commands.get("drive.read_statusword", {}), True)
    statusword = read_scalar_value(status_read)
    if not status_read.get("ok") or statusword is None:
        raise DriveActionError("DRIVE_WRITE_SAFETY_STATUSWORD_FAILED", "写驱动前 statusword 读回失败，未写驱动。", {"axis": target.get("axis"), "position": target.get("position"), "read": status_read})
    if needs_disabled and (statusword & STATUSWORD_OPERATION_ENABLED_BIT):
        raise DriveActionError("DRIVE_WRITE_REQUIRES_DISABLED", "目标驱动仍处于 Operation Enabled，未写驱动。", {"axis": target.get("axis"), "position": target.get("position"), "statusword": statusword})
    velocity_read = None
    velocity = 0
    if needs_no_motion:
        velocity_command = commands.get("drive.read_actual_velocity")
        if not isinstance(velocity_command, dict) or velocity_command.get("supported") is False:
            raise DriveActionError("DRIVE_WRITE_SAFETY_VELOCITY_UNSUPPORTED", "写驱动前缺少真实速度读回，未写驱动。", {"axis": target.get("axis"), "position": target.get("position")})
        velocity_read = read_command(str(target.get("position") or ""), "drive.read_actual_velocity", velocity_command, True)
        velocity_value = read_scalar_value(velocity_read)
        if not velocity_read.get("ok") or velocity_value is None:
            raise DriveActionError("DRIVE_WRITE_SAFETY_VELOCITY_FAILED", "写驱动前速度读回失败，未写驱动。", {"axis": target.get("axis"), "position": target.get("position"), "read": velocity_read})
        velocity = int(velocity_value)
        if (statusword & STATUSWORD_OPERATION_ENABLED_BIT) and velocity != 0:
            raise DriveActionError("DRIVE_WRITE_REQUIRES_NO_MOTION", "目标驱动速度非 0，未写驱动。", {"axis": target.get("axis"), "position": target.get("position"), "actual_velocity": velocity})
    no_motion_source = "statusword_not_operation_enabled" if not (statusword & STATUSWORD_OPERATION_ENABLED_BIT) else "drive_actual_velocity_zero"
    return {"ok": True, "statusword": statusword, "velocity": velocity, "no_motion_source": no_motion_source, "status_read": compact_read_item(status_read), "velocity_read": compact_read_item(velocity_read) if velocity_read is not None else None}


def precheck_targets_for_write(
    targets: List[Dict[str, Any]],
    command_names: List[str],
    timeout_s: float,
    wait_disabled_transition: bool = False,
) -> List[Dict[str, Any]]:
    deadline = time.monotonic() + max(
        0.1, min(float(timeout_s), DRIVE_DISABLE_TRANSITION_TIMEOUT_S))
    while True:
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
        if not failures:
            return checks
        transition_pending = wait_disabled_transition and all(
            item.get("code") == "DRIVE_WRITE_REQUIRES_DISABLED" for item in failures)
        if transition_pending and time.monotonic() < deadline:
            time.sleep(DRIVE_DISABLE_TRANSITION_POLL_S)
            continue
        if transition_pending:
            raise DriveActionError(
                "DRIVE_WRITE_DISABLE_NOT_CONFIRMED",
                "自动下使能后目标驱动仍未退出 Operation Enabled，未写驱动。",
                {"failures": failures},
            )
        raise DriveActionError(
            "DRIVE_WRITE_PRECHECK_FAILED",
            "写驱动前置检查失败，未写驱动。",
            {"failures": failures},
        )
