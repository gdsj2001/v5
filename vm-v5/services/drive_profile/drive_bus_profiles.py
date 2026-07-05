#!/usr/bin/env python3
"""Drive profile matching utilities for v5 drive command mapping."""

from __future__ import annotations

from typing import Any, Callable, Dict, List, Optional, Tuple


def normalize_hex(value: Any, width: int = 8) -> str:
    text = str(value or "").strip().lower()
    if not text:
        return ""
    try:
        number = int(text, 16) if text.startswith("0x") else int(text, 10)
        return "0x%0*x" % (width, number)
    except Exception:
        return text


def profile_matches_slave(profile: Dict[str, Any], slave: Dict[str, Any]) -> bool:
    vendor = normalize_hex(profile.get("vendor_id"), 8)
    product = normalize_hex(profile.get("product_code"), 8)
    revision = normalize_hex(profile.get("revision"), 8)
    if vendor and vendor != str(slave.get("vendor_id", "")).lower():
        return False
    if product and product != str(slave.get("product_code", "")).lower():
        return False
    if revision and revision != str(slave.get("revision", "")).lower():
        return False
    pattern = str(profile.get("name_pattern") or "").strip().lower()
    name = ("%s %s" % (slave.get("order_number", ""), slave.get("name", ""))).lower()
    return not pattern or pattern in name


def profile_id(profile: Dict[str, Any]) -> str:
    detail = profile.get("profile_detail") if isinstance(profile.get("profile_detail"), dict) else {}
    return str(profile.get("profile_id") or detail.get("profile_id") or "").strip()


def axis_requested_profile_id(axis: Dict[str, Any]) -> str:
    for key in ("profile_id", "drive_profile_id", "profile"):
        value = str(axis.get(key) or "").strip()
        if value:
            return value
    return ""


def match_profile(
    slave: Dict[str, Any],
    profiles: List[Dict[str, Any]],
    requested_profile_id: str = "",
    *,
    error_factory: Callable[[str, str, Any], Exception] | None = None,
) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    matches = [profile for profile in profiles if profile_matches_slave(profile, slave)]
    if not matches:
        return {}, {}
    requested = str(requested_profile_id or "").strip()
    if requested:
        requested_matches = [profile for profile in matches if profile_id(profile) == requested]
        if not requested_matches:
            detail = {
                "profile_id": requested,
                "slave": slave,
                "matching_profile_ids": [profile_id(item) for item in matches],
            }
            if error_factory is not None:
                raise error_factory(
                    "DRIVE_PROFILE_ID_NOT_FOUND",
                    "轴参数表指定的驱动 profile_id 未在驱动映射表中匹配到",
                    detail,
                )
            raise ValueError("DRIVE_PROFILE_ID_NOT_FOUND")
        matches = requested_matches
    matches.sort(
        key=lambda item: (
            1 if str(item.get("map_source") or item.get("source") or item.get("map_scope") or "").lower() == "private" else 0,
            int(float(item.get("match_priority", 0) or 0)),
        ),
        reverse=True,
    )
    profile = matches[0]
    detail = profile.get("profile_detail", {}) if isinstance(profile.get("profile_detail", {}), dict) else {}
    return profile, detail


def positive_profile_float(value: Any) -> Optional[float]:
    try:
        if value is None or value == "":
            return None
        parsed = float(value)
    except Exception:
        return None
    return parsed if parsed > 0.0 else None


def encoder_counts_per_motor_rev_from_bits(bits: Any) -> Optional[int]:
    try:
        parsed = float(bits)
    except Exception:
        return None
    rounded = int(round(parsed))
    if rounded <= 0 or rounded > 62 or abs(parsed - float(rounded)) > 0.000001:
        return None
    return 1 << rounded


def sdo_object_token(obj: Any) -> str:
    if isinstance(obj, str):
        return obj.strip()
    if not isinstance(obj, dict):
        return ""
    index = obj.get("index")
    subindex = obj.get("subindex")
    if index in (None, "") or subindex in (None, ""):
        return ""
    return "%s:%s" % (str(index).strip(), str(subindex).strip())


def command_supported(command: Any) -> bool:
    return isinstance(command, dict) and bool(command.get("supported", True))


