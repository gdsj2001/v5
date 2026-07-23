from __future__ import annotations

import math
import os
import time
from typing import Any, Dict, List, Tuple

import v5_drive_enable_window
from v5_drive_feedforward_recovery import (
    prepare_frozen_set_for_reset,
    recover_frozen_set_after_reset,
)
from v5_drive_bus_contract import DriveActionError, now_utc
from v5_drive_health import (
    fault_reset_batch,
    precheck_targets_for_write,
    read_scalar_value,
)
from v5_drive_query import configured_drive_targets
from v5_drive_result import compact_error_detail
from v5_drive_runtime_store import persist_settings_runtime
from v5_drive_sdo import read_command, write_command


READ_SOURCE = "drive.read_velocity_feedforward_source"
READ_FILTER = "drive.read_velocity_feedforward_filter"
READ_GAIN = "drive.read_velocity_feedforward_gain"
WRITE_GAIN = "drive.write_velocity_feedforward_gain"
READ_EEPROM = "drive.read_communication_eeprom_policy"
WRITE_EEPROM = "drive.write_communication_eeprom_policy"
SOFTWARE_RESET = "drive.software_reset"
READ_ERROR = "drive.read_error_code"
GAIN_RAW_MIN = 0
GAIN_RAW_MAX = 1000
GAIN_RAW_MAX_STEP = 100

def _request_gain_raw(request: Dict[str, Any]) -> int:
    if "target_gain_raw" in request:
        value = request.get("target_gain_raw")
        if isinstance(value, bool):
            raise ValueError("boolean gain is invalid")
        raw = int(value)
        if isinstance(value, float) and not value.is_integer():
            raise ValueError("target_gain_raw must be an integer")
    else:
        value = request.get("target_gain_percent")
        if isinstance(value, bool):
            raise ValueError("boolean gain is invalid")
        percent = float(value)
        if not math.isfinite(percent):
            raise ValueError("target_gain_percent must be finite")
        scaled = percent * 10.0
        raw = int(round(scaled))
        if abs(scaled - raw) > 1.0e-6:
            raise ValueError("target_gain_percent must use 0.1% steps")
    if raw < GAIN_RAW_MIN or raw > GAIN_RAW_MAX:
        raise ValueError("target gain is outside 0.0%..100.0%")
    return raw


def _select_target(
    targets: List[Dict[str, Any]], request: Dict[str, Any],
) -> Tuple[Dict[str, Any], str]:
    axis = str(request.get("axis") or "").strip().upper()
    if not axis:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_AXIS_REQUIRED",
            "逐轴速度前馈维护必须指定一个轴。",
            {"axis": request.get("axis")},
        )
    matches = [
        target for target in targets
        if str(target.get("axis") or "").strip().upper() == axis
    ]
    if len(matches) != 1:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_AXIS_NOT_UNIQUE",
            "逐轴速度前馈维护未找到唯一已绑定轴，未写驱动。",
            {"axis": axis, "matches": len(matches)},
        )
    return matches[0], axis


def _read_scalar(
    target: Dict[str, Any], command_name: str,
) -> Tuple[int, Dict[str, Any]]:
    commands = target.get("commands")
    commands = commands if isinstance(commands, dict) else {}
    result = read_command(
        str(target.get("position") or ""),
        command_name,
        commands.get(command_name, {}),
        True,
    )
    value = read_scalar_value(result)
    if not result.get("ok") or value is None:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_READ_FAILED",
            "逐轴速度前馈维护的必需 SDO 读回失败。",
            {
                "axis": target.get("axis"),
                "position": target.get("position"),
                "command": command_name,
                "read": result,
            },
        )
    return int(value), result


def _write_scalar(
    target: Dict[str, Any], command_name: str, value: int,
) -> Dict[str, Any]:
    commands = target.get("commands")
    commands = commands if isinstance(commands, dict) else {}
    result = write_command(
        str(target.get("position") or ""),
        command_name,
        commands.get(command_name, {}),
        value,
    )
    if not result.get("ok"):
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_WRITE_FAILED",
            "逐轴速度前馈维护的白名单 SDO 写入失败。",
            {
                "axis": target.get("axis"),
                "position": target.get("position"),
                "command": command_name,
                "value": value,
                "write": result,
            },
        )
    return result


