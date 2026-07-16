"""Resident drive-runtime projection of the canonical C motion-model registry.

`v5_motion_model_registry.h` remains the only descriptor owner. The focused
active-model checker requires this projection to match every C field exactly.
"""

from __future__ import annotations

import re
from typing import Any, Dict, Tuple

import v5_drive_bus_contract as contract
from v5_drive_bus_contract import AXIS_ORDER, DriveActionError
from v5_drive_runtime_store import read_runtime_ini_sections


def _active_coordinate_axes(raw: Any, source: str) -> Tuple[str, ...]:
    text = str(raw or "").strip().upper()
    if not text:
        raise DriveActionError(
            "ACTIVE_MODEL_COORDINATES_MISSING",
            "active model 坐标定义缺失，驱动目标已 fail-closed。",
            {"source": source},
        )
    compact = re.sub(r"[\s,]+", "", text)
    if not re.fullmatch(r"[XYZABCUVW]+", compact):
        raise DriveActionError(
            "ACTIVE_MODEL_COORDINATES_INVALID",
            "active model 坐标定义含非法轴名或分隔符，驱动目标已 fail-closed。",
            {"source": source, "value": text},
        )
    axes = tuple(compact)
    if len(set(axes)) != len(axes):
        raise DriveActionError(
            "ACTIVE_MODEL_COORDINATES_DUPLICATE",
            "active model 坐标定义含重复轴，驱动目标已 fail-closed。",
            {"source": source, "value": text, "axes": list(axes)},
        )
    return axes


ACTIVE_MODEL_REGISTRY_PROJECTION_BY_CANONICAL = {
    "XYZAC_TRT": {
        "registry_id": 1,
        "display": "AC摇篮",
        "aliases": ("AC", "AC摇篮", "XYZAC", "XYZAC_TRT"),
        "kins_module": "xyzac-trt-kins",
        "kins_coordinates": "XYZAC",
        "traj_coordinates": "X Y Z A C",
        "wrapped_rotary_mask": 40,
        "first_rotary_axis": "A",
        "second_rotary_axis": "C",
        "first_status_slot": 3,
        "second_status_slot": 4,
        "first_g53_center": 0,
        "second_g53_center": 2,
        "first_center_wcs_component": 0,
        "second_center_wcs_component": 2,
        "active_axes": tuple("XYZAC"),
        "active_status_slots": (0, 1, 2, 3, 4),
    },
    "XYZBC_TRT": {
        "registry_id": 2,
        "display": "BC摇篮",
        "aliases": ("BC", "BC摇篮", "XYZBC", "XYZBC_TRT"),
        "kins_module": "xyzbc-trt-kins",
        "kins_coordinates": "XYZBC",
        "traj_coordinates": "X Y Z B C",
        "wrapped_rotary_mask": 48,
        "first_rotary_axis": "B",
        "second_rotary_axis": "C",
        "first_status_slot": 3,
        "second_status_slot": 4,
        "first_g53_center": 1,
        "second_g53_center": 2,
        "first_center_wcs_component": 1,
        "second_center_wcs_component": 2,
        "active_axes": tuple("XYZBC"),
        "active_status_slots": (0, 1, 2, 3, 4),
    },
}


def _validated_active_model_descriptor(model: str) -> Dict[str, Any]:
    descriptor = ACTIVE_MODEL_REGISTRY_PROJECTION_BY_CANONICAL.get(model)
    if not isinstance(descriptor, dict):
        raise DriveActionError(
            "ACTIVE_MODEL_UNSUPPORTED",
            "active runtime INI 的运动模型未登记驱动目标分支，驱动目标已 fail-closed。",
            {"model": model},
        )
    active_axes = descriptor.get("active_axes")
    status_slots = descriptor.get("active_status_slots")
    if (not isinstance(active_axes, tuple) or not active_axes or
            not isinstance(status_slots, tuple) or len(status_slots) != len(active_axes) or
            len(set(active_axes)) != len(active_axes) or len(set(status_slots)) != len(status_slots) or
            any(axis not in "XYZABCUVW" for axis in active_axes) or
            any(not isinstance(slot, int) or slot < 0 or slot >= len(AXIS_ORDER) for slot in status_slots)):
        raise DriveActionError(
            "ACTIVE_MODEL_DESCRIPTOR_INVALID",
            "active-model resident descriptor 的生效轴/状态槽非法，驱动目标已 fail-closed。",
            {"model": model, "active_axes": active_axes, "active_status_slots": status_slots},
        )
    kins_axes = _active_coordinate_axes(descriptor.get("kins_coordinates"), "descriptor.kins_coordinates")
    traj_axes = _active_coordinate_axes(descriptor.get("traj_coordinates"), "descriptor.traj_coordinates")
    first_axis = str(descriptor.get("first_rotary_axis") or "")
    second_axis = str(descriptor.get("second_rotary_axis") or "")
    first_slot = descriptor.get("first_status_slot")
    second_slot = descriptor.get("second_status_slot")
    slot_by_axis = dict(zip(active_axes, status_slots))
    if (kins_axes != active_axes or traj_axes != active_axes or
            slot_by_axis.get(first_axis) != first_slot or
            slot_by_axis.get(second_axis) != second_slot or
            first_axis == second_axis):
        raise DriveActionError(
            "ACTIVE_MODEL_DESCRIPTOR_INVALID",
            "active-model resident descriptor 与自身 kinematics/status-slot 语义不一致，驱动目标已 fail-closed。",
            {"model": model, "descriptor": descriptor},
        )
    return descriptor


