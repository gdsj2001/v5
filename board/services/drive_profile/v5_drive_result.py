from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

import v5_drive_bus_contract as contract
from v5_drive_bus_contract import MAX_RESULT_JSON_BYTES, now_utc

def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    if path.parent == contract.RUN_DIR and len(text.encode("utf-8")) > MAX_RESULT_JSON_BYTES:
        payload = compact_action_result_payload(payload)
        text = json.dumps(payload, ensure_ascii=False, indent=2)
    if path.parent == contract.RUN_DIR and len(text.encode("utf-8")) > MAX_RESULT_JSON_BYTES:
        payload = {
            "schema": str(payload.get("schema") or "v5.drive_bus_action.v1"),
            "generated_at": now_utc(),
            "action": str(payload.get("action") or ""),
            "ok": False,
            "code": "DRIVE_ACTION_RESULT_BUDGET_EXCEEDED",
            "message_cn": "驱动动作结果超过常驻内存预算，已 fail-closed。",
            "write_executed": False,
            "motion_executed": False,
            "result_bytes": len(text.encode("utf-8")),
            "max_result_bytes": MAX_RESULT_JSON_BYTES,
        }
        text = json.dumps(payload, ensure_ascii=False, indent=2)
    tmp.write_text(text + "\n", encoding="utf-8")
    tmp.replace(path)


def compact_sdo_io(item: Any) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return {"ok": False, "code": "BAD_SDO_RESULT"}
    compact: Dict[str, Any] = {
        "ok": bool(item.get("ok")),
        "code": str(item.get("code") or ""),
        "returncode": item.get("returncode"),
        "index": item.get("index"),
        "subindex": item.get("subindex"),
        "data_type": item.get("data_type"),
    }
    if "value" in item:
        compact["value"] = item.get("value")
    if "write_value" in item:
        compact["write_value"] = item.get("write_value")
    stderr = str(item.get("stderr") or "")
    stdout = str(item.get("stdout") or "")
    if stderr:
        compact["stderr_tail"] = stderr[-240:]
    if stdout and not item.get("ok"):
        compact["stdout_tail"] = stdout[-240:]
    return compact


def compact_write_result(item: Any) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return {"ok": False, "code": "BAD_WRITE_RESULT"}
    operations = item.get("operations") if isinstance(item.get("operations"), list) else []
    compact: Dict[str, Any] = {
        "ok": bool(item.get("ok")),
        "code": str(item.get("code") or ""),
        "standard_command": item.get("standard_command"),
        "operation_count": len(operations),
    }
    if operations:
        compact["operations"] = [
            compact_sdo_io(op) if isinstance(op, dict) else {"ok": False, "code": "BAD_SDO_RESULT"}
            for op in operations[:4]
        ]
    return compact


def compact_read_item(item: Any) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return {"ok": False, "code": "BAD_READ_RESULT"}
    compact: Dict[str, Any] = {
        "ok": bool(item.get("ok")),
        "supported": item.get("supported"),
        "required": item.get("required"),
        "code": str(item.get("code") or ""),
    }
    if isinstance(item.get("upload"), dict):
        compact["upload"] = compact_sdo_io(item.get("upload"))
    if isinstance(item.get("numerator"), dict):
        compact["numerator"] = compact_sdo_io(item.get("numerator"))
    if isinstance(item.get("denominator"), dict):
        compact["denominator"] = compact_sdo_io(item.get("denominator"))
    return compact


def compact_health(health: Any) -> Dict[str, Any]:
    if not isinstance(health, dict):
        return {"ok": False, "code": "BAD_HEALTH_RESULT"}
    compact: Dict[str, Any] = {
        "ok": bool(health.get("ok")),
        "failures": health.get("failures") if isinstance(health.get("failures"), list) else [],
    }
    for key in (
        "statusword",
        "error_code",
        "aux_error_code",
        "mode_of_operation",
        "actual_position_counts",
        "egear_numerator",
        "egear_denominator",
    ):
        if key in health:
            compact[key] = health.get(key)
    return compact


