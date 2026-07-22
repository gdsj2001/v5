from __future__ import annotations

import json
import math
import re
from pathlib import Path
from typing import Any, Dict, List, Tuple

import v5_drive_bus_context as context
import v5_drive_bus_contract as contract
from v5_drive_bus_contract import (
    DriveActionError,
    MAX_SETTINGS_RUNTIME_DEPTH,
    MAX_SETTINGS_RUNTIME_NODES,
    SETTINGS_RUNTIME_FORBIDDEN_KEYS,
    SETTINGS_RUNTIME_FORBIDDEN_PREFIXES,
    SETTINGS_RUNTIME_SCHEMA,
    SETTINGS_RUNTIME_WRITE_DROP_KEYS,
    axis_unit,
    finite_float,
    now_utc,
)
from v5_drive_result import write_json


LEGACY_AXIS_RATIO_KEYS = (
    "motor_rev",
    "load_rev",
    "motor_revs_per_load_rev",
    "reducer_ratio",
)


def drop_legacy_axis_ratio_copies(payload: Dict[str, Any]) -> None:
    axes = payload.get("axes") if isinstance(payload.get("axes"), list) else []
    for axis_cfg in axes:
        if not isinstance(axis_cfg, dict):
            continue
        for key in LEGACY_AXIS_RATIO_KEYS:
            axis_cfg.pop(key, None)
        zero_model = axis_cfg.get("zero_model")
        scale_evidence = zero_model.get("scale_evidence") if isinstance(
            zero_model, dict) else None
        if isinstance(scale_evidence, dict):
            for key in LEGACY_AXIS_RATIO_KEYS:
                scale_evidence.pop(key, None)
        for evidence_key in ("electronic_gear", "drive_set_evidence"):
            evidence = axis_cfg.get(evidence_key)
            if not isinstance(evidence, dict):
                continue
            identity = evidence.get("drive_transaction_identity")
            if not isinstance(identity, dict) or not identity.get(
                    "transaction_generation"):
                for key in LEGACY_AXIS_RATIO_KEYS:
                    evidence.pop(key, None)

def settings_runtime_key(raw_key: Any) -> str:
    return str(raw_key or "").strip().lower()


def settings_runtime_forbidden_reason(key: str) -> str:
    if key in SETTINGS_RUNTIME_FORBIDDEN_KEYS:
        return key
    for prefix in SETTINGS_RUNTIME_FORBIDDEN_PREFIXES:
        if key.startswith(prefix):
            return prefix.rstrip("_")
    if key.endswith("_status_frame") or key.endswith("_runtime_ini"):
        return key
    return ""


def _settings_runtime_schema_error(code: str, message_cn: str, path: str, detail: Any = None) -> DriveActionError:
    payload = {"path": path}
    if detail is not None:
        payload["detail"] = detail
    return DriveActionError(code, message_cn, payload)


def _validate_settings_runtime_node(value: Any, path: str, depth: int, node_count: List[int]) -> None:
    if depth > MAX_SETTINGS_RUNTIME_DEPTH:
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_TOO_DEEP", "settings_runtime drive-only schema 嵌套过深，已 fail-closed。", path)
    node_count[0] += 1
    if node_count[0] > MAX_SETTINGS_RUNTIME_NODES:
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_TOO_LARGE", "settings_runtime drive-only schema 节点数超出预算，已 fail-closed。", path, {"max_nodes": MAX_SETTINGS_RUNTIME_NODES})
    if isinstance(value, dict):
        for raw_key, item in value.items():
            if not isinstance(raw_key, str) or not raw_key.strip():
                raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_BAD_KEY", "settings_runtime drive-only schema 含非法 key，已 fail-closed。", path, raw_key)
            key = settings_runtime_key(raw_key)
            reason = settings_runtime_forbidden_reason(key)
            if reason:
                raise _settings_runtime_schema_error(
                    "SETTINGS_RUNTIME_SCHEMA_FORBIDDEN_FIELD",
                    "settings_runtime drive-only schema 含运行态/坐标/模态/安全污染字段，已 fail-closed。",
                    "%s.%s" % (path, raw_key),
                    {"field": raw_key, "reason": reason},
                )
            _validate_settings_runtime_node(item, "%s.%s" % (path, raw_key), depth + 1, node_count)
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            _validate_settings_runtime_node(item, "%s[%d]" % (path, index), depth + 1, node_count)
        return
    if isinstance(value, float) and not math.isfinite(value):
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_NON_FINITE", "settings_runtime drive-only schema 含 NaN/Inf，已 fail-closed。", path, value)


