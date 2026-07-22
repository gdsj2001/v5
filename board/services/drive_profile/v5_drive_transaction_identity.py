from __future__ import annotations

import hashlib
import json
from typing import Any, Dict, List, Tuple

import v5_drive_bus_context as context
from v5_command_gate_zero_client import probe_axis_slave_mapping
from v5_drive_axis_model import mark_drive_parameters_invalid, target_egear
from v5_drive_bus_contract import DriveActionError
from v5_drive_query import configured_drive_targets
from v5_drive_sdo import parse_slave_identity


IDENTITY_SCHEMA = "v5.drive_set_transaction_identity.v1"


def planned_drive_transaction(
        targets: List[Dict[str, Any]]) -> Dict[
            str, Tuple[Tuple[int, int], Dict[str, Any]]]:
    planned: Dict[str, Tuple[Tuple[int, int], Dict[str, Any]]] = {}
    for target in targets:
        egear = target_egear(target)
        planned[str(target.get("position") or "")] = (
            (egear[0], egear[1]), egear[2])
    return planned


def invalidate_stale_drive_transaction_evidence(
        targets: List[Dict[str, Any]],
        transaction_identity: Dict[str, Any]) -> List[Dict[str, Any]]:
    current_generation = str(
        transaction_identity.get("transaction_generation") or "")
    invalidated: List[Dict[str, Any]] = []
    if not current_generation:
        raise DriveActionError(
            "DRIVE_TRANSACTION_GENERATION_MISSING",
            "Drive transaction validation failed.",
            transaction_identity,
        )
    for target in targets:
        axis_cfg = target.get("axis_cfg") if isinstance(
            target.get("axis_cfg"), dict) else {}
        electronic = axis_cfg.get("electronic_gear") if isinstance(
            axis_cfg.get("electronic_gear"), dict) else {}
        set_evidence = axis_cfg.get("drive_set_evidence") if isinstance(
            axis_cfg.get("drive_set_evidence"), dict) else {}
        stored_identity = axis_cfg.get("drive_transaction_identity")
        if not isinstance(stored_identity, dict):
            stored_identity = electronic.get("drive_transaction_identity")
        if not isinstance(stored_identity, dict):
            stored_identity = set_evidence.get("drive_transaction_identity")
        stored_generation = str(
            stored_identity.get("transaction_generation") or ""
        ) if isinstance(stored_identity, dict) else ""
        has_evidence = bool(
            axis_cfg.get("drive_parameters_valid") or set_evidence or
            electronic.get("write_status") == "write_verified_readback")
        if has_evidence and stored_generation != current_generation:
            mark_drive_parameters_invalid(
                axis_cfg, "drive_transaction_generation_changed",
                "pending_set_drive_for_current_generation")
            invalidated.append({
                "axis": str(target.get("axis") or ""),
                "position": str(target.get("position") or ""),
                "previous_generation": stored_generation,
                "current_generation": current_generation,
            })
    return invalidated