def append_batch_readback_context(compact: Dict[str, Any], readback: Dict[str, Any]) -> None:
    if "slave_state" in readback:
        compact["slave_state"] = str(readback.get("slave_state") or "")
    required_failures = readback.get("required_read_failures") if isinstance(readback.get("required_read_failures"), list) else []
    if required_failures:
        compact["required_read_failures"] = [
            {key: item.get(key) for key in ("command", "code") if key in item}
            for item in required_failures[:8] if isinstance(item, dict)
        ]
    if "recovery_reason" in readback:
        compact["recovery_reason"] = str(readback.get("recovery_reason") or "")
    first_failure = readback.get("first_failure") if isinstance(readback.get("first_failure"), dict) else {}
    if first_failure:
        compact["first_failure"] = {
            key: first_failure.get(key)
            for key in ("command", "field", "code", "value", "actual", "expected", "aux_error_code")
            if key in first_failure
        }


def compact_readback(readback: Any) -> Dict[str, Any]:
    if not isinstance(readback, dict):
        return {"ok": False, "code": "BAD_READBACK_RESULT"}
    attempts = readback.get("attempts") if isinstance(readback.get("attempts"), list) else []
    compact: Dict[str, Any] = {
        "ok": bool(readback.get("ok")),
        "attempt_count": len(attempts),
        "health": compact_health(readback.get("health")),
    }
    if attempts:
        last = attempts[-1] if isinstance(attempts[-1], dict) else {}
        compact["last_attempt"] = {
            "attempt": last.get("attempt"),
            "read_ok": bool(last.get("read_ok")),
            "health": compact_health(last.get("health")),
        }
    append_batch_readback_context(compact, readback)
    return compact


def compact_error_detail(detail: Any) -> Any:
    if isinstance(detail, dict) and ("attempts" in detail or "health" in detail):
        return compact_readback(detail)
    if isinstance(detail, dict):
        clean: Dict[str, Any] = {}
        for key, value in detail.items():
            if key == "readback":
                clean[key] = compact_readback(value)
            elif key in {"read", "status_read", "velocity_read"}:
                clean[key] = compact_read_item(value)
            elif key in {"factory_reset_write", "fault_reset_write", "egear_write", "mode_write", "save_parameters_write"}:
                clean[key] = compact_write_result(value)
            elif key == "failures" and isinstance(value, list):
                clean[key] = [compact_target_result(item) if isinstance(item, dict) else item for item in value]
            elif isinstance(value, (str, int, float, bool)) or value is None:
                clean[key] = value
            elif isinstance(value, list):
                clean[key] = value[:8]
            elif isinstance(value, dict):
                clean[key] = {str(k): v for k, v in list(value.items())[:12] if isinstance(v, (str, int, float, bool)) or v is None}
            else:
                clean[key] = str(value)
        return clean
    return detail


def compact_target_result(item: Any) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return {"ok": False, "code": "BAD_TARGET_RESULT"}
    keep = (
        "ok",
        "code",
        "message_cn",
        "axis",
        "position",
        "profile_id",
        "target_mode",
        "mode_write_status",
        "profile_save_required",
        "profile_activation_status",
        "batch_readback_attempt_count",
        "batch_recovery_attempted",
        "slave_state",
    )
    compact = {key: item.get(key) for key in keep if key in item}
    if "target_egear" in item:
        compact["target_egear"] = item.get("target_egear")
    for key in ("factory_reset_write", "fault_reset_write", "egear_write", "mode_write", "save_parameters_write"):
        if key in item:
            compact[key] = compact_write_result(item.get(key))
    if "write_readback" in item:
        compact["write_readback"] = compact_readback(item.get("write_readback"))
    if "mode_pre_readback" in item and isinstance(item.get("mode_pre_readback"), dict):
        mode_pre = item.get("mode_pre_readback") or {}
        compact["mode_pre_readback"] = {
            "ok": bool(mode_pre.get("ok")),
            "mode": mode_pre.get("mode"),
            "reads": {name: compact_read_item(read_item) for name, read_item in (mode_pre.get("reads", {}) or {}).items()} if isinstance(mode_pre.get("reads"), dict) else {},
        }
    if "readback" in item:
        compact["readback"] = compact_readback(item.get("readback"))
    if "reads" in item and isinstance(item.get("reads"), dict):
        compact["reads"] = {
            str(name): compact_read_item(read_item)
            for name, read_item in list(item.get("reads", {}).items())[:16]
        }
    if "health" in item:
        compact["health"] = compact_health(item.get("health"))
    if "detail" in item:
        compact["detail"] = compact_error_detail(item.get("detail"))
    return compact


