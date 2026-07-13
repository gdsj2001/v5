from __future__ import annotations

import math
import re
from typing import Any, Dict, List, Tuple

import v5_drive_bus_contract as contract
from v5_drive_bus_contract import DriveActionError, axis_unit, finite_float, now_utc
from v5_drive_parameter_table import read_parameter_tsv
from v5_drive_runtime_store import (
    find_runtime_axis,
    load_settings_runtime,
    persist_axis_zero_model,
    read_runtime_ini_sections,
    runtime_ini_joint_index,
    runtime_ini_value,
    saved_zero_counts,
)
from v5_drive_sdo import parse_slave_identity, read_command, read_resident_snapshot, select_profile

def target_egear(target: Dict[str, Any]) -> Tuple[int, int, Dict[str, Any]]:
    axis = str(target.get("axis") or "").upper()
    axis_index = int(target.get("axis_index") or 0)
    profile = target.get("profile") if isinstance(target.get("profile"), dict) else {}
    return target_egear_from_runtime_ini(axis, axis_index, profile)


def mark_drive_parameters_invalid(axis_cfg: Dict[str, Any], reason: str, write_status: str) -> None:
    axis_cfg["drive_parameters_valid"] = False
    axis_cfg["drive_parameters_invalid_reason"] = reason
    electronic = dict(axis_cfg.get("electronic_gear")) if isinstance(axis_cfg.get("electronic_gear"), dict) else {}
    electronic["write_status"] = write_status
    electronic["invalidated_at"] = now_utc()
    axis_cfg["electronic_gear"] = electronic
    zero_model = dict(axis_cfg.get("zero_model")) if isinstance(axis_cfg.get("zero_model"), dict) else {}
    if zero_model:
        zero_model["drive_parameters_invalid_reason"] = reason
        zero_model["apply_state"] = "drive_parameters_invalid_pending_set_drive_and_restart"
        axis_cfg["zero_model"] = zero_model


def mark_reset_invalid(axis_cfg: Dict[str, Any], reason: str) -> None:
    mark_drive_parameters_invalid(axis_cfg, reason, "invalidated_by_factory_reset")


def apply_wcheckpoint_profile(axis_cfg: Dict[str, Any], profile: Dict[str, Any]) -> None:
    axis = str(axis_cfg.get("axis") or "").upper()
    if axis_unit(axis) != "deg":
        return
    numeric_fields = (
        "position_command_raw_bits",
        "position_command_raw_modulus",
        "position_feedback_raw_bits",
        "position_feedback_raw_modulus",
    )
    values: Dict[str, int] = {}
    for field in numeric_fields:
        value = finite_float(profile.get(field))
        if value is None or value <= 0.0 or abs(value - round(value)) > 1e-6:
            raise DriveActionError(
                "DRIVE_WCHECKPOINT_MAPPING_MISSING",
                "驱动映射缺少 wcheckpoint 所需的位置位宽或模数。",
                {"axis": axis, "profile_id": profile.get("profile_id", ""), "field": field},
            )
        values[field] = int(round(value))
    command_type = str(profile.get("position_command_raw_value_type") or "").lower()
    feedback_type = str(profile.get("position_feedback_raw_value_type") or "").lower()
    if not command_type or not feedback_type:
        raise DriveActionError(
            "DRIVE_WCHECKPOINT_VALUE_TYPE_MISSING",
            "驱动映射缺少 wcheckpoint 所需的位置数值类型。",
            {"axis": axis, "profile_id": profile.get("profile_id", "")},
        )
    axis_cfg.update(values)
    axis_cfg["position_command_raw_value_type"] = command_type
    axis_cfg["position_feedback_raw_value_type"] = feedback_type
    axis_cfg["position_command_raw_signed"] = 1 if command_type.startswith("int") else 0
    axis_cfg["position_feedback_raw_signed"] = 1 if feedback_type.startswith("int") else 0
    axis_cfg["drive_wrapped_rotary_support"] = bool(profile.get("drive_wrapped_rotary_support"))
    axis_cfg["drive_wrapped_rotary_support_flag"] = 1 if profile.get("drive_wrapped_rotary_support") else 0