def _write_and_verify(
    target: Dict[str, Any],
    write_name: str,
    read_name: str,
    value: int,
) -> Dict[str, Any]:
    write = _write_scalar(target, write_name, value)
    readback, read = _read_scalar(target, read_name)
    if readback != value:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_READBACK_MISMATCH",
            "逐轴速度前馈维护写入后的源位置读回不一致。",
            {
                "axis": target.get("axis"),
                "position": target.get("position"),
                "command": write_name,
                "expected": value,
                "actual": readback,
            },
        )
    return {"write": write, "read": read, "value": readback}


def _error_code_readback(
    targets: List[Dict[str, Any]],
) -> Tuple[Dict[str, int], List[Dict[str, Any]]]:
    values: Dict[str, int] = {}
    failures: List[Dict[str, Any]] = []
    for target in targets:
        position = str(target.get("position") or "")
        try:
            value, _read = _read_scalar(target, READ_ERROR)
            values[position] = value
            if value != 0:
                failures.append({
                    "axis": target.get("axis"),
                    "position": position,
                    "error_code": value,
                })
        except DriveActionError as exc:
            failures.append({
                "axis": target.get("axis"),
                "position": position,
                "code": exc.code,
                "detail": compact_error_detail(exc.detail),
            })
    return values, failures


def _reset_recover_and_read(
    target: Dict[str, Any],
    targets: List[Dict[str, Any]],
    timeout_s: float,
    expected_gain: int,
) -> Dict[str, Any]:
    pre_reset = prepare_frozen_set_for_reset(targets, timeout_s)
    reset = _write_scalar(target, SOFTWARE_RESET, 1)
    recovery = recover_frozen_set_after_reset(targets, timeout_s)
    if not recovery.get("ok"):
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_TARGET_SET_RECOVERY_FAILED",
            "速度前馈写入后未能恢复全部从站到 OP。",
            recovery,
        )
    errors, error_failures = _error_code_readback(targets)
    fault_reset: Dict[str, Any] = {}
    if error_failures:
        fault_reset = fault_reset_batch(targets, timeout_s)
        if fault_reset.get("ok"):
            errors, error_failures = _error_code_readback(targets)
    gain, gain_read = _read_scalar(target, READ_GAIN)
    policy, policy_read = _read_scalar(target, READ_EEPROM)
    if gain != expected_gain or policy != 0 or error_failures:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_POST_RESET_READBACK_FAILED",
            "software reset 后的速度前馈持久值、EEPROM 策略或错误码未闭合。",
            {
                "expected_gain_raw": expected_gain,
                "gain_raw": gain,
                "eeprom_policy": policy,
                "error_codes": errors,
                "error_failures": error_failures,
            },
        )
    return {
        "pre_reset_full_set": pre_reset,
        "software_reset": reset,
        "target_set_recovery": recovery,
        "gain_raw": gain,
        "gain_read": gain_read,
        "eeprom_policy": policy,
        "eeprom_policy_read": policy_read,
        "error_codes": errors,
        "fault_reset": fault_reset,
    }


def _commit_gain(
    target: Dict[str, Any],
    targets: List[Dict[str, Any]],
    timeout_s: float,
    gain_raw: int,
) -> Dict[str, Any]:
    policy_one: Dict[str, Any] = {}
    gain_write: Dict[str, Any] = {}
    policy_zero: Dict[str, Any] = {}
    primary_error: BaseException | None = None
    try:
        policy_one = _write_and_verify(
            target, WRITE_EEPROM, READ_EEPROM, 1)
        gain_write = _write_and_verify(
            target, WRITE_GAIN, READ_GAIN, gain_raw)
    except BaseException as exc:
        primary_error = exc
    try:
        policy_zero = _write_and_verify(
            target, WRITE_EEPROM, READ_EEPROM, 0)
    except BaseException as exc:
        if primary_error is None:
            primary_error = exc
        else:
            raise DriveActionError(
                "DRIVE_FEEDFORWARD_WRITE_AND_POLICY_RESTORE_FAILED",
                "速度前馈写入失败且 EEPROM 策略未能恢复为 0。",
                {
                    "primary": "%s: %s" % (
                        type(primary_error).__name__, primary_error),
                    "policy_restore": "%s: %s" % (
                        type(exc).__name__, exc),
                },
            ) from exc
    if primary_error is not None:
        raise primary_error
    reset_readback = _reset_recover_and_read(
        target, targets, timeout_s, gain_raw)
    return {
        "policy_one": policy_one,
        "gain_write": gain_write,
        "policy_zero": policy_zero,
        "post_reset": reset_readback,
    }