def compact_precheck_result(item: Any) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return {"ok": False, "code": "BAD_PRECHECK_RESULT"}
    compact = {key: item.get(key) for key in ("ok", "code", "message_cn", "axis", "position") if key in item}
    safety = item.get("safety") if isinstance(item.get("safety"), dict) else {}
    if safety:
        compact["safety"] = {
            "ok": bool(safety.get("ok")),
            "statusword": safety.get("statusword"),
            "velocity": safety.get("velocity"),
            "no_motion_source": safety.get("no_motion_source"),
        }
    if "detail" in item:
        compact["detail"] = compact_error_detail(item.get("detail"))
    return compact


def compact_scan_result(scan: Any) -> Dict[str, Any]:
    if not isinstance(scan, dict):
        return {"ok": False, "code": "BAD_SCAN_RESULT"}
    slaves = scan.get("slaves") if isinstance(scan.get("slaves"), list) else []
    compact_slaves = []
    for slave in slaves[:16]:
        if isinstance(slave, dict):
            compact_slaves.append({key: slave.get(key) for key in ("position", "alias", "state", "name") if key in slave})
        else:
            compact_slaves.append(slave)
    return {
        "ok": bool(scan.get("ok")),
        "code": str(scan.get("code") or ""),
        "slave_count": len(slaves),
        "slaves": compact_slaves,
    }


def compact_action_result_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
    compact: Dict[str, Any] = {}
    for key in (
        "schema",
        "generated_at",
        "action",
        "ok",
        "code",
        "message_cn",
        "display_message_cn",
        "failed_stage",
        "write_executed",
        "drive_write_executed",
        "motion_executed",
        "restart_required",
        "restart_deferred",
        "backend_restart_required",
        "raw_limit_live_verified",
        "raw_runtime_zero_verified",
        "memory_zero_verified",
        "snapshot_generated_at",
        "snapshot_profile_count",
    ):
        if key in payload:
            compact[key] = payload.get(key)
    if isinstance(payload.get("detail"), dict):
        compact["detail"] = compact_error_detail(payload.get("detail"))
    elif "detail" in payload:
        compact["detail"] = payload.get("detail")
    if isinstance(payload.get("targets"), list):
        compact["targets"] = [compact_target_result(item) for item in payload.get("targets", [])]
    if isinstance(payload.get("failures"), list):
        compact["failures"] = [compact_target_result(item) for item in payload.get("failures", [])]
    if isinstance(payload.get("prechecks"), list):
        compact["prechecks"] = [compact_precheck_result(item) for item in payload.get("prechecks", [])]
    if isinstance(payload.get("scan"), dict):
        compact["scan"] = compact_scan_result(payload.get("scan"))
    if isinstance(payload.get("recovery_positions"), list):
        compact["recovery_positions"] = [
            str(position) for position in payload.get("recovery_positions", [])[:16]
        ]
    if isinstance(payload.get("drive_write_window"), dict):
        window = payload.get("drive_write_window") or {}
        compact_window: Dict[str, Any] = {
            key: window.get(key)
            for key in (
                "restore_requested", "restore_expected", "restore_confirmed",
                "final_off_confirmed",
            )
            if key in window
        }
        for stage_name in ("begin", "finish"):
            stage = window.get(stage_name)
            if isinstance(stage, dict):
                compact_window[stage_name] = {
                    key: stage.get(key)
                    for key in (
                        "ok", "code", "run_id", "initial_machine_enabled",
                        "final_machine_enabled",
                    )
                    if key in stage
                }
        compact["drive_write_window"] = compact_window
    for batch_key in ("factory_reset_batch", "fault_reset_batch"):
        batch = payload.get(batch_key)
        if isinstance(batch, dict):
            compact[batch_key] = {
                "code": str(batch.get("code") or ""),
                "message_cn": str(batch.get("message_cn") or ""),
                "recovery_positions": [
                    str(position)
                    for position in (batch.get("recovery_positions") or [])[:16]
                ],
            }
    for key in ("settings_runtime_writeback", "disk_write", "scale_chain"):
        if key in payload:
            compact[key] = payload.get(key)
    compact["result_compacted"] = True
    compact["result_compacted_at"] = now_utc()
    return compact