def validate_settings_runtime_drive_only(payload: Any) -> None:
    if not isinstance(payload, dict):
        raise DriveActionError("SETTINGS_RUNTIME_SCHEMA_NOT_OBJECT", "settings_runtime drive-only schema 顶层不是对象，已 fail-closed。", type(payload).__name__)
    schema = payload.get("schema")
    if schema is not None and schema != SETTINGS_RUNTIME_SCHEMA:
        raise DriveActionError("SETTINGS_RUNTIME_SCHEMA_MISMATCH", "settings_runtime schema 不是 drive-only v1，已 fail-closed。", {"schema": schema, "expected": SETTINGS_RUNTIME_SCHEMA})
    axes = payload.get("axes")
    if not isinstance(axes, list):
        raise DriveActionError("SETTINGS_RUNTIME_SCHEMA_AXES_MISSING", "settings_runtime drive-only schema 缺少 axes 列表，已 fail-closed。", payload)
    _validate_settings_runtime_node(payload, "$", 0, [0])


def _sanitize_settings_runtime_node(value: Any, path: str, depth: int, node_count: List[int]) -> Any:
    if depth > MAX_SETTINGS_RUNTIME_DEPTH:
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_TOO_DEEP", "settings_runtime drive-only schema 嵌套过深，已 fail-closed。", path)
    node_count[0] += 1
    if node_count[0] > MAX_SETTINGS_RUNTIME_NODES:
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_TOO_LARGE", "settings_runtime drive-only schema 节点数超出预算，已 fail-closed。", path, {"max_nodes": MAX_SETTINGS_RUNTIME_NODES})
    if isinstance(value, dict):
        clean: Dict[str, Any] = {}
        for raw_key, item in value.items():
            if not isinstance(raw_key, str) or not raw_key.strip():
                raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_BAD_KEY", "settings_runtime drive-only schema 含非法 key，已 fail-closed。", path, raw_key)
            key = settings_runtime_key(raw_key)
            if key in SETTINGS_RUNTIME_WRITE_DROP_KEYS:
                continue
            reason = settings_runtime_forbidden_reason(key)
            if reason:
                raise _settings_runtime_schema_error(
                    "SETTINGS_RUNTIME_SCHEMA_FORBIDDEN_FIELD",
                    "settings_runtime drive-only schema 含运行态/坐标/模态/安全污染字段，已 fail-closed。",
                    "%s.%s" % (path, raw_key),
                    {"field": raw_key, "reason": reason},
                )
            clean[raw_key] = _sanitize_settings_runtime_node(item, "%s.%s" % (path, raw_key), depth + 1, node_count)
        return clean
    if isinstance(value, list):
        return [_sanitize_settings_runtime_node(item, "%s[%d]" % (path, index), depth + 1, node_count) for index, item in enumerate(value)]
    if isinstance(value, float) and not math.isfinite(value):
        raise _settings_runtime_schema_error("SETTINGS_RUNTIME_SCHEMA_NON_FINITE", "settings_runtime drive-only schema 含 NaN/Inf，已 fail-closed。", path, value)
    return value


def sanitize_settings_runtime_drive_only(payload: Dict[str, Any]) -> Dict[str, Any]:
    clean = _sanitize_settings_runtime_node(payload, "$", 0, [0])
    if not isinstance(clean, dict):
        raise DriveActionError("SETTINGS_RUNTIME_SCHEMA_NOT_OBJECT", "settings_runtime drive-only schema 顶层不是对象，已 fail-closed。", type(clean).__name__)
    clean["schema"] = SETTINGS_RUNTIME_SCHEMA
    drop_legacy_axis_ratio_copies(clean)
    validate_settings_runtime_drive_only(clean)
    return clean