def active_model_descriptor_from_sections(sections: Dict[str, Dict[str, str]]) -> Dict[str, Any]:
    rtcp = sections.get("RTCP") if isinstance(sections, dict) else None
    traj = sections.get("TRAJ") if isinstance(sections, dict) else None
    model = str((rtcp or {}).get("MODEL") or "").strip().upper()
    if not model:
        raise DriveActionError(
            "ACTIVE_MODEL_MISSING",
            "active runtime INI 缺少 RTCP/MODEL，驱动目标已 fail-closed。",
            {"section": "RTCP", "key": "MODEL"},
        )
    if not re.fullmatch(r"[A-Z0-9][A-Z0-9_.-]*", model):
        raise DriveActionError(
            "ACTIVE_MODEL_UNSUPPORTED",
            "active runtime INI 的运动模型身份格式非法，驱动目标已 fail-closed。",
            {"model": model},
        )
    descriptor = _validated_active_model_descriptor(model)
    descriptor_axes = descriptor["active_axes"]
    kins_module = str((rtcp or {}).get("KINS_MODULE") or "").strip()
    if not kins_module:
        raise DriveActionError(
            "ACTIVE_MODEL_KINS_MODULE_MISSING",
            "active runtime INI 缺少 RTCP/KINS_MODULE，驱动目标已 fail-closed。",
            {"model": model, "section": "RTCP", "key": "KINS_MODULE"},
        )
    if kins_module != descriptor["kins_module"]:
        raise DriveActionError(
            "ACTIVE_MODEL_KINS_MODULE_MISMATCH",
            "active runtime INI 的 KINS_MODULE 不属于 MODEL 登记分支，驱动目标已 fail-closed。",
            {"model": model, "expected": descriptor["kins_module"], "actual": kins_module},
        )
    kins_axes = _active_coordinate_axes((rtcp or {}).get("KINS_COORDINATES"), "RTCP.KINS_COORDINATES")
    traj_axes = _active_coordinate_axes((traj or {}).get("COORDINATES"), "TRAJ.COORDINATES")
    if kins_axes != traj_axes:
        raise DriveActionError(
            "ACTIVE_MODEL_COORDINATES_MISMATCH",
            "active model descriptor 投影出的 KINS 坐标与 TRAJ 坐标不一致，驱动目标已 fail-closed。",
            {"model": model, "kins_axes": list(kins_axes), "traj_axes": list(traj_axes)},
        )
    if kins_axes != descriptor_axes:
        raise DriveActionError(
            "ACTIVE_MODEL_DESCRIPTOR_MISMATCH",
            "active runtime INI 的 KINS/TRAJ 坐标不属于 MODEL 登记分支，驱动目标已 fail-closed。",
            {
                "model": model,
                "descriptor_axes": list(descriptor_axes),
                "runtime_axes": list(kins_axes),
            },
        )
    return {
        "canonical": model,
        **descriptor,
        "active_axes": tuple(descriptor_axes),
        "active_status_slots": tuple(descriptor["active_status_slots"]),
    }


def active_model_axes_from_sections(sections: Dict[str, Dict[str, str]]) -> Tuple[str, ...]:
    return active_model_descriptor_from_sections(sections)["active_axes"]


def active_model_descriptor_from_runtime_ini() -> Dict[str, Any]:
    return active_model_descriptor_from_sections(read_runtime_ini_sections(contract.RUNTIME_SETTINGS_INI))
