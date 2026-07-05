from __future__ import annotations

from typing import Any, Dict, List, Tuple
import drive_bus_profiles as profile_rules

class DriveCommandError(RuntimeError):
    def __init__(self, code: str, message: str, detail: Any = ""):
        super().__init__(message)
        self.code = code
        self.message = message
        self.detail = detail

def _sdo_object_token(obj: Any) -> str:
    return profile_rules.sdo_object_token(obj)

def _command_timeout_seconds(command: Dict[str, Any], default: float) -> float:
    return profile_rules.command_timeout_seconds(command, default)

def _pair_scalar_data_type(command: Dict[str, Any], default: str) -> str:
    return profile_rules.pair_scalar_data_type(command, default)

def _mapped_command_object(command: Dict[str, Any], command_name: str, key: str = "object") -> str:
    token = _sdo_object_token(command.get(key))
    if not token:
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_OBJECT_MISSING",
            "驱动映射表命令缺少对象，未访问驱动",
            {"standard_command": command_name, "field": key},
        )
    return token

def _mapped_command_data_type(command: Dict[str, Any], command_name: str) -> str:
    data_type = str(command.get("data_type") or "").strip()
    if not data_type or data_type in {"identity_group", "uint32_pair"}:
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_TYPE_MISSING",
            "驱动映射表命令缺少可写数据类型，未访问驱动",
            {"standard_command": command_name, "data_type": data_type},
        )
    return data_type

def _mapped_numeric_value(value: Any, command_name: str, field: str) -> int:
    if value is None or value == "":
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_VALUE_MISSING",
            "驱动映射表命令缺少写入值，未访问驱动",
            {"standard_command": command_name, "field": field},
        )
    try:
        text = str(value).strip()
        return int(text, 16) if text.lower().startswith("0x") else int(value)
    except Exception as exc:
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_VALUE_INVALID",
            "驱动映射表命令写入值非法，未访问驱动",
            {"standard_command": command_name, "field": field, "value": value, "error": "%s: %s" % (type(exc).__name__, exc)},
        )

def mapped_write_steps(command: Dict[str, Any], command_name: str) -> List[Dict[str, Any]]:
    raw_sequence = command.get("write_sequence")
    if isinstance(raw_sequence, list) and raw_sequence:
        steps = []
        for index, raw_step in enumerate(raw_sequence):
            if isinstance(raw_step, dict):
                value = raw_step.get("value", raw_step.get("write_value"))
                step = dict(raw_step)
            else:
                value = raw_step
                step = {}
            step["value"] = _mapped_numeric_value(value, command_name, "write_sequence[%d].value" % index)
            steps.append(step)
        return steps
    return [{"value": _mapped_numeric_value(command.get("write_value"), command_name, "write_value")}]

def mapped_pair_objects(command: Dict[str, Any], command_name: str) -> Tuple[str, str]:
    pair = command.get("objects")
    if not isinstance(pair, dict):
        pair = command.get("verify_object")
    if not isinstance(pair, dict):
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_OBJECT_MISSING",
            "驱动映射表命令缺少对象，未访问驱动",
            {"standard_command": command_name, "field": "objects"},
        )
    numerator = _sdo_object_token(pair.get("numerator"))
    denominator = _sdo_object_token(pair.get("denominator"))
    if not numerator or not denominator:
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_OBJECT_MISSING",
            "驱动映射表命令缺少对象，未访问驱动",
            {"standard_command": command_name, "field": "objects.numerator/denominator"},
        )
    return numerator, denominator

def mapped_pair_data_type(command: Dict[str, Any], command_name: str) -> str:
    data_type = _pair_scalar_data_type(command, "")
    if not data_type:
        raise DriveCommandError(
            "DRIVE_PROFILE_COMMAND_TYPE_MISSING",
            "驱动映射表命令缺少可写数据类型，未访问驱动",
            {"standard_command": command_name, "data_type": command.get("data_type")},
        )
    return data_type

def mapped_request_value(command: Dict[str, Any], command_name: str, request_values: Dict[str, Any]) -> int:
    source = str(command.get("value_source") or "").strip()
    if source.startswith("request.") and "/" not in source:
        key = source.split(".", 1)[1]
        if key not in request_values:
            raise DriveCommandError(
                "DRIVE_PROFILE_COMMAND_VALUE_MISSING",
                "驱动映射表命令指定的请求值不存在，未访问驱动",
                {"standard_command": command_name, "value_source": source},
            )
        return _mapped_numeric_value(request_values.get(key), command_name, source)
    return _mapped_numeric_value(command.get("write_value"), command_name, "write_value")