def drive_only_scale_evidence(scale_evidence: Dict[str, Any]) -> Dict[str, Any]:
    allowed = (
        "unit",
        "counts_per_unit",
        "source",
        "pitch_mm_per_rev",
        "inferred_pitch_mm_per_rev",
        "encoder_bits",
        "bit_counts_per_motor_rev",
        "egear_numerator",
        "egear_denominator",
        "bit_egear_counts_per_motor_rev",
        "actual_counts_per_motor_rev",
        "chain_counts_per_unit",
        "scale_chain_delta",
        "scale_chain_relative_delta",
    )
    return {key: scale_evidence.get(key) for key in allowed if key in scale_evidence and scale_evidence.get(key) is not None}


def read_runtime_ini_sections(path: Path) -> Dict[str, Dict[str, str]]:
    cache_key = str(path)
    if cache_key in context.runtime_ini_sections_cache:
        return context.runtime_ini_sections_cache[cache_key]
    if not context.resident_preload_active:
        raise DriveActionError("RUNTIME_INI_RESIDENT_NOT_PRELOADED", "active runtime INI 未在启动阶段载入内存，动作热路径拒绝读盘。", str(path))
    sections: Dict[str, Dict[str, str]] = {}
    if not path.is_file():
        context.runtime_ini_sections_cache[cache_key] = sections
        return sections
    current = ""
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].strip().upper()
            sections.setdefault(current, {})
            continue
        if "=" in line and current:
            key, value = line.split("=", 1)
            sections[current][key.strip().upper()] = value.strip()
    context.runtime_ini_sections_cache[cache_key] = sections
    return sections


def runtime_ini_value(sections: Dict[str, Dict[str, str]], names: List[str], keys: List[str]) -> float | None:
    for name in names:
        table = sections.get(str(name).upper(), {})
        for key in keys:
            value = finite_float(table.get(str(key).upper()))
            if value is not None:
                return value
    return None


def runtime_ini_joint_index(
        sections: Dict[str, Dict[str, str]], axis: str, configured_axis_index: int) -> Tuple[int, str]:
    axis_name = str(axis or "").strip().upper()
    raw_coordinates = str(sections.get("TRAJ", {}).get("COORDINATES") or "")
    coordinates = re.findall(r"[XYZABCUVW]", raw_coordinates.upper())
    matches = [index for index, letter in enumerate(coordinates) if letter == axis_name]
    if not coordinates or len(matches) != 1:
        raise DriveActionError(
            "RUNTIME_INI_JOINT_MAPPING_INVALID",
            "active runtime INI 的 TRAJ/COORDINATES 无法唯一映射当前轴，拒绝使用设置表行号代替 joint index。",
            {
                "axis": axis_name,
                "coordinates": raw_coordinates,
                "matches": matches,
                "configured_axis_index": configured_axis_index,
            },
        )
    return matches[0], "active_runtime_ini.TRAJ.COORDINATES"


