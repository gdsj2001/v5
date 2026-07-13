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
from v5_drive_sdo import read_command, run_command, run_ethercat_slaves, write_command

SET_DRIVE_BATCH_READBACK_ATTEMPTS = 2
SET_DRIVE_BATCH_RECOVERY_DELAY_S = 0.25
FACTORY_RESET_STABILIZE_ATTEMPTS = 8
FACTORY_RESET_STABILIZE_DELAY_S = 2.0
FAULT_RESET_RECOVERY_ATTEMPTS = 3
FAULT_RESET_RECOVERY_DELAY_S = 0.25

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


def recover_slave_mailbox(position: str, timeout_s: float) -> Dict[str, Any]:
    operations: List[Dict[str, Any]] = []
    for state in ("PREOP", "OP"):
        op = run_command(["ethercat", "state", "-p", str(position), state], min(timeout_s, 5.0))
        op["requested_state"] = state
        compact = compact_sdo_io(op)
        compact["requested_state"] = state
        operations.append(compact)
        time.sleep(0.35)
    return {"ok": bool(operations and operations[-1].get("ok")), "operations": operations}


def readback_with_retry(target: Dict[str, Any],
                        timeout_s: float,
                        expected_egear: Tuple[int, int] | None = None,
                        expected_mode: int | None = None,
                        attempts: int = 3) -> Dict[str, Any]:
    attempt_results: List[Dict[str, Any]] = []
    recovery: Dict[str, Any] | None = None
    max_attempts = max(1, attempts)
    for attempt in range(1, max_attempts + 1):
        state = read_required_state(target, timeout_s)
        health = evaluate_drive_health(state.get("reads", {}), expected_egear, expected_mode)
        compact_reads = {name: compact_read_item(item) for name, item in (state.get("reads", {}) or {}).items()}
        attempt_payload = {"attempt": attempt, "read_ok": bool(state.get("ok")), "health": health, "reads": compact_reads}
        attempt_results.append(attempt_payload)
        if bool(state.get("ok")) and bool(health.get("ok")):
            return {"ok": True, "attempts": attempt_results, "health": health}
        if attempt == 1 and attempt < max_attempts:
            recovery = recover_slave_mailbox(str(target.get("position") or ""), timeout_s)
        time.sleep(0.25)
    last_health = attempt_results[-1].get("health", {}) if attempt_results else {}
    return {"ok": False, "attempts": attempt_results, "health": last_health, "mailbox_recovery": recovery}


def recover_failed_target_set_mailbox(targets: List[Dict[str, Any]], timeout_s: float) -> Dict[str, Any]:
    operations: List[Dict[str, Any]] = []
    for state in ("PREOP", "OP"):
        for target in targets:
            position = str(target.get("position") or "")
            op = run_command(["ethercat", "state", "-p", position, state], min(timeout_s, 5.0))
            op.update({"requested_state": state, "position": position})
            compact = compact_sdo_io(op)
            compact.update({"requested_state": state, "position": position})
            operations.append(compact)
        time.sleep(0.35)
    return {"ok": bool(operations) and all(bool(op.get("ok")) for op in operations), "operations": operations}


def set_drive_batch_readback(targets: List[Dict[str, Any]],
                             timeout_s: float,
                             expectations: Dict[str, Tuple[Tuple[int, int], int]]) -> Dict[str, Any]:
    cycles: List[Dict[str, Any]] = []
    final_readbacks: Dict[str, Dict[str, Any]] = {}
    final_failed_positions: List[str] = []
    for attempt in range(1, SET_DRIVE_BATCH_READBACK_ATTEMPTS + 1):
        scan = run_ethercat_slaves(min(timeout_s, 5.0))
        if not scan.get("ok"):
            return {"ok": False, "code": str(scan.get("code") or "DRIVE_SCAN_FAILED"), "scan": scan, "cycles": cycles, "readbacks": final_readbacks, "failed_positions": [str(target.get("position") or "") for target in targets]}
        states = {str(item.get("position") or ""): str(item.get("state") or "").upper() for item in (scan.get("slaves") or []) if isinstance(item, dict)}
        readbacks: Dict[str, Dict[str, Any]] = {}
        actual_fault = False
        failed_targets: List[Dict[str, Any]] = []
        for target in targets:
            position = str(target.get("position") or "")
            expected_egear, expected_mode = expectations[position]
            readback = readback_with_retry(target, timeout_s, expected_egear, expected_mode, attempts=1)
            readbacks[position] = readback
            failure_codes = {str(item.get("code") or "") for item in (readback.get("health", {}).get("failures") or []) if isinstance(item, dict)}
            actual_fault = actual_fault or bool(failure_codes & {"DRIVE_STATUSWORD_FAULT_BIT_SET", "DRIVE_ERROR_CODE_NONZERO", "DRIVE_AUX_ERROR_CODE_NONZERO"})
            if states.get(position) != "OP" or not readback.get("ok"):
                failed_targets.append(target)
        final_readbacks = readbacks
        final_failed_positions = [str(target.get("position") or "") for target in failed_targets]
        all_readback_ok = bool(readbacks) and all(bool(item.get("ok")) for item in readbacks.values())
        cycle: Dict[str, Any] = {"attempt": attempt, "all_targets_op": all(states.get(str(target.get("position") or "")) == "OP" for target in targets), "all_readback_ok": all_readback_ok, "actual_fault": actual_fault}
        cycles.append(cycle)
        if not failed_targets and all_readback_ok:
            return {"ok": True, "code": "DRIVE_SET_BATCH_READBACK_OK", "cycles": cycles, "readbacks": final_readbacks, "failed_positions": []}
        if actual_fault:
            break
        if failed_targets and attempt < SET_DRIVE_BATCH_READBACK_ATTEMPTS:
            recovery = recover_failed_target_set_mailbox(failed_targets, timeout_s)
            cycle["recovery_ok"] = bool(recovery.get("ok"))
            cycle["recovery_positions"] = [str(target.get("position") or "") for target in failed_targets]
            if not recovery.get("ok"):
                break
            time.sleep(SET_DRIVE_BATCH_RECOVERY_DELAY_S)
    return {"ok": False, "code": "DRIVE_SET_BATCH_READBACK_FAILED", "cycles": cycles, "readbacks": final_readbacks, "failed_positions": final_failed_positions}