def drive_parameter_axis_value(axis: str, field: str) -> str:
    target_axis = str(axis or "").upper()
    target_field = str(field or "")
    for row_axis, row_field, row_value in read_parameter_tsv(contract.DRIVE_PARAMETER_TABLE):
        if row_axis.upper() == target_axis and row_field == target_field:
            return row_value
    return ""


def final_encoder_bits(axis: str, profile: Dict[str, Any]) -> Tuple[int, Dict[str, Any]]:
    table_value = drive_parameter_axis_value(axis, "encoder_bits")
    bits = finite_float(table_value)
    source = "drive_parameter_table.encoder_bits"
    if bits is None:
        bits = finite_float(profile.get("encoder_bits")) or finite_float(profile.get("encoder_resolution_bits"))
        source = "drive_profile.encoder_bits"
    if bits is None or bits <= 0.0 or bits >= 63.0:
        raise DriveActionError("DRIVE_ENCODER_BITS_MISSING", "缺少最终 encoder_bits，不能计算电子齿轮。", {"axis": axis, "drive_parameter_table": str(contract.DRIVE_PARAMETER_TABLE), "profile_id": profile.get("profile_id", "")})
    rounded = int(round(bits))
    if abs(bits - float(rounded)) > 1e-6:
        raise DriveActionError("DRIVE_ENCODER_BITS_NON_INTEGER", "encoder_bits 必须是整数，不能计算电子齿轮。", {"axis": axis, "encoder_bits": bits, "source": source})
    return rounded, {"source": source, "raw_value": table_value if table_value else profile.get("encoder_bits")}


def target_egear_from_runtime_ini(axis: str, axis_index: int, profile: Dict[str, Any]) -> Tuple[int, int, Dict[str, Any]]:
    sections = read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
    axis_name = "AXIS_%s" % str(axis or "").upper()
    joint_index, joint_index_source = runtime_ini_joint_index(sections, axis, axis_index)
    joint_name = "JOINT_%d" % joint_index
    scale = runtime_ini_value(sections, [joint_name, axis_name], ["SCALE"])
    target_precision = runtime_ini_value(sections, [axis_name, joint_name], ["TARGET_PRECISION", "PRECISION", "TARGET_UNIT_PER_COUNT", "UNIT_PER_COUNT"])
    precision_source = "active_runtime_ini.target_precision"
    if target_precision is None and scale is not None and scale > 0.0:
        target_precision = 1.0 / scale
        precision_source = "active_runtime_ini.SCALE.inverse"
    pitch = runtime_ini_value(sections, [axis_name, joint_name], ["LEAD_PITCH_MM_PER_REV", "SCREW_PITCH_MM_PER_REV", "ROTARY_DEGREES_PER_REV", "PITCH"])
    ratio = runtime_ini_value(sections, [axis_name, joint_name], ["MOTOR_REVS_PER_LOAD_REV", "REDUCER_RATIO", "GEAR_RATIO"])
    ratio_source = "active_runtime_ini.ratio"
    motor_rev = runtime_ini_value(sections, [axis_name, joint_name], ["MOTOR_REV"])
    load_rev = runtime_ini_value(sections, [axis_name, joint_name], ["LOAD_REV"])
    if ratio is None and motor_rev is not None and load_rev is not None and load_rev > 0.0:
        ratio = motor_rev / load_rev
        ratio_source = "active_runtime_ini.motor_rev_load_rev"
    missing: List[str] = []
    if target_precision is None or target_precision <= 0.0:
        missing.append("target_precision")
    if pitch is None or pitch <= 0.0:
        missing.append("pitch")
    if ratio is None or ratio <= 0.0:
        missing.append("motor_rev/load_rev")
    if missing:
        raise DriveActionError("DRIVE_EGEAR_SCALE_CHAIN_MISSING", "缺少可信比例链输入，未写驱动。", {"axis": axis, "missing": missing, "runtime_ini": str(contract.RUNTIME_SETTINGS_INI), "axis_section": axis_name, "joint_section": joint_name})
    bits, bit_source = final_encoder_bits(axis, profile)
    drive_internal_counts = 1 << bits
    target_command_counts_float = float(pitch) / float(ratio) / float(target_precision)
    target_command_counts = int(round(target_command_counts_float))
    if target_command_counts <= 0:
        raise DriveActionError("DRIVE_EGEAR_TARGET_COUNTS_INVALID", "电子齿轮目标 count 非法，未写驱动。", {"axis": axis, "target_command_counts": target_command_counts, "target_command_counts_float": target_command_counts_float})
    divisor = math.gcd(drive_internal_counts, target_command_counts)
    numerator = drive_internal_counts // divisor
    denominator = target_command_counts // divisor
    if numerator <= 0 or denominator <= 0:
        raise DriveActionError("DRIVE_EGEAR_TARGET_INVALID", "电子齿轮目标分子/分母非法，未写驱动。", {"axis": axis, "numerator": numerator, "denominator": denominator})
    evidence = {
        "source": "active_runtime_ini_formula",
        "axis": axis,
        "axis_index": joint_index,
        "joint_index_source": joint_index_source,
        "configured_axis_index": axis_index,
        "runtime_ini": str(contract.RUNTIME_SETTINGS_INI),
        "axis_section": axis_name,
        "joint_section": joint_name,
        "unit": axis_unit(axis),
        "scale": scale,
        "target_precision": target_precision,
        "target_precision_source": precision_source,
        "pitch_units_per_load_rev": pitch,
        "motor_rev": motor_rev,
        "load_rev": load_rev,
        "motor_revs_per_load_rev": ratio,
        "ratio_source": ratio_source,
        "encoder_bits": bits,
        "encoder_bits_source": bit_source.get("source", ""),
        "drive_internal_counts_per_motor_rev": drive_internal_counts,
        "target_command_counts_per_motor_rev": target_command_counts,
        "target_command_counts_float": target_command_counts_float,
        "rounding_delta_counts": abs(target_command_counts_float - float(target_command_counts)),
        "formula": "egear = drive_internal_counts_per_motor_rev / round(pitch / ratio / target_precision)",
    }
    return numerator, denominator, evidence