def _read_settings_runtime_owner() -> Dict[str, Any]:
    if not contract.SETTINGS_RUNTIME_JSON.is_file():
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_MISSING", "settings_runtime resident owner 未加载，不能校验设0。", str(contract.SETTINGS_RUNTIME_JSON))
    try:
        payload = json.loads(contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_INVALID", "settings_runtime resident owner 损坏，不能校验设0。", "%s: %s" % (type(exc).__name__, exc))
    validate_settings_runtime_drive_only(payload)
    drop_legacy_axis_ratio_copies(payload)
    axes = payload.get("axes") if isinstance(payload, dict) else None
    if not isinstance(axes, list):
        raise DriveActionError("SETTINGS_AXIS_ZERO_AXES_MISSING", "settings_runtime resident owner 缺少 axes，不能校验设0。", payload)
    return payload


def load_settings_runtime() -> Dict[str, Any]:
    if context.settings_runtime_cache is not None:
        return context.settings_runtime_cache
    if not context.resident_preload_active:
        raise DriveActionError("SETTINGS_RUNTIME_RESIDENT_NOT_PRELOADED", "settings_runtime drive-only resident owner 未在启动阶段载入内存，动作热路径拒绝读盘。", str(contract.SETTINGS_RUNTIME_JSON))
    payload = _read_settings_runtime_owner()
    context.settings_runtime_cache = payload
    return payload


def find_runtime_axis(runtime: Dict[str, Any], axis: str) -> Tuple[Dict[str, Any], int]:
    target = str(axis or "").upper()
    for index, item in enumerate(runtime.get("axes", [])):
        if isinstance(item, dict) and str(item.get("axis") or "").upper() == target:
            return item, index
    raise DriveActionError("SETTINGS_AXIS_ZERO_AXIS_MISSING", "settings_runtime resident owner 没有该轴零位记录。", {"axis": target})


def saved_zero_counts(axis_cfg: Dict[str, Any]) -> float:
    zero_model = axis_cfg.get("zero_model") if isinstance(axis_cfg.get("zero_model"), dict) else {}
    candidates = [
        zero_model.get("zero_anchor_counts"),
        zero_model.get("actual_counts"),
        zero_model.get("zero_counts"),
    ]
    drive_position = zero_model.get("drive_position") if isinstance(zero_model.get("drive_position"), dict) else {}
    candidates.append(drive_position.get("actual_position_counts"))
    for value in candidates:
        number = finite_float(value)
        if number is not None:
            return number
    raise DriveActionError("SETTINGS_AXIS_ZERO_DISK_ZERO_MISSING", "resident 零位 zero_model 缺少可比较的 count-domain 零位。", zero_model)


def write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def snapshot_axis_zero_persistence() -> Dict[str, str]:
    if not contract.SETTINGS_RUNTIME_JSON.is_file() or not contract.RUNTIME_SETTINGS_INI.is_file():
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_PERSISTENCE_SNAPSHOT_MISSING",
            "设0前持久owner不完整，不能建立可回滚快照。",
            {"settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
             "runtime_ini": str(contract.RUNTIME_SETTINGS_INI)},
        )
    return {
        "settings_runtime_json": contract.SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"),
        "runtime_ini": contract.RUNTIME_SETTINGS_INI.read_text(encoding="utf-8", errors="ignore"),
    }


def restore_axis_zero_persistence(snapshot: Dict[str, str]) -> Dict[str, Any]:
    settings_text = snapshot.get("settings_runtime_json") if isinstance(snapshot, dict) else None
    ini_text = snapshot.get("runtime_ini") if isinstance(snapshot, dict) else None
    if not isinstance(settings_text, str) or not isinstance(ini_text, str):
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_ROLLBACK_SNAPSHOT_INVALID",
            "设0回滚快照无效，保持Machine Off。", snapshot)
    payload = json.loads(settings_text)
    validate_settings_runtime_drive_only(payload)
    write_text_atomic(contract.RUNTIME_SETTINGS_INI, ini_text)
    write_text_atomic(contract.SETTINGS_RUNTIME_JSON, settings_text)
    context.runtime_ini_sections_cache.pop(str(contract.RUNTIME_SETTINGS_INI), None)
    context.settings_runtime_cache = payload
    return {
        "ok": True,
        "code": "SETTINGS_AXIS_ZERO_PERSISTENCE_ROLLED_BACK",
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
        "runtime_ini": str(contract.RUNTIME_SETTINGS_INI),
    }