def command_timeout_seconds(command: Dict[str, Any], default: float) -> float:
    try:
        timeout_ms = float(command.get("timeout_ms", 0) or 0)
    except Exception:
        timeout_ms = 0.0
    return timeout_ms / 1000.0 if timeout_ms > 0 else float(default)


def pair_scalar_data_type(command: Dict[str, Any], default: str) -> str:
    data_type = str(command.get("scalar_data_type") or command.get("element_data_type") or "").strip()
    if data_type:
        return data_type
    data_type = str(command.get("data_type") or "").strip()
    if data_type.endswith("_pair"):
        return data_type[:-5]
    return data_type or default


def _add_command_object(objects: Dict[str, Any], commands: Dict[str, Any], command_name: str, object_key: str) -> None:
    command = commands.get(command_name)
    if not command_supported(command):
        return
    token = sdo_object_token(command.get("object"))
    if token:
        objects.setdefault(object_key, token)
        data_type = str(command.get("data_type") or "").strip()
        if data_type and data_type not in {"identity_group", "uint32_pair"}:
            objects.setdefault(object_key + "_type", data_type)
        timeout = command_timeout_seconds(command, 0.0)
        if timeout > 0:
            objects.setdefault(object_key + "_timeout", timeout)


def _add_egear_objects(objects: Dict[str, Any], commands: Dict[str, Any], command_name: str) -> None:
    command = commands.get(command_name)
    if not command_supported(command):
        return
    pair = command.get("objects")
    if not isinstance(pair, dict):
        pair = command.get("verify_object")
    if not isinstance(pair, dict):
        return
    numerator = sdo_object_token(pair.get("numerator"))
    denominator = sdo_object_token(pair.get("denominator"))
    data_type = pair_scalar_data_type(command, "")
    timeout = command_timeout_seconds(command, 0.0)
    if numerator:
        objects.setdefault("electronic_gear_num", numerator)
        if data_type:
            objects.setdefault("electronic_gear_num_type", data_type)
        if timeout > 0:
            objects.setdefault("electronic_gear_num_timeout", timeout)
    if denominator:
        objects.setdefault("electronic_gear_den", denominator)
        if data_type:
            objects.setdefault("electronic_gear_den_type", data_type)
        if timeout > 0:
            objects.setdefault("electronic_gear_den_timeout", timeout)


def objects_from_standard_commands(commands: Dict[str, Any]) -> Dict[str, Any]:
    objects: Dict[str, Any] = {}
    _add_command_object(objects, commands, "drive.read_statusword", "statusword")
    _add_command_object(objects, commands, "drive.write_controlword", "controlword")
    _add_command_object(objects, commands, "drive.software_reset", "software_reset")
    _add_command_object(objects, commands, "drive.reset_fault", "fault_reset")
    _add_command_object(objects, commands, "drive.restore_factory_defaults", "restore_factory_defaults")
    _add_command_object(objects, commands, "drive.read_error_code", "error_code")
    _add_command_object(objects, commands, "drive.write_mode", "mode_of_operation")
    _add_command_object(objects, commands, "drive.read_mode", "mode_display")
    _add_command_object(objects, commands, "drive.read_actual_position", "actual_position")
    _add_command_object(objects, commands, "drive.read_abs_multi_turn", "abs_multi_turn")
    _add_command_object(objects, commands, "drive.read_abs_single_turn", "abs_single_turn")
    _add_command_object(objects, commands, "drive.read_abs_position64", "abs_position64")
    _add_egear_objects(objects, commands, "drive.set_egear")
    _add_egear_objects(objects, commands, "drive.read_egear")
    return objects


def profile_objects(detail: Dict[str, Any]) -> Dict[str, Any]:
    objects = detail.get("objects", {}) if isinstance(detail.get("objects", {}), dict) else {}
    if objects:
        return objects
    commands = detail.get("commands", {}) if isinstance(detail.get("commands", {}), dict) else {}
    return objects_from_standard_commands(commands) if commands else {}


def profile_commands(detail: Dict[str, Any]) -> Dict[str, Any]:
    commands = detail.get("commands", {}) if isinstance(detail.get("commands", {}), dict) else {}
    return commands