def derive_counts_per_unit(axis: str, axis_cfg: Dict[str, Any], axis_index: int) -> Tuple[float, Dict[str, Any]]:
    unit = axis_unit(axis)
    sections = read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI)
    joint_index, joint_index_source = runtime_ini_joint_index(sections, axis, axis_index)
    joint_name = "JOINT_%d" % joint_index
    axis_name = "AXIS_%s" % str(axis or "").upper()
    scale = runtime_ini_value(sections, [joint_name, axis_name], ["SCALE"])
    pitch = runtime_ini_value(sections, [axis_name, joint_name], ["LEAD_PITCH_MM_PER_REV", "SCREW_PITCH_MM_PER_REV", "PITCH"])
    ratio = finite_float(axis_cfg.get("motor_revs_per_load_rev")) or finite_float(axis_cfg.get("reducer_ratio"))
    motor_rev = finite_float(axis_cfg.get("motor_rev"))
    load_rev = finite_float(axis_cfg.get("load_rev"))
    if ratio is None and motor_rev and load_rev and load_rev != 0.0:
        ratio = motor_rev / load_rev
    actual_counts_per_motor_rev = finite_float(axis_cfg.get("actual_counts_per_motor_rev")) or finite_float(axis_cfg.get("drive_command_counts_per_motor_rev")) or finite_float(axis_cfg.get("target_command_counts_per_motor_rev"))
    encoder_bits = finite_float(axis_cfg.get("encoder_bits")) or finite_float(axis_cfg.get("encoder_resolution_bits")) or finite_float(axis_cfg.get("raw_encoder_bits"))
    egear_num = finite_float(axis_cfg.get("egear_numerator"))
    egear_den = finite_float(axis_cfg.get("egear_denominator"))
    bit_counts = (2.0 ** encoder_bits) if encoder_bits is not None and 0.0 < encoder_bits < 63.0 else None
    bit_derived_counts = None
    if bit_counts and egear_num and egear_den and egear_num != 0.0:
        bit_derived_counts = bit_counts * egear_den / egear_num
    if actual_counts_per_motor_rev is None and bit_derived_counts is not None:
        actual_counts_per_motor_rev = bit_derived_counts
    inferred_pitch = None
    if unit != "deg" and pitch is None and scale is not None and scale > 0.0 and ratio and ratio > 0.0 and actual_counts_per_motor_rev and actual_counts_per_motor_rev > 0.0:
        inferred_pitch = (actual_counts_per_motor_rev * ratio) / scale
        pitch = inferred_pitch
    chain_counts_per_unit = None
    if unit == "deg":
        rotary_counts = finite_float(axis_cfg.get("rotary_load_counts_per_rev"))
        if rotary_counts and rotary_counts > 0.0:
            chain_counts_per_unit = rotary_counts / 360.0
        elif actual_counts_per_motor_rev and ratio and actual_counts_per_motor_rev > 0.0 and ratio > 0.0:
            chain_counts_per_unit = (actual_counts_per_motor_rev * ratio) / 360.0
    else:
        if pitch is not None and pitch > 0.0 and ratio and ratio > 0.0 and actual_counts_per_motor_rev and actual_counts_per_motor_rev > 0.0:
            chain_counts_per_unit = (actual_counts_per_motor_rev * ratio) / pitch
    if scale is not None and scale > 0.0:
        counts_per_unit = scale
        source = "active_runtime_ini.SCALE"
    elif chain_counts_per_unit is not None and chain_counts_per_unit > 0.0:
        counts_per_unit = chain_counts_per_unit
        source = "axis_ratio_chain"
    else:
        raise DriveActionError("SETTINGS_AXIS_ZERO_SCALE_CHAIN_MISSING", "缺少 SCALE 或螺距/减速比/bit 比例链，不能换算 mm/deg。", {"axis": axis, "unit": unit, "pitch": pitch, "ratio": ratio, "encoder_bits": encoder_bits, "actual_counts_per_motor_rev": actual_counts_per_motor_rev})
    evidence = {
        "unit": unit,
        "counts_per_unit": counts_per_unit,
        "source": source,
        "runtime_ini": str(contract.RUNTIME_SETTINGS_INI),
        "joint_section": joint_name,
        "joint_index": joint_index,
        "joint_index_source": joint_index_source,
        "configured_axis_index": axis_index,
        "axis_section": axis_name,
        "scale": scale,
        "pitch_mm_per_rev": pitch,
        "inferred_pitch_mm_per_rev": inferred_pitch,
        "motor_rev": motor_rev,
        "load_rev": load_rev,
        "motor_revs_per_load_rev": ratio,
        "reducer_ratio": finite_float(axis_cfg.get("reducer_ratio")),
        "encoder_bits": encoder_bits,
        "bit_counts_per_motor_rev": bit_counts,
        "egear_numerator": egear_num,
        "egear_denominator": egear_den,
        "bit_egear_counts_per_motor_rev": bit_derived_counts,
        "actual_counts_per_motor_rev": actual_counts_per_motor_rev,
        "chain_counts_per_unit": chain_counts_per_unit,
    }
    if scale is not None and chain_counts_per_unit is not None and chain_counts_per_unit > 0.0:
        evidence["scale_chain_delta"] = abs(scale - chain_counts_per_unit)
        evidence["scale_chain_relative_delta"] = abs(scale - chain_counts_per_unit) / max(abs(scale), 1.0)
    return counts_per_unit, evidence


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
    counts_per_unit, scale_evidence = derive_counts_per_unit(axis, axis_cfg, axis_index)
    captured_counts, capture_read_evidence = current_axis_counts(axis_cfg, timeout_s, slave_position)
    disk_write = persist_axis_zero_model(runtime, axis, axis_index, captured_counts, counts_per_unit, scale_evidence, capture_read_evidence)
    axis_after, _ = find_runtime_axis(runtime, axis)
    saved_counts = saved_zero_counts(axis_after)
    current_counts, verify_read_evidence = current_axis_counts(axis_after, timeout_s, slave_position)
    current_physical = current_counts / counts_per_unit
    saved_physical = saved_counts / counts_per_unit
    delta = abs(current_physical - saved_physical)
    ok = delta <= tolerance
    code = "SETTINGS_AXIS_ZERO_ENCODER_MATCH" if ok else "SETTINGS_AXIS_ZERO_ENCODER_MISMATCH"
    message = "%s轴设0校验%s：已先写入本次硬盘零位；当前编码器 %.6f %s，resident 新零位 %.6f %s，差值 %.6f %s，容差 %.3f %s。" % (
        axis,
        "成功" if ok else "失败",
        current_physical,
        unit,
        saved_physical,
        unit,
        delta,
        unit,
        tolerance,
        unit,
    )
    return {
        "ok": ok,
        "code": code,
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
        "settings_runtime_json": str(contract.SETTINGS_RUNTIME_JSON),
        "zero_model_source": (axis_after.get("zero_model") or {}).get("source", "") if isinstance(axis_after.get("zero_model"), dict) else "",
        "scale_chain": scale_evidence,
        "disk_write": disk_write,
        "capture_encoder_read": capture_read_evidence,
        "encoder_read": verify_read_evidence,
        "write_executed": True,
        "drive_write_executed": False,
        "motion_executed": False,
        "backend_restart_required": True,
        "restart_deferred": True,
        "raw_limit_disk_saved": True,
        "raw_limit_live_verified": False,
        "raw_runtime_zero_verified": False,
        "memory_zero_verified": False,
    }