def update_runtime_ini_raw_limits(axis: str, axis_index: int,
                                  old_zero_physical: float,
                                  new_zero_physical: float,
                                  counts_per_unit: float) -> Dict[str, Any]:
    if not contract.RUNTIME_SETTINGS_INI.is_file():
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_INI_MISSING", "runtime INI 不存在，不能同次写入 raw limit。", str(contract.RUNTIME_SETTINGS_INI))
    original = contract.RUNTIME_SETTINGS_INI.read_text(encoding="utf-8", errors="ignore")
    lines = original.splitlines()
    section = ""
    axis_section = "AXIS_%s" % str(axis or "").upper()
    sections = read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
    joint_index, joint_index_source = runtime_ini_joint_index(sections, axis, axis_index)
    joint_section = "JOINT_%d" % joint_index
    rotary_axis = str(axis or "").upper() in {"A", "B", "C"}
    wcheckpoint_counts_per_rev = None
    if rotary_axis:
        counts_per_rev = finite_float(counts_per_unit)
        if counts_per_rev is None or counts_per_rev <= 0.0:
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_ROTARY_SCALE_INVALID",
                "Rotary active SCALE is invalid; wcheckpoint Crev was not written.",
                {"axis": axis, "counts_per_unit": counts_per_unit})
        counts_per_rev *= 360.0
        rounded_counts_per_rev = int(round(counts_per_rev))
        if (rounded_counts_per_rev <= 0 or
                abs(counts_per_rev - rounded_counts_per_rev) > 1.0e-6):
            raise DriveActionError(
                "SETTINGS_AXIS_ZERO_ROTARY_CREV_NON_INTEGER",
                "Rotary SCALE does not produce an integer wcheckpoint Crev.",
                {"axis": axis, "counts_per_unit": counts_per_unit,
                 "counts_per_rev": counts_per_rev})
        wcheckpoint_counts_per_rev = rounded_counts_per_rev
    values: Dict[str, Dict[str, float]] = {axis_section: {}, joint_section: {}}
    for raw in lines:
        stripped = raw.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].strip().upper()
            continue
        if section in values and "=" in raw:
            key, value = raw.split("=", 1)
            key_u = key.strip().upper()
            if key_u in {"MIN_LIMIT", "MAX_LIMIT"}:
                number = finite_float(value.strip())
                if number is not None:
                    values[section][key_u] = number
    limit_source = values.get(joint_section) if values.get(joint_section, {}).get("MIN_LIMIT") is not None else values.get(axis_section)
    if not limit_source or "MIN_LIMIT" not in limit_source or "MAX_LIMIT" not in limit_source:
        raise DriveActionError("SETTINGS_AXIS_ZERO_RAW_LIMIT_MISSING", "runtime INI 缺少当前 raw limit，不能按新零位重算。", {"axis_section": axis_section, "joint_section": joint_section})
    raw_min_current = limit_source["MIN_LIMIT"]
    raw_max_current = limit_source["MAX_LIMIT"]
    min_limit_disabled = raw_min_current == 0.0
    max_limit_disabled = raw_max_current == 0.0
    if ((not min_limit_disabled and old_zero_physical < raw_min_current) or
            (not max_limit_disabled and old_zero_physical > raw_max_current)):
        raise DriveActionError("SETTINGS_AXIS_ZERO_OLD_ZERO_OUTSIDE_RAW_LIMIT", "旧零位证据落在当前 raw 限位区间外，不能继续滚动重算限位。", {"old_zero_physical": old_zero_physical, "raw_min_limit": raw_min_current, "raw_max_limit": raw_max_current, "runtime_ini": str(contract.RUNTIME_SETTINGS_INI)})
    min_distance = 0.0 if min_limit_disabled else raw_min_current - old_zero_physical
    max_distance = 0.0 if max_limit_disabled else raw_max_current - old_zero_physical
    new_min = 0.0 if min_limit_disabled else new_zero_physical + min_distance
    new_max = 0.0 if max_limit_disabled else new_zero_physical + max_distance
    if not (math.isfinite(new_min) and math.isfinite(new_max) and
            (min_limit_disabled or max_limit_disabled or new_min < new_max)):
        raise DriveActionError("SETTINGS_AXIS_ZERO_RAW_LIMIT_INVALID", "按新零位重算 raw limit 后区间非法，未写入。", {"new_min": new_min, "new_max": new_max})
    out = []
    section = ""
    touched: Dict[str, Dict[str, bool]] = {axis_section: {"MIN_LIMIT": False, "MAX_LIMIT": False}, joint_section: {"MIN_LIMIT": False, "MAX_LIMIT": False}}
    wcheckpoint_write_count = 0
    for raw in lines:
        stripped = raw.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].strip().upper()
            out.append(raw)
            continue
        if section in touched and "=" in raw:
            key, _value = raw.split("=", 1)
            key_u = key.strip().upper()
            if key_u == "MIN_LIMIT":
                out.append("%s = %.10g" % (key.strip(), new_min))
                touched[section]["MIN_LIMIT"] = True
                continue
            if key_u == "MAX_LIMIT":
                out.append("%s = %.10g" % (key.strip(), new_max))
                touched[section]["MAX_LIMIT"] = True
                continue
            if (section == axis_section and rotary_axis and
                    key_u == "WCHECKPOINT_COUNTS_PER_REV"):
                out.append("%s = %d" % (key.strip(), wcheckpoint_counts_per_rev))
                wcheckpoint_write_count += 1
                continue
        out.append(raw)
    if not any(v["MIN_LIMIT"] and v["MAX_LIMIT"] for v in touched.values()):
        raise DriveActionError("SETTINGS_AXIS_ZERO_RAW_LIMIT_WRITE_MISSING", "runtime INI raw limit 未找到可写字段。", touched)
    if rotary_axis and wcheckpoint_write_count != 1:
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_WCHECKPOINT_CREV_WRITE_MISSING",
            "Rotary runtime INI requires one WCHECKPOINT_COUNTS_PER_REV owner.",
            {"axis_section": axis_section,
             "write_count": wcheckpoint_write_count})
    write_text_atomic(contract.RUNTIME_SETTINGS_INI, "\n".join(out) + "\n")
    context.runtime_ini_sections_cache.pop(str(contract.RUNTIME_SETTINGS_INI), None)
    old_preload = context.resident_preload_active
    context.resident_preload_active = True
    try:
        read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
    finally:
        context.resident_preload_active = old_preload
    return {
        "runtime_ini": str(contract.RUNTIME_SETTINGS_INI),
        "axis_section": axis_section,
        "joint_section": joint_section,
        "joint_index": joint_index,
        "joint_index_source": joint_index_source,
        "configured_axis_index": axis_index,
        "old_zero_physical": old_zero_physical,
        "new_zero_physical": new_zero_physical,
        "min_limit_disabled": min_limit_disabled,
        "max_limit_disabled": max_limit_disabled,
        "ui_min_limit_distance": min_distance,
        "ui_max_limit_distance": max_distance,
        "raw_min_limit": new_min,
        "raw_max_limit": new_max,
        "wcheckpoint_counts_per_rev": wcheckpoint_counts_per_rev,
        "wcheckpoint_profile_updated": bool(rotary_axis),
        "updated_sections": [name for name, item in touched.items() if item["MIN_LIMIT"] and item["MAX_LIMIT"]],
    }


