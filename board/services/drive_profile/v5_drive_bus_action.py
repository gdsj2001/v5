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


from v5_drive_bus_reset_action import (
    _run_factory_reset_impl,
    run_factory_reset,
    run_fault_reset,
)




from v5_drive_bus_apply_action import (
    _run_boot_drive_apply_impl,
    _run_set_drive_preflight_impl,
    run_boot_drive_apply,
    run_set_drive,
)

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