def _baseline_readback(target: Dict[str, Any]) -> Dict[str, Any]:
    source, _source_read = _read_scalar(target, READ_SOURCE)
    filter_raw, _filter_read = _read_scalar(target, READ_FILTER)
    gain_raw, _gain_read = _read_scalar(target, READ_GAIN)
    policy, _policy_read = _read_scalar(target, READ_EEPROM)
    if source != 1:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_SOURCE_NOT_INTERNAL",
            "H05.19 未读回 1，当前驱动不是内部速度前馈来源，拒绝整定。",
            {"source_raw": source},
        )
    if not GAIN_RAW_MIN <= gain_raw <= GAIN_RAW_MAX:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_GAIN_OUT_OF_RANGE",
            "H08.19 当前读回超出 0.0%..100.0%，拒绝整定。",
            {"gain_raw": gain_raw},
        )
    if policy < 0 or policy > 4:
        raise DriveActionError(
            "DRIVE_FEEDFORWARD_EEPROM_POLICY_INVALID",
            "H0E.01 当前值不在厂家定义的 0..4，拒绝继续整定。",
            {"eeprom_policy": policy},
        )
    return {
        "source_raw": source,
        "filter_raw": filter_raw,
        "gain_raw": gain_raw,
        "eeprom_policy": policy,
    }