def persist_axis_zero_model(runtime: Dict[str, Any],
                            axis: str,
                            axis_index: int,
                            current_counts: float,
                            counts_per_unit: float,
                            scale_evidence: Dict[str, Any],
                            read_evidence: Dict[str, Any],
                            zero_run_id: str = "") -> Dict[str, Any]:
    try:
        slave_position = int(str(read_evidence.get("position") or "").strip())
    except (TypeError, ValueError):
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_SLAVE_POSITION_MISSING",
            "设0证据缺少本次轴参数映射对应的从站位置，拒绝保存零点。",
            {"axis": axis, "position": read_evidence.get("position")},
        )
    if slave_position < 0:
        raise DriveActionError(
            "SETTINGS_AXIS_ZERO_SLAVE_POSITION_INVALID",
            "设0证据中的从站位置非法，拒绝保存零点。",
            {"axis": axis, "position": slave_position},
        )
    if not context.resident_preload_active:
        raise DriveActionError("SETTINGS_RUNTIME_RESIDENT_NOT_PRELOADED", "settings_runtime drive-only resident owner 未在启动阶段载入内存，设0保存拒绝写盘。", str(contract.SETTINGS_RUNTIME_JSON))
    owner_runtime = _read_settings_runtime_owner()
    axis_cfg, _ = find_runtime_axis(owner_runtime, axis)
    prior_zero_model = axis_cfg.get("zero_model")
    zero_model = prior_zero_model if isinstance(prior_zero_model, dict) else {}
    if prior_zero_model is None:
        old_counts = 0.0
        old_zero_physical = 0.0
        old_zero_source = "runtime_ini_initial_origin"
    else:
        old_counts = saved_zero_counts(axis_cfg)
        old_zero_physical = finite_float(zero_model.get("raw_zero_position"))
        if old_zero_physical is None:
            old_zero_physical = old_counts / counts_per_unit
        old_zero_source = "existing_zero_model"
    new_zero_physical = current_counts / counts_per_unit
    raw_limit_save = update_runtime_ini_raw_limits(
        axis, axis_index, old_zero_physical, new_zero_physical,
        counts_per_unit)
    new_zero_model = dict(zero_model)
    drive_position = dict(new_zero_model.get("drive_position")) if isinstance(new_zero_model.get("drive_position"), dict) else {}
    drive_position.update({
        "axis": axis,
        "ok": True,
        "code": "SETTINGS_ZERO_DRIVE_ACTUAL_POSITION_READ",
        "actual_position_counts": current_counts,
        "audit_actual_position_counts": current_counts,
        "read_scope": "linear_actual_position_only" if axis_unit(axis) == "mm" else "absolute_count_domain_position",
        "readback": read_evidence.get("read", {}),
        "profile_id": read_evidence.get("profile_id", ""),
    })
    new_zero_model.update({
        "source": "settings_axis_zero",
        "apply_route": "drive_actual_position_read_then_disk_zero_and_raw_limit_save",
        "apply_state": "count_domain_zero_saved_restart_required",
        "captured_at": now_utc(),
        "position_count_source": "drive.read_actual_position",
        "actual_counts": current_counts,
        "zero_counts": current_counts,
        "zero_anchor_counts": current_counts,
        "raw_zero_position": new_zero_physical,
        "raw_zero_formula": "zero_counts / active_runtime_ini.SCALE",
        "counts_per_unit": counts_per_unit,
        "slave_position": slave_position,
        "unit": axis_unit(axis),
        "scale_chain": drive_only_scale_evidence(scale_evidence),
        "drive_position": drive_position,
    })
    if zero_run_id:
        new_zero_model["zero_run_id"] = zero_run_id
    new_zero_model.pop("zero_generation", None)
    superseded_zero_axes: List[str] = []
    owner_axes = owner_runtime.get("axes")
    if isinstance(owner_axes, list):
        for candidate in owner_axes:
            if candidate is axis_cfg or not isinstance(candidate, dict):
                continue
            candidate_zero = candidate.get("zero_model")
            if not isinstance(candidate_zero, dict):
                continue
            candidate_slave = finite_float(candidate_zero.get("slave_position"))
            if (candidate_slave is None or candidate_slave < 0.0 or
                    candidate_slave != float(int(candidate_slave)) or
                    int(candidate_slave) != slave_position):
                continue
            candidate_axis = str(candidate.get("axis") or "").strip().upper()
            candidate.pop("zero_model", None)
            candidate.pop("zero_model_writeback", None)
            if candidate_axis:
                superseded_zero_axes.append(candidate_axis)
    axis_cfg["zero_model"] = new_zero_model
    axis_cfg["zero_model_writeback"] = {
        "ok": True,
        "code": "SETTINGS_AXIS_ZERO_MODEL_SAVED",
        "written_at": now_utc(),
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
    }
    runtime_for_write = sanitize_settings_runtime_drive_only(owner_runtime)
    write_json(contract.SETTINGS_RUNTIME_JSON, runtime_for_write)
    runtime.clear()
    runtime.update(runtime_for_write)
    context.settings_runtime_cache = runtime_for_write
    reread_axis, _ = find_runtime_axis(runtime_for_write, axis)
    reread_counts = saved_zero_counts(reread_axis)
    if abs(reread_counts - current_counts) > 0.5:
        raise DriveActionError("SETTINGS_AXIS_ZERO_OWNER_READBACK_MISMATCH", "resident owner 零位写入后内存回读不一致。", {"written_counts": current_counts, "readback_counts": reread_counts})
    return {
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
        "written_counts": current_counts,
        "readback_counts": reread_counts,
        "old_zero_counts": old_counts,
        "old_zero_physical": old_zero_physical,
        "old_zero_source": old_zero_source,
        "new_zero_physical": new_zero_physical,
        "slave_position": slave_position,
        "superseded_zero_axes": superseded_zero_axes,
        "raw_limit_save": raw_limit_save,
    }


def persist_settings_runtime(runtime: Dict[str, Any]) -> Dict[str, Any]:
    runtime_for_write = sanitize_settings_runtime_drive_only(runtime)
    write_json(contract.SETTINGS_RUNTIME_JSON, runtime_for_write)
    runtime.clear()
    runtime.update(runtime_for_write)
    context.settings_runtime_cache = runtime_for_write
    reread = load_settings_runtime()
    return {
        "ok": True,
        "code": "SETTINGS_RUNTIME_WRITEBACK_OK",
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
        "schema": reread.get("schema"),
        "axis_count": len(reread.get("axes", [])) if isinstance(reread.get("axes"), list) else 0,
        "written_at": now_utc(),
    }