def update_axis_drive_set_evidence(axis_cfg: Dict[str, Any],
                                   target: Dict[str, Any],
                                   egear: Tuple[int, int],
                                   readback: Dict[str, Any],
                                   egear_source: Dict[str, Any]) -> None:
    health = readback.get("health", {}) if isinstance(readback.get("health"), dict) else {}
    read_num = int(health.get("egear_numerator") or egear[0])
    read_den = int(health.get("egear_denominator") or egear[1])
    encoder_bits = finite_float(egear_source.get("encoder_bits")) or finite_float(axis_cfg.get("encoder_bits")) or finite_float(axis_cfg.get("encoder_resolution_bits")) or finite_float(axis_cfg.get("raw_encoder_bits")) or finite_float((target.get("profile") or {}).get("encoder_bits"))
    if encoder_bits is None or encoder_bits <= 0.0 or encoder_bits >= 63.0:
        raise DriveActionError("DRIVE_ENCODER_BITS_MISSING", "缺少最终 encoder_bits，不能刷新设置驱动证据。", {"axis": axis_cfg.get("axis")})
    drive_internal_counts = finite_float(egear_source.get("drive_internal_counts_per_motor_rev")) or (2.0 ** encoder_bits)
    feedback_counts = drive_internal_counts * float(read_den) / float(read_num)
    ratio = finite_float(egear_source.get("motor_revs_per_load_rev"))
    motor_rev = finite_float(egear_source.get("motor_rev"))
    load_rev = finite_float(egear_source.get("load_rev"))
    if ratio is None:
        ratio = finite_float(axis_cfg.get("motor_revs_per_load_rev")) or finite_float(axis_cfg.get("reducer_ratio"))
    if motor_rev is None:
        motor_rev = finite_float(axis_cfg.get("motor_rev"))
    if load_rev is None:
        load_rev = finite_float(axis_cfg.get("load_rev"))
    if ratio is None and motor_rev and load_rev and load_rev > 0.0:
        ratio = motor_rev / load_rev
    rotary_load_counts = feedback_counts * ratio if ratio and ratio > 0.0 else None
    apply_wcheckpoint_profile(axis_cfg, target.get("profile") or {})
    axis_cfg["encoder_bits"] = encoder_bits
    axis_cfg["encoder_resolution_bits"] = encoder_bits
    if motor_rev is not None:
        axis_cfg["motor_rev"] = motor_rev
    if load_rev is not None:
        axis_cfg["load_rev"] = load_rev
    if ratio is not None:
        axis_cfg["motor_revs_per_load_rev"] = ratio
        axis_cfg["reducer_ratio"] = ratio
    axis_cfg["drive_internal_counts_per_motor_rev"] = drive_internal_counts
    axis_cfg["encoder_single_turn_counts"] = int(round(drive_internal_counts))
    axis_cfg["egear_numerator"] = read_num
    axis_cfg["egear_denominator"] = read_den
    axis_cfg["feedback_counts_per_motor_rev"] = int(round(feedback_counts)) if abs(feedback_counts - round(feedback_counts)) < 1e-6 else feedback_counts
    axis_cfg["actual_counts_per_motor_rev"] = axis_cfg["feedback_counts_per_motor_rev"]
    axis_cfg["drive_command_counts_per_motor_rev"] = axis_cfg["feedback_counts_per_motor_rev"]
    if rotary_load_counts is not None:
        axis_cfg["rotary_load_counts_per_rev"] = int(round(rotary_load_counts)) if abs(rotary_load_counts - round(rotary_load_counts)) < 1e-6 else rotary_load_counts
        axis_cfg["rotary_crev_counts"] = axis_cfg["rotary_load_counts_per_rev"]
    axis_cfg["drive_parameters_valid"] = True
    axis_cfg.pop("drive_parameters_invalid_reason", None)
    electronic = dict(axis_cfg.get("electronic_gear")) if isinstance(axis_cfg.get("electronic_gear"), dict) else {}
    electronic.update({
        "numerator": egear[0],
        "denominator": egear[1],
        "computed_numerator": egear[0],
        "computed_denominator": egear[1],
        "readback_numerator": read_num,
        "readback_denominator": read_den,
        "actual_counts_per_motor_rev": axis_cfg["feedback_counts_per_motor_rev"],
        "feedback_counts_per_motor_rev": axis_cfg["feedback_counts_per_motor_rev"],
        "feedback_counts_source": "drive_internal_counts_and_egear_readback",
        "target_command_counts_per_motor_rev": egear_source.get("target_command_counts_per_motor_rev"),
        "target_command_counts_float": egear_source.get("target_command_counts_float"),
        "target_precision": egear_source.get("target_precision"),
        "target_precision_source": egear_source.get("target_precision_source", ""),
        "target_unit_per_count": egear_source.get("target_precision"),
        "pitch_units_per_load_rev": egear_source.get("pitch_units_per_load_rev"),
        "pitch_source": "active_runtime_ini",
        "ratio_source": egear_source.get("ratio_source", ""),
        "motor_revs_per_load_rev": ratio,
        "write_status": "write_verified_readback",
        "write_verified_at": now_utc(),
        "target_source": egear_source.get("source", ""),
    })
    if rotary_load_counts is not None:
        electronic["rotary_load_counts_per_rev"] = axis_cfg["rotary_load_counts_per_rev"]
    axis_cfg["electronic_gear"] = electronic
    axis_cfg["drive_readback"] = {
        "ok": True,
        "source": "drive_set",
        "updated_at": now_utc(),
        "axis_name": axis_cfg.get("axis") or target.get("axis"),
        "slave_index": target.get("position"),
        "profile_id": (target.get("profile") or {}).get("profile_id", ""),
        "statusword": health.get("statusword"),
        "error_code": health.get("error_code"),
        "mode_of_operation": health.get("mode_of_operation"),
        "mode_display": health.get("mode_of_operation"),
        "actual_position_counts": health.get("actual_position_counts"),
        "egear_numerator": read_num,
        "egear_denominator": read_den,
        "errors": [],
    }
    axis_cfg["drive_set_evidence"] = {
        "ok": True,
        "code": "DRIVE_SET_TARGET_OK",
        "updated_at": now_utc(),
        "slave_index": target.get("position"),
        "profile_id": (target.get("profile") or {}).get("profile_id", ""),
        "target_egear_numerator": egear[0],
        "target_egear_denominator": egear[1],
        "readback_egear_numerator": read_num,
        "readback_egear_denominator": read_den,
        "mode_of_operation": health.get("mode_of_operation"),
        "drive_internal_counts_per_motor_rev": drive_internal_counts,
        "feedback_counts_per_motor_rev": axis_cfg["feedback_counts_per_motor_rev"],
        "target_source": egear_source.get("source", ""),
        "target_command_counts_per_motor_rev": egear_source.get("target_command_counts_per_motor_rev"),
        "target_precision": egear_source.get("target_precision"),
        "pitch_units_per_load_rev": egear_source.get("pitch_units_per_load_rev"),
        "motor_revs_per_load_rev": ratio,
    }
