from __future__ import annotations

import re
from typing import Any, Dict, Tuple

import v5_drive_bus_contract as contract
from v5_drive_bus_contract import DriveActionError, axis_unit, finite_float
from v5_drive_runtime_store import (
    find_runtime_axis,
    load_settings_runtime,
    persist_axis_zero_model,
    read_runtime_ini_sections,
    restore_axis_zero_persistence,
    saved_zero_counts,
    snapshot_axis_zero_persistence,
)
from v5_drive_sdo import (
    parse_slave_identity,
    read_command,
    read_resident_snapshot,
    select_profile,
)


def request_slave_position(request: Dict[str, Any]) -> str:
    raw = request.get("slave_index")
    if raw is None or str(raw).strip() == "":
        raw = request.get("slave")
    text = str(raw or "").strip()
    if not text:
        raise DriveActionError("SETTINGS_AXIS_ZERO_SLAVE_REQUIRED", "设0请求缺少当前行从站列选择，未读取驱动。", request)
    text = re.split(r"[\s:;,|]+", text, 1)[0].strip()
    if not re.fullmatch(r"\d+", text):
        raise DriveActionError("SETTINGS_AXIS_ZERO_SLAVE_INVALID", "设0请求的从站号非法，未读取驱动。", {"slave_index": raw})
    return str(int(text))


def current_axis_counts(axis_cfg: Dict[str, Any], timeout_s: float, slave_position: str) -> Tuple[float, Dict[str, Any]]:
    position = slave_position
    if position is None or str(position).strip() == "":
        raise DriveActionError("SETTINGS_AXIS_ZERO_SLAVE_MISSING", "该轴缺少从站绑定，不能读取编码器位置。", axis_cfg)
    position_text = str(int(position)) if isinstance(position, (int, float)) else str(position).strip()
    snapshot = read_resident_snapshot()
    identity = parse_slave_identity(position_text, min(timeout_s, 3.0))
    if not identity.get("identity_ok"):
        raise DriveActionError("SETTINGS_AXIS_ZERO_SLAVE_IDENTITY_MISSING", "真实从站身份读取不完整，不能读取编码器位置。", identity)
    profile = select_profile(identity, snapshot)
    commands = profile.get("commands", {}) if isinstance(profile.get("commands"), dict) else {}
    item = read_command(position_text, "drive.read_actual_position", commands.get("drive.read_actual_position", {}), True)
    upload = item.get("upload") if isinstance(item.get("upload"), dict) else {}
    value = finite_float(upload.get("value"))
    if not item.get("ok") or value is None:
        raise DriveActionError("SETTINGS_AXIS_ZERO_ENCODER_READ_FAILED", "编码器/驱动当前位置读回失败，不能校验设0。", {"position": position_text, "read": item})
    evidence = {
        "position": position_text,
        "identity": identity,
        "profile_id": profile.get("profile_id", ""),
        "selected_map_sha256": profile.get("profile_map_sha256", ""),
        "map_source": profile.get("map_source", ""),
        "read": item,
    }
    return value, evidence


