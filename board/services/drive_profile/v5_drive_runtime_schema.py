from __future__ import annotations

import math
from typing import Any, Dict, List

from v5_drive_bus_contract import (
    DriveActionError,
    MAX_SETTINGS_RUNTIME_DEPTH,
    MAX_SETTINGS_RUNTIME_NODES,
    SETTINGS_RUNTIME_FORBIDDEN_KEYS,
    SETTINGS_RUNTIME_FORBIDDEN_PREFIXES,
    SETTINGS_RUNTIME_SCHEMA,
    SETTINGS_RUNTIME_WRITE_DROP_KEYS,
)


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


def _settings_runtime_schema_error(
    code: str, message_cn: str, path: str, detail: Any = None,
) -> DriveActionError:
    payload = {"path": path}
    if detail is not None:
        payload["detail"] = detail
    return DriveActionError(code, message_cn, payload)


def _validate_settings_runtime_node(
    value: Any, path: str, depth: int, node_count: List[int],
) -> None:
    if depth > MAX_SETTINGS_RUNTIME_DEPTH:
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_TOO_DEEP",
            "settings_runtime drive-only schema 嵌套过深，已 fail-closed。", path)
    node_count[0] += 1
    if node_count[0] > MAX_SETTINGS_RUNTIME_NODES:
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_TOO_LARGE",
            "settings_runtime drive-only schema 节点数超出预算，已 fail-closed。",
            path, {"max_nodes": MAX_SETTINGS_RUNTIME_NODES})
    if isinstance(value, dict):
        for raw_key, item in value.items():
            if not isinstance(raw_key, str) or not raw_key.strip():
                raise _settings_runtime_schema_error(
                    "SETTINGS_RUNTIME_SCHEMA_BAD_KEY",
                    "settings_runtime drive-only schema 含非法 key，已 fail-closed。",
                    path, raw_key)
            key = settings_runtime_key(raw_key)
            reason = settings_runtime_forbidden_reason(key)
            if reason:
                raise _settings_runtime_schema_error(
                    "SETTINGS_RUNTIME_SCHEMA_FORBIDDEN_FIELD",
                    "settings_runtime drive-only schema 含运行态/坐标/模态/安全污染字段，已 fail-closed。",
                    "%s.%s" % (path, raw_key),
                    {"field": raw_key, "reason": reason},
                )
            _validate_settings_runtime_node(
                item, "%s.%s" % (path, raw_key), depth + 1, node_count)
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            _validate_settings_runtime_node(
                item, "%s[%d]" % (path, index), depth + 1, node_count)
        return
    if isinstance(value, float) and not math.isfinite(value):
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_NON_FINITE",
            "settings_runtime drive-only schema 含 NaN/Inf，已 fail-closed。",
            path, value)


def validate_settings_runtime_drive_only(payload: Any) -> None:
    if not isinstance(payload, dict):
        raise DriveActionError(
            "SETTINGS_RUNTIME_SCHEMA_NOT_OBJECT",
            "settings_runtime drive-only schema 顶层不是对象，已 fail-closed。",
            type(payload).__name__)
    schema = payload.get("schema")
    if schema is not None and schema != SETTINGS_RUNTIME_SCHEMA:
        raise DriveActionError(
            "SETTINGS_RUNTIME_SCHEMA_MISMATCH",
            "settings_runtime schema 不是 drive-only v1，已 fail-closed。",
            {"schema": schema, "expected": SETTINGS_RUNTIME_SCHEMA})
    if not isinstance(payload.get("axes"), list):
        raise DriveActionError(
            "SETTINGS_RUNTIME_SCHEMA_AXES_MISSING",
            "settings_runtime drive-only schema 缺少 axes 列表，已 fail-closed。",
            payload)
    _validate_settings_runtime_node(payload, "$", 0, [0])


def _sanitize_settings_runtime_node(
    value: Any, path: str, depth: int, node_count: List[int],
) -> Any:
    if depth > MAX_SETTINGS_RUNTIME_DEPTH:
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_TOO_DEEP",
            "settings_runtime drive-only schema 嵌套过深，已 fail-closed。", path)
    node_count[0] += 1
    if node_count[0] > MAX_SETTINGS_RUNTIME_NODES:
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_TOO_LARGE",
            "settings_runtime drive-only schema 节点数超出预算，已 fail-closed。",
            path, {"max_nodes": MAX_SETTINGS_RUNTIME_NODES})
    if isinstance(value, dict):
        clean: Dict[str, Any] = {}
        for raw_key, item in value.items():
            if not isinstance(raw_key, str) or not raw_key.strip():
                raise _settings_runtime_schema_error(
                    "SETTINGS_RUNTIME_SCHEMA_BAD_KEY",
                    "settings_runtime drive-only schema 含非法 key，已 fail-closed。",
                    path, raw_key)
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
            clean[raw_key] = _sanitize_settings_runtime_node(
                item, "%s.%s" % (path, raw_key), depth + 1, node_count)
        return clean
    if isinstance(value, list):
        return [
            _sanitize_settings_runtime_node(
                item, "%s[%d]" % (path, index), depth + 1, node_count)
            for index, item in enumerate(value)
        ]
    if isinstance(value, float) and not math.isfinite(value):
        raise _settings_runtime_schema_error(
            "SETTINGS_RUNTIME_SCHEMA_NON_FINITE",
            "settings_runtime drive-only schema 含 NaN/Inf，已 fail-closed。",
            path, value)
    return value


def sanitize_settings_runtime_drive_only(payload: Dict[str, Any]) -> Dict[str, Any]:
    clean = _sanitize_settings_runtime_node(payload, "$", 0, [0])
    if not isinstance(clean, dict):
        raise DriveActionError(
            "SETTINGS_RUNTIME_SCHEMA_NOT_OBJECT",
            "settings_runtime drive-only schema 顶层不是对象，已 fail-closed。",
            type(clean).__name__)
    clean["schema"] = SETTINGS_RUNTIME_SCHEMA
    drop_legacy_axis_ratio_copies(clean)
    validate_settings_runtime_drive_only(clean)
    return clean


def drive_only_scale_evidence(scale_evidence: Dict[str, Any]) -> Dict[str, Any]:
    allowed = (
        "unit", "counts_per_unit", "source", "pitch_mm_per_rev",
        "inferred_pitch_mm_per_rev", "encoder_bits",
        "bit_counts_per_motor_rev", "egear_numerator", "egear_denominator",
        "bit_egear_counts_per_motor_rev", "actual_counts_per_motor_rev",
        "chain_counts_per_unit", "scale_chain_delta",
        "scale_chain_relative_delta",
    )
    return {
        key: scale_evidence.get(key)
        for key in allowed
        if key in scale_evidence and scale_evidence.get(key) is not None
    }