def summarize_reset_recovery_cycle(attempt: int,
                                   readback: Dict[str, Any],
                                   fault_reset: Dict[str, Any] | None = None,
                                   op_recovery: Dict[str, Any] | None = None) -> Dict[str, Any]:
    health = readback.get("health") if isinstance(readback.get("health"), dict) else {}
    payload: Dict[str, Any] = {
        "attempt": attempt,
        "ok": bool(readback.get("ok")),
        "statusword": health.get("statusword"),
        "error_code": health.get("error_code"),
        "aux_error_code": health.get("aux_error_code"),
        "egear_numerator": health.get("egear_numerator"),
        "egear_denominator": health.get("egear_denominator"),
    }
    if isinstance(fault_reset, dict):
        payload["fault_reset_ok"] = bool(fault_reset.get("ok"))
        payload["fault_reset_code"] = str(fault_reset.get("code") or "")
    if isinstance(op_recovery, dict):
        payload["op_recovery_ok"] = bool(op_recovery.get("ok"))
    return payload


def fault_reset_readback_with_recovery(target: Dict[str, Any],
                                       timeout_s: float,
                                       expected_egear: Tuple[int, int] | None = None,
                                       expected_mode: int | None = None) -> Dict[str, Any]:
    commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
    reset_command = commands.get("drive.reset_fault", {})
    position = str(target.get("position") or "")
    cycles: List[Dict[str, Any]] = []
    last_write: Dict[str, Any] = {"ok": False, "code": "DRIVE_FAULT_RESET_NOT_ATTEMPTED"}
    last_readback: Dict[str, Any] = {"ok": False, "health": {}}
    for attempt in range(1, FAULT_RESET_RECOVERY_ATTEMPTS + 1):
        last_write = write_command(position, "drive.reset_fault", reset_command)
        last_readback = readback_with_retry(
            target, timeout_s, expected_egear, expected_mode, attempts=1) if last_write.get("ok") else {"ok": False, "health": {}}
        op_recovery: Dict[str, Any] | None = None
        if not last_readback.get("ok") and attempt < FAULT_RESET_RECOVERY_ATTEMPTS:
            op_recovery = recover_slave_mailbox(position, timeout_s)
        cycles.append(summarize_reset_recovery_cycle(attempt, last_readback, last_write, op_recovery))
        if last_readback.get("ok"):
            return {
                "ok": True,
                "fault_reset_write": last_write,
                "readback": last_readback,
                "summary": {"ok": True, "attempt_count": attempt, "settle_delay_s": FAULT_RESET_RECOVERY_DELAY_S, "cycles": cycles},
            }
        if attempt < FAULT_RESET_RECOVERY_ATTEMPTS:
            time.sleep(FAULT_RESET_RECOVERY_DELAY_S)
    return {
        "ok": False,
        "fault_reset_write": last_write,
        "readback": last_readback,
        "summary": {"ok": False, "attempt_count": len(cycles), "settle_delay_s": FAULT_RESET_RECOVERY_DELAY_S, "cycles": cycles},
    }


def factory_reset_readback_with_recovery(target: Dict[str, Any], timeout_s: float) -> Dict[str, Any]:
    commands = target.get("commands") if isinstance(target.get("commands"), dict) else {}
    reset_command = commands.get("drive.reset_fault", {})
    reset_supported = isinstance(reset_command, dict) and reset_command.get("supported") is not False
    cycles: List[Dict[str, Any]] = []
    last_readback: Dict[str, Any] = {"ok": False, "health": {}}
    for attempt in range(1, FACTORY_RESET_STABILIZE_ATTEMPTS + 1):
        if attempt > 1:
            time.sleep(FACTORY_RESET_STABILIZE_DELAY_S)
        readback = readback_with_retry(target, timeout_s, attempts=1)
        last_readback = readback
        if readback.get("ok"):
            cycles.append(summarize_reset_recovery_cycle(attempt, readback))
            return {
                "ok": True,
                "readback": readback,
                "summary": {
                    "ok": True,
                    "attempt_count": attempt,
                    "settle_delay_s": FACTORY_RESET_STABILIZE_DELAY_S,
                    "cycles": cycles,
                },
            }
        fault_reset: Dict[str, Any] | None = None
        op_recovery: Dict[str, Any] | None = None
        if reset_supported:
            fault_reset = write_command(str(target.get("position") or ""), "drive.reset_fault", reset_command)
            if not fault_reset.get("ok"):
                op_recovery = recover_slave_mailbox(str(target.get("position") or ""), timeout_s)
        cycles.append(summarize_reset_recovery_cycle(attempt, readback, fault_reset, op_recovery))
    return {
        "ok": False,
        "readback": last_readback,
        "summary": {
            "ok": False,
            "attempt_count": len(cycles),
            "settle_delay_s": FACTORY_RESET_STABILIZE_DELAY_S,
            "cycles": cycles,
        },
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