def _canonical_hash(payload: Any) -> str:
    encoded = json.dumps(
        payload, ensure_ascii=False, sort_keys=True,
        separators=(",", ":"), allow_nan=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _fnv1a_u32(value: int, current: int) -> int:
    result = int(current) & 0xFFFFFFFF
    for byte_index in range(4):
        result ^= (int(value) >> (byte_index * 8)) & 0xFF
        result = (result * 16777619) & 0xFFFFFFFF
    return result


def persistent_mapping_generation(targets: List[Dict[str, Any]]) -> int:
    by_slot: Dict[int, Tuple[str, int]] = {}
    for target in targets:
        axis = str(target.get("axis") or "").strip().upper()
        try:
            slot = int(target.get("status_slot"))
            slave = int(str(target.get("position") or ""))
        except (TypeError, ValueError):
            raise DriveActionError(
                "DRIVE_TRANSACTION_MAPPING_INVALID",
                "Drive transaction validation failed.",
                {"axis": axis, "status_slot": target.get("status_slot"),
                 "position": target.get("position")},
            )
        if len(axis) != 1 or slot in by_slot or slot < 0 or slot >= 5 or slave < 0 or slave >= 5:
            raise DriveActionError(
                "DRIVE_TRANSACTION_MAPPING_INVALID",
                "Drive transaction validation failed.",
                {"axis": axis, "status_slot": slot, "position": slave},
            )
        by_slot[slot] = (axis, slave)
    if sorted(by_slot) != list(range(5)) or len({item[1] for item in by_slot.values()}) != 5:
        raise DriveActionError(
            "DRIVE_TRANSACTION_MAPPING_INVALID",
            "Drive transaction validation failed.",
            {"slots": sorted(by_slot), "mapping": by_slot},
        )
    generation = 2166136261
    for slot in range(5):
        axis, slave = by_slot[slot]
        generation = _fnv1a_u32(slot, generation)
        generation = _fnv1a_u32(ord(axis), generation)
        generation = _fnv1a_u32(slave, generation)
    return generation or 1


def _physical_rows(targets: List[Dict[str, Any]], timeout_s: float,
                   fresh: bool) -> List[Dict[str, str]]:
    rows: List[Dict[str, str]] = []
    for target in sorted(targets, key=lambda item: int(item.get("status_slot", -1))):
        position = str(target.get("position") or "")
        try:
            slave_identity = parse_slave_identity(
                position, min(max(timeout_s, 0.1), 3.0)) if fresh else target.get("identity")
        except Exception as exc:
            raise DriveActionError(
                "DRIVE_TRANSACTION_PHYSICAL_IDENTITY_UNAVAILABLE",
                "Drive transaction validation failed.",
                {"axis": target.get("axis"), "position": position,
                 "error": "%s: %s" % (type(exc).__name__, exc)},
            )
        identity = slave_identity
        if not isinstance(identity, dict) or not identity.get("identity_ok"):
            raise DriveActionError(
                "DRIVE_TRANSACTION_PHYSICAL_IDENTITY_MISSING",
                "Drive transaction validation failed.",
                {"axis": target.get("axis"), "position": position, "identity": identity},
            )
        profile = target.get("profile") if isinstance(target.get("profile"), dict) else {}
        rows.append({
            "axis": str(target.get("axis") or "").upper(),
            "status_slot": str(int(target.get("status_slot"))),
            "position": position,
            "vendor_id": str(identity.get("vendor_id") or "").lower(),
            "product_code": str(identity.get("product_code") or "").lower(),
            "revision": str(identity.get("revision") or "").lower(),
            "profile_id": str(profile.get("profile_id") or ""),
            "profile_map_sha256": str(profile.get("profile_map_sha256") or ""),
        })
    return rows


def _native_mapping_status(timeout_s: float) -> Dict[str, Any]:
    try:
        return probe_axis_slave_mapping(min(max(timeout_s, 0.1), 1.0))
    except Exception as exc:
        raise DriveActionError(
            "DRIVE_TRANSACTION_NATIVE_MAPPING_UNAVAILABLE",
            "Drive transaction validation failed.",
            {"error": "%s: %s" % (type(exc).__name__, exc)},
        )


def _semantic_identity(targets: List[Dict[str, Any]],
                       planned: Dict[str, Tuple[Tuple[int, int], Dict[str, Any]]],
                       scan: Dict[str, Any],
                       physical_rows: List[Dict[str, str]]) -> Dict[str, Any]:
    ratio_rows: List[Dict[str, Any]] = []
    profile_rows: List[Dict[str, Any]] = []
    for target in sorted(targets, key=lambda item: int(item.get("status_slot", -1))):
        position = str(target.get("position") or "")
        if position not in planned:
            raise DriveActionError(
                "DRIVE_TRANSACTION_PLAN_MISSING",
                "Drive transaction validation failed.",
                {"axis": target.get("axis"), "position": position},
            )
        egear, source = planned[position]
        ratio_rows.append({
            "axis": str(target.get("axis") or "").upper(),
            "status_slot": int(target.get("status_slot")),
            "position": position,
            "scale": source.get("scale"),
            "target_precision": source.get("target_precision"),
            "pitch": source.get("pitch_units_per_load_rev"),
            "motor_rev": source.get("motor_rev"),
            "load_rev": source.get("load_rev"),
            "motor_revs_per_load_rev": source.get("motor_revs_per_load_rev"),
            "target_command_counts_per_motor_rev": source.get("target_command_counts_per_motor_rev"),
            "egear_numerator": int(egear[0]),
            "egear_denominator": int(egear[1]),
        })
        profile = target.get("profile") if isinstance(target.get("profile"), dict) else {}
        profile_rows.append({
            "axis": str(target.get("axis") or "").upper(),
            "position": position,
            "profile_id": str(profile.get("profile_id") or ""),
            "profile_map_sha256": str(profile.get("profile_map_sha256") or ""),
            "expected_profile_id": str(
                (target.get("axis_cfg") or {}).get("drive_profile_id") or
                (target.get("axis_cfg") or {}).get("profile_id") or ""),
            "encoder_bits": source.get("encoder_bits"),
            "encoder_bits_source": str(source.get("encoder_bits_source") or ""),
        })
    model = {
        "canonical": str(scan.get("active_model") or ""),
        "active_axes": list(scan.get("active_model_axes") or []),
        "active_status_slots": list(scan.get("active_model_status_slots") or []),
    }
    return {
        "model": model,
        "model_generation": _canonical_hash(model),
        "persistent_mapping_generation": persistent_mapping_generation(targets),
        "runtime_ratio_generation": _canonical_hash(ratio_rows),
        "profile_bit_generation": _canonical_hash(profile_rows),
        "physical_slave_generation": _canonical_hash(physical_rows),
        "ratio_rows": ratio_rows,
        "profile_rows": profile_rows,
        "physical_rows": physical_rows,
    }


def capture_drive_transaction_identity(
        targets: List[Dict[str, Any]],
        planned: Dict[str, Tuple[Tuple[int, int], Dict[str, Any]]],
        scan: Dict[str, Any], timeout_s: float) -> Dict[str, Any]:
    mapping = _native_mapping_status(timeout_s)
    if not mapping.get("ok"):
        raise DriveActionError(
            "DRIVE_TRANSACTION_NATIVE_MAPPING_UNAVAILABLE",
            "Drive transaction validation failed.",
            mapping,
        )
    physical_rows = _physical_rows(targets, timeout_s, fresh=False)
    semantic = _semantic_identity(targets, planned, scan, physical_rows)
    native_generation = int(mapping.get("generation") or 0)
    stable_generation = {
        "schema": IDENTITY_SCHEMA,
        "model_generation": semantic["model_generation"],
        "persistent_mapping_generation": semantic["persistent_mapping_generation"],
        "runtime_ratio_generation": semantic["runtime_ratio_generation"],
        "profile_bit_generation": semantic["profile_bit_generation"],
        "physical_slave_generation": semantic["physical_slave_generation"],
    }
    identity: Dict[str, Any] = {
        "schema": IDENTITY_SCHEMA,
        **semantic,
        "native_mapping_generation": native_generation,
        "expected_post_restart_native_mapping_generation": semantic["persistent_mapping_generation"],
        "native_mapping_code": str(mapping.get("code") or ""),
        "native_mapping_matches_persistent": native_generation == semantic["persistent_mapping_generation"],
    }
    identity["transaction_generation"] = _canonical_hash(stable_generation)
    return identity


def reload_drive_transaction_identity(timeout_s: float) -> Dict[str, Any]:
    context.reset_resident_preload_caches()
    context.resident_preload_active = True
    targets, _runtime, scan = configured_drive_targets(timeout_s)
    return capture_drive_transaction_identity(
        targets, planned_drive_transaction(targets), scan, timeout_s)



def verify_drive_transaction_identity(
        frozen: Dict[str, Any], current: Dict[str, Any],
        stage: str) -> Dict[str, Any]:
    mismatches: List[Dict[str, Any]] = []
    for field in (
            "transaction_generation",
            "model_generation",
            "persistent_mapping_generation",
            "runtime_ratio_generation",
            "profile_bit_generation",
            "physical_slave_generation"):
        if current.get(field) != frozen.get(field):
            mismatches.append({
                "field": field,
                "expected": frozen.get(field),
                "actual": current.get(field),
            })
    if int(current.get("native_mapping_generation") or 0) != int(
            frozen.get("native_mapping_generation") or 0):
        mismatches.append({
            "field": "native_mapping_generation",
            "expected": frozen.get("native_mapping_generation"),
            "actual": current.get("native_mapping_generation"),
        })
    if mismatches:
        raise DriveActionError(
            "DRIVE_TRANSACTION_IDENTITY_CHANGED",
            "Drive transaction validation failed.",
            {"stage": stage, "transaction_generation": frozen.get("transaction_generation"), "mismatches": mismatches},
        )
    return {
        "ok": True,
        "stage": stage,
        "transaction_generation": frozen.get("transaction_generation"),
        "native_mapping_generation": int(current.get("native_mapping_generation") or 0),
        "persistent_mapping_generation": current.get("persistent_mapping_generation"),
        "physical_slave_generation": current.get("physical_slave_generation"),
    }



def compact_drive_transaction_identity(identity: Dict[str, Any]) -> Dict[str, Any]:
    keys = (
        "schema", "transaction_generation", "model_generation",
        "persistent_mapping_generation", "native_mapping_generation",
        "expected_post_restart_native_mapping_generation",
        "native_mapping_matches_persistent", "runtime_ratio_generation",
        "profile_bit_generation", "physical_slave_generation",
    )
    return {key: identity.get(key) for key in keys if identity.get(key) is not None}