def axis_zero_verify(request: Dict[str, Any], timeout_s: float) -> Dict[str, Any]:
    axis = str(request.get("axis") or "").upper()
    driver_mode = str(request.get("driver_mode") or "").lower()
    target_scope = str(request.get("target_scope") or "")
    apply_mode = str(request.get("apply_mode") or "")
    tolerance = finite_float(request.get("tolerance_mm_deg")) or 0.1
    if not axis:
        raise DriveActionError("SETTINGS_AXIS_ZERO_AXIS_REQUIRED", "设0请求缺少轴号，未读取驱动。", request)
    if driver_mode not in {"bus", "ethercat"} or target_scope != "bus_count_domain_zero" or apply_mode != "count_domain_zero":
        raise DriveActionError("SETTINGS_AXIS_ZERO_SCOPE_UNSUPPORTED", "当前只支持 BUS/EtherCAT count-domain 设0校验；Pulse HOME_OFFSET 另走 native owner。", request)
    slave_position = request_slave_position(request)
    runtime = load_settings_runtime()
    axis_cfg, axis_index = find_runtime_axis(runtime, axis)
    unit = axis_unit(axis)
    from v5_drive_axis_model import derive_counts_per_unit

    counts_per_unit, scale_evidence = derive_counts_per_unit(axis, axis_cfg, axis_index)
    captured_counts, capture_read_evidence = current_axis_counts(axis_cfg, timeout_s, slave_position)
    zero_run_id = str(request.get("_run_id") or request.get("run_id") or "")
    if not zero_run_id:
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_RUN_ID_REQUIRED",
            "BUS 设零缺少本次 action run_id，拒绝保存不可关联的零点。",
            {"axis": axis})
    persistence_snapshot = snapshot_axis_zero_persistence()
    disk_write: Dict[str, Any] = {}
    verify_read_evidence: Dict[str, Any] = {}
    raw_limit_readback: Dict[str, Any] = {}
    persistence_touched = False
    try:
        persistence_touched = True
        disk_write = persist_axis_zero_model(
            runtime, axis, axis_index, captured_counts, counts_per_unit,
            scale_evidence, capture_read_evidence,
            zero_run_id=zero_run_id)
        axis_after, _ = find_runtime_axis(runtime, axis)
        saved_counts = saved_zero_counts(axis_after)
        current_counts, verify_read_evidence = current_axis_counts(axis_after, timeout_s, slave_position)
        current_physical = current_counts / counts_per_unit
        saved_physical = saved_counts / counts_per_unit
        delta = abs(current_physical - saved_physical)
        if delta > tolerance:
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_ENCODER_MISMATCH",
                "%s轴设0失败：当前编码器与本次硬盘零位差值超过 %.3f %s。" %
                (axis, tolerance, unit),
                {"current_encoder_physical": current_physical,
                 "saved_zero_physical": saved_physical,
                 "delta_physical": delta, "unit": unit},
            )
        raw_limit_save = disk_write.get("raw_limit_save")
        if not isinstance(raw_limit_save, dict):
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_RAW_LIMIT_EVIDENCE_MISSING",
                "%s轴设0未返回 raw 软限位写盘证据，正在回滚。" % axis,
                disk_write,
            )
        expected_min = finite_float(raw_limit_save.get("raw_min_limit"))
        expected_max = finite_float(raw_limit_save.get("raw_max_limit"))
        raw_zero = finite_float(raw_limit_save.get("new_zero_physical"))
        min_distance = finite_float(raw_limit_save.get("ui_min_limit_distance"))
        max_distance = finite_float(raw_limit_save.get("ui_max_limit_distance"))
        min_limit_disabled = raw_limit_save.get("min_limit_disabled")
        max_limit_disabled = raw_limit_save.get("max_limit_disabled")
        sections = raw_limit_save.get("updated_sections")
        if (expected_min is None or expected_max is None or raw_zero is None or
                min_distance is None or max_distance is None or
                not isinstance(min_limit_disabled, bool) or
                not isinstance(max_limit_disabled, bool) or
                not isinstance(sections, list) or not sections):
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_RAW_LIMIT_EVIDENCE_INVALID",
                "%s轴设0的 raw 零位/软限位证据不完整，正在回滚。" % axis,
                raw_limit_save,
            )
        formula_tolerance = max(
            1.0 / counts_per_unit,
            abs(expected_min) * 2.0e-9,
            abs(expected_max) * 2.0e-9,
            1.0e-9,
        )
        formula_min = 0.0 if min_limit_disabled else raw_zero + min_distance
        formula_max = 0.0 if max_limit_disabled else raw_zero + max_distance
        if (abs(expected_min - formula_min) > formula_tolerance or
                abs(expected_max - formula_max) > formula_tolerance):
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_RAW_LIMIT_FORMULA_MISMATCH",
                "%s轴设0的 raw 软限位不符合新零位加相对距离公式，正在回滚。" % axis,
                {"raw_zero": raw_zero,
                 "min_limit_disabled": min_limit_disabled,
                 "max_limit_disabled": max_limit_disabled,
                 "negative_limit_distance": min_distance,
                 "positive_limit_distance": max_distance,
                 "expected_raw_min": formula_min,
                 "expected_raw_max": formula_max,
                 "written_raw_min": expected_min,
                 "written_raw_max": expected_max,
                 "tolerance": formula_tolerance},
            )
        ini_sections = read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
        read_sections: Dict[str, Any] = {}
        for section_name in sections:
            section_key = str(section_name or "").upper()
            table = ini_sections.get(section_key, {})
            actual_min = finite_float(table.get("MIN_LIMIT"))
            actual_max = finite_float(table.get("MAX_LIMIT"))
            read_sections[section_key] = {
                "raw_min": actual_min,
                "raw_max": actual_max,
            }
            if (actual_min is None or actual_max is None or
                    abs(actual_min - expected_min) > formula_tolerance or
                    abs(actual_max - expected_max) > formula_tolerance):
                raise DriveActionError(
                    "SETTINGS_AXIS_ZERO_RAW_LIMIT_READBACK_MISMATCH",
                    "%s轴设0后的 raw 软限位硬盘回读不一致，正在回滚。" % axis,
                    {"section": section_key,
                     "expected_raw_min": expected_min,
                     "expected_raw_max": expected_max,
                     "actual_raw_min": actual_min,
                     "actual_raw_max": actual_max,
                     "tolerance": formula_tolerance},
                )
        raw_limit_readback = {
            "ok": True,
            "code": "SETTINGS_AXIS_ZERO_RAW_LIMIT_DISK_READBACK_OK",
            "runtime_ini": str(contract.RUNTIME_SETTINGS_INI),
            "raw_zero": raw_zero,
            "min_limit_disabled": min_limit_disabled,
            "max_limit_disabled": max_limit_disabled,
            "negative_limit_distance": min_distance,
            "positive_limit_distance": max_distance,
            "raw_min": expected_min,
            "raw_max": expected_max,
            "formula": {
                "raw_min": "0 when disabled else raw_zero + negative_limit_distance",
                "raw_max": "0 when disabled else raw_zero + positive_limit_distance",
            },
            "sections": read_sections,
            "tolerance": formula_tolerance,
        }
    except Exception as exc:
        rollback: Dict[str, Any] = {}
        rollback_error = ""
        if persistence_touched:
            try:
                rollback = restore_axis_zero_persistence(persistence_snapshot)
                runtime.clear()
                runtime.update(load_settings_runtime())
            except Exception as rollback_exc:
                rollback_error = "%s: %s" % (type(rollback_exc).__name__, rollback_exc)
        code = exc.code if isinstance(exc, DriveActionError) else "SETTINGS_AXIS_ZERO_PERSISTENCE_TRANSACTION_FAILED"
        message_cn = exc.message_cn if isinstance(exc, DriveActionError) else "设0持久化事务失败，已尝试恢复动作前零点和软限位。"
        detail = exc.detail if isinstance(exc, DriveActionError) else "%s: %s" % (type(exc).__name__, exc)
        if rollback_error:
            code = "SETTINGS_AXIS_ZERO_PERSISTENCE_ROLLBACK_FAILED"
            message_cn = "%s轴设0失败且持久文件回滚未完成，禁止继续保存并必须彻底重启。" % axis
        raise DriveActionError(code, message_cn, {
            "cause": detail,
            "persistence_rollback": rollback,
            "rollback_error": rollback_error,
        })
    settings_mcs_position = (captured_counts - saved_counts) / counts_per_unit
    settings_zero_display_verified = abs(settings_mcs_position) <= max(1.0 / counts_per_unit, 1.0e-9)
    message = "%s轴零位与 raw 软限位已保存；设置页机械坐标已按新零位重算为 %.6f %s，全局将在保存并重启后生效。" % (
        axis, settings_mcs_position, unit)
    return {
        "ok": True,
        "code": "SETTINGS_COUNT_DOMAIN_ZERO_SAVED_RESTART_REQUIRED",
        "message_cn": message,
        "display_message_cn": message,
        "axis": axis,
        "driver_mode": driver_mode,
        "target_scope": target_scope,
        "apply_mode": apply_mode,
        "slave_index": slave_position,
        "unit": unit,
        "tolerance_mm_deg": tolerance,
        "captured_encoder_counts": captured_counts,
        "current_encoder_counts": current_counts,
        "saved_zero_counts": saved_counts,
        "counts_per_unit": counts_per_unit,
        "current_encoder_physical": current_physical,
        "saved_zero_physical": saved_physical,
        "delta_physical": delta,
        "settings_mcs_position": settings_mcs_position,
        "settings_mcs_position_valid": True,
        "settings_zero_display_verified": settings_zero_display_verified,
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
        "zero_model_source": (axis_after.get("zero_model") or {}).get("source", "") if isinstance(axis_after.get("zero_model"), dict) else "",
        "zero_run_id": zero_run_id,
        "scale_chain": scale_evidence,
        "disk_write": disk_write,
        "capture_encoder_read": capture_read_evidence,
        "encoder_read": verify_read_evidence,
        "raw_limit_readback": raw_limit_readback,
        "write_executed": True,
        "drive_write_executed": False,
        "motion_executed": False,
        "backend_restart_required": True,
        "restart_deferred": True,
        "raw_limit_disk_saved": True,
        "raw_limit_live_verified": False,
        "raw_runtime_zero_verified": False,
        "memory_zero_verified": False,
        "zero_display_verified": False,
        "pending_runtime_proof": True,
        "canonical_clean_restart_required": True,
        "commit_seq": 0,
    }