def run_velocity_feedforward_commission(
    timeout_s: float,
    request: Dict[str, Any] | None = None,
) -> Dict[str, Any]:
    request_payload = dict(request) if isinstance(request, dict) else {}
    run_id = str(
        request_payload.get("_run_id")
        or request_payload.get("run_id")
        or "direct-%d-%d" % (os.getpid(), time.monotonic_ns())
    )
    window_started = False
    window_begin: Dict[str, Any] = {}
    window_finish: Dict[str, Any] = {}
    write_attempted = False
    baseline: Dict[str, Any] = {}
    rollback: Dict[str, Any] = {}
    target: Dict[str, Any] = {}
    result: Dict[str, Any]
    try:
        try:
            target_gain_raw = _request_gain_raw(request_payload)
        except (TypeError, ValueError) as exc:
            raise DriveActionError(
                "DRIVE_FEEDFORWARD_TARGET_INVALID",
                "速度前馈目标必须是 0.1% 精度的 0.0%..100.0%。",
                {"detail": str(exc)},
            ) from exc
        targets, runtime, scan = configured_drive_targets(timeout_s)
        target, axis = _select_target(targets, request_payload)
        window_begin = v5_drive_enable_window.begin(run_id, timeout_s)
        window_started = True
        prechecks = precheck_targets_for_write(
            [target],
            [WRITE_EEPROM, WRITE_GAIN, SOFTWARE_RESET],
            timeout_s,
            wait_disabled_transition=True,
        )
        baseline = _baseline_readback(target)
        if abs(target_gain_raw - int(baseline["gain_raw"])) > GAIN_RAW_MAX_STEP:
            raise DriveActionError(
                "DRIVE_FEEDFORWARD_STEP_TOO_LARGE",
                "H08.19 单次最多增加或减少 10.0%，未写驱动。",
                {
                    "current_gain_raw": baseline["gain_raw"],
                    "target_gain_raw": target_gain_raw,
                    "max_step_raw": GAIN_RAW_MAX_STEP,
                },
            )
        if target_gain_raw == int(baseline["gain_raw"]):
            raise DriveActionError(
                "DRIVE_FEEDFORWARD_TARGET_UNCHANGED",
                "H08.19 目标与当前值相同，未执行 EEPROM 写入。",
                {"gain_raw": target_gain_raw},
            )
        write_attempted = True
        transaction = _commit_gain(
            target, targets, timeout_s, target_gain_raw)
        post_reset = transaction["post_reset"]
        evidence = {
            "ok": True,
            "code": "DRIVE_FEEDFORWARD_COMMISSION_OK",
            "updated_at": now_utc(),
            "axis": axis,
            "slave_index": target.get("position"),
            "profile_id": (target.get("profile") or {}).get("profile_id", ""),
            "profile_map_source": (
                target.get("profile") or {}).get("map_source", ""),
            "profile_map_sha256": (
                target.get("profile") or {}).get("profile_map_sha256", ""),
            "profile_snapshot_generated_at": (
                scan.get("profile_snapshot") or {}).get("generated_at"),
            "source_raw": baseline["source_raw"],
            "filter_raw": baseline["filter_raw"],
            "gain_before_raw": baseline["gain_raw"],
            "gain_target_raw": target_gain_raw,
            "gain_after_reset_raw": post_reset["gain_raw"],
            "eeprom_policy_before": baseline["eeprom_policy"],
            "eeprom_policy_final": post_reset["eeprom_policy"],
            "all_targets_op": bool(
                post_reset["target_set_recovery"].get("ok")),
            "error_codes": post_reset["error_codes"],
        }
        target["axis_cfg"]["velocity_feedforward_evidence"] = evidence
        persistence = persist_settings_runtime(runtime)
        result = {
            "ok": True,
            "code": "DRIVE_FEEDFORWARD_COMMISSION_OK",
            "message_cn": (
                "逐轴速度前馈已写入并在 software reset 后读回；"
                "当前保持 Machine Off，彻底重启后方可运动。"),
            "axis": axis,
            "position": target.get("position"),
            "baseline": baseline,
            "target_gain_raw": target_gain_raw,
            "target_gain_percent": target_gain_raw / 10.0,
            "transaction": transaction,
            "prechecks": prechecks,
            "scan": scan,
            "settings_runtime_writeback": persistence,
            "write_executed": True,
            "drive_write_executed": True,
            "motion_executed": False,
        }
    except BaseException as exc:
        if write_attempted and target and baseline.get("gain_raw") is not None:
            try:
                recovery_before_rollback = recover_frozen_set_after_reset(
                    targets, timeout_s)
                if not recovery_before_rollback.get("ok"):
                    raise DriveActionError(
                        "DRIVE_FEEDFORWARD_ROLLBACK_MAILBOX_FAILED",
                        "回滚前未能恢复全部从站 mailbox。",
                        recovery_before_rollback,
                    )
                rollback_transaction = _commit_gain(
                    target, targets, timeout_s, int(baseline["gain_raw"]))
                rollback = {
                    "ok": True,
                    "code": "DRIVE_FEEDFORWARD_ROLLBACK_OK",
                    "recovery_before_rollback": recovery_before_rollback,
                    "transaction": rollback_transaction,
                }
            except BaseException as rollback_exc:
                rollback = {
                    "ok": False,
                    "code": "DRIVE_FEEDFORWARD_ROLLBACK_FAILED",
                    "detail": "%s: %s" % (
                        type(rollback_exc).__name__, rollback_exc),
                }
        if isinstance(exc, DriveActionError):
            code = exc.code
            message = exc.message_cn
            detail = compact_error_detail(exc.detail)
        else:
            code = "DRIVE_FEEDFORWARD_EXCEPTION"
            message = "逐轴速度前馈维护异常终止；已保持 Machine Off。"
            detail = "%s: %s" % (type(exc).__name__, exc)
        result = {
            "ok": False,
            "code": code,
            "message_cn": message,
            "detail": detail,
            "axis": str(request_payload.get("axis") or "").upper(),
            "baseline": baseline,
            "rollback": rollback,
            "write_executed": write_attempted,
            "drive_write_executed": write_attempted,
            "motion_executed": False,
        }
    finally:
        if window_started:
            window_finish = v5_drive_enable_window.finish_safely(
                run_id, timeout_s, restore=False)
    if window_started:
        final_off_confirmed = bool(window_finish.get("ok")) and not bool(
            window_finish.get("final_machine_enabled"))
        result["drive_write_window"] = {
            "begin": window_begin,
            "finish": window_finish,
            "restore_requested": False,
            "final_off_confirmed": final_off_confirmed,
        }
        if not final_off_confirmed:
            result["ok"] = False
            result["code"] = "DRIVE_FEEDFORWARD_WINDOW_CLOSE_FAILED"
            result["message_cn"] = (
                "速度前馈动作已结束，但 native 安全窗口未确认最终 Machine Off。")
    if write_attempted:
        result["restart_required"] = True
        result["restart_deferred"] = True
        result["backend_restart_required"] = True
        result["canonical_clean_restart_required"] = True
    return result
