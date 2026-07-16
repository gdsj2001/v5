from __future__ import annotations

import math
import re
from pathlib import Path
from typing import Any, Dict, List, Tuple

import v5_drive_bus_context as context
import v5_drive_bus_contract as contract
from v5_drive_bus_contract import AXIS_ORDER, DriveActionError, finite_float
from v5_drive_sdo import parse_slave_position_token

DRIVE_DISPLAY_FIELDS = {"egear_numerator", "egear_denominator", "write_status"}

def read_parameter_tsv(path: Path) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    if not path.is_file():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        text = line.strip("\r\n")
        if not text or text.startswith("#"):
            continue
        parts = text.split("\t")
        if len(parts) < 3:
            continue
        axis, field, value = parts[0].strip(), parts[1].strip(), parts[2].strip()
        if axis and field and value:
            rows.append((axis, field, value))
    return rows


def write_parameter_tsv(path: Path, rows: List[Tuple[str, str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    lines = ["# schema=v5.settings.parameter_table.tsv.v1"]
    for axis, field, value in rows:
        if axis and field and value:
            lines.append("%s\t%s\t%s" % (axis, field, value))
    tmp.write_text("\n".join(lines) + "\n", encoding="utf-8")
    tmp.replace(path)


def scan_slave_option_token(slave: Dict[str, Any]) -> str:
    position = str(slave.get("position") or "").strip()
    if not position:
        return ""
    name = str(slave.get("name") or "").strip()
    if not name:
        line_parts = str(slave.get("line") or "").split()
        if line_parts:
            name = line_parts[-1]
    if name == "+" or name == position:
        name = ""
    name = re.sub(r"[\t\r\n,;|:]+", "_", name).strip()
    return "%s:%s" % (position, name) if name else position


def write_scan_self_parameter_table(scan: Dict[str, Any]) -> None:
    if not scan.get("ok"):
        return
    options: List[str] = []
    seen_positions = set()
    for slave in scan.get("slaves", []):
        if not isinstance(slave, dict):
            continue
        position = str(slave.get("position") or "").strip()
        token = scan_slave_option_token(slave)
        if position and token and position not in seen_positions:
            options.append(token)
            seen_positions.add(position)
    if not options:
        return
    rows = [
        row for row in read_parameter_tsv(contract.SELF_PARAMETER_TABLE)
        if not (row[0] == "SETTINGS" and row[1] == "slave_options")
    ]
    rows.append(("SETTINGS", "slave_options", ",".join(options)))
    write_parameter_tsv(contract.SELF_PARAMETER_TABLE, rows)


def normalize_self_slave_binding(axis: str, value: Any) -> str:
    raw = str(value if value is not None else "").strip()
    if not raw:
        return ""
    token = re.split(r"[\s:;,|]+", raw, 1)[0].strip()
    if token.upper() == "NAT":
        return "NAT"
    if not re.fullmatch(r"\d+", token):
        raise DriveActionError("DRIVE_TARGET_SELF_SLAVE_INVALID", "自建参数表中的轴从站号非法，未访问驱动。", {"axis": axis, "slave": value})
    return str(int(token))


def load_self_slave_bindings() -> Dict[str, str]:
    if context.self_slave_binding_cache is not None:
        return context.self_slave_binding_cache
    if not context.resident_preload_active:
        raise DriveActionError("SELF_PARAMETER_RESIDENT_NOT_PRELOADED", "self 参数表未在启动/受控刷新阶段载入内存，驱动目标拒绝临时读盘。", str(contract.SELF_PARAMETER_TABLE))
    bindings: Dict[str, str] = {}
    for axis, field, value in read_parameter_tsv(contract.SELF_PARAMETER_TABLE):
        axis_name = str(axis or "").upper()
        if axis_name in AXIS_ORDER and field == "slave":
            binding = normalize_self_slave_binding(axis_name, value)
            if binding:
                bindings[axis_name] = binding
    context.self_slave_binding_cache = bindings
    return bindings


def drive_display_slave_key(position: Any) -> str:
    try:
        token = parse_slave_position_token(position)
    except Exception:
        token = ""
    return "SLAVE_%s" % token if token else ""


def format_drive_display_int(value: Any) -> str:
    number = finite_float(value)
    if number is None or not math.isfinite(number):
        return ""
    if abs(number - round(number)) < 1e-6:
        return str(int(round(number)))
    return "%.12g" % number


def resident_axis_by_slave_position() -> Dict[str, str]:
    bindings = load_self_slave_bindings()
    mapping: Dict[str, str] = {}
    for axis in AXIS_ORDER:
        if axis not in bindings:
            raise DriveActionError(
                "DRIVE_TARGET_SELF_SLAVE_MISSING",
                "resident self owner 缺少轴从站绑定，未访问驱动。",
                {"axis": axis},
            )
        raw_binding = bindings[axis]
        if str(raw_binding).upper() == "NAT":
            continue
        position = parse_slave_position_token(raw_binding)
        if not position:
            raise DriveActionError(
                "DRIVE_TARGET_SELF_SLAVE_INVALID",
                "resident self owner 的轴从站绑定非法，未访问驱动。",
                {"axis": axis, "slave": raw_binding},
            )
        if position in mapping:
            raise DriveActionError(
                "DRIVE_TARGET_DUPLICATE_SLAVE",
                "resident self owner 中多个轴绑定到同一从站，未访问驱动。",
                {"axis": axis, "other_axis": mapping[position], "slave_index": position},
            )
        mapping[position] = axis
    return mapping


def write_drive_parameter_display_rows(updates: List[Dict[str, Any]]) -> Dict[str, Any]:
    normalized: List[Tuple[str, str, str]] = []
    touched_rows = set()
    touched_axes = set()
    touched_slaves = set()
    for item in updates:
        axis = str(item.get("axis") or "").upper()
        slave_key = drive_display_slave_key(item.get("position"))
        row_keys: List[str] = []
        if axis in AXIS_ORDER:
            row_keys.append(axis)
            touched_axes.add(axis)
        if slave_key:
            row_keys.append(slave_key)
            touched_slaves.add(slave_key)
        if not row_keys:
            continue
        fields: Dict[str, str] = {}
        numerator = format_drive_display_int(item.get("egear_numerator"))
        denominator = format_drive_display_int(item.get("egear_denominator"))
        status = str(item.get("write_status") or "").strip()
        if numerator:
            fields["egear_numerator"] = numerator
        if denominator:
            fields["egear_denominator"] = denominator
        if status:
            fields["write_status"] = status
        if not fields:
            continue
        for row_key in row_keys:
            touched_rows.add(row_key)
            for field in ("egear_numerator", "egear_denominator", "write_status"):
                value = fields.get(field)
                if value:
                    normalized.append((row_key, field, value))
    if not touched_rows:
        return {"ok": False, "code": "DRIVE_DISPLAY_TABLE_NO_UPDATES", "path": str(contract.DRIVE_PARAMETER_TABLE)}
    rows = [
        row for row in read_parameter_tsv(contract.DRIVE_PARAMETER_TABLE)
        if not (row[0].upper() in touched_rows and row[1] in DRIVE_DISPLAY_FIELDS)
    ]
    rows.extend(normalized)
    write_parameter_tsv(contract.DRIVE_PARAMETER_TABLE, rows)
    return {
        "ok": True,
        "code": "DRIVE_DISPLAY_TABLE_UPDATED",
        "path": str(contract.DRIVE_PARAMETER_TABLE),
        "axis_count": len(touched_axes),
        "slave_count": len(touched_slaves),
        "field_count": len(normalized),
    }


def drive_display_update_from_health(axis: str, health: Dict[str, Any], status: str, position: Any = "") -> Dict[str, Any]:
    return {
        "axis": axis,
        "position": str(position or ""),
        "egear_numerator": health.get("egear_numerator"),
        "egear_denominator": health.get("egear_denominator"),
        "write_status": status,
    }


def format_scan_slave_display(scan: Dict[str, Any]) -> str:
    slaves = scan.get("slaves", []) if isinstance(scan, dict) else []
    if not isinstance(slaves, list):
        slaves = []
    cells: List[str] = []
    for index, slave in enumerate(slaves[:8], start=1):
        position = ""
        name = ""
        if isinstance(slave, dict):
            position = str(slave.get("position") or "").strip()
            name = str(slave.get("name") or "").strip()
            if not name:
                line_parts = str(slave.get("line") or "").split()
                if line_parts:
                    name = line_parts[-1]
                if name == "+" or name == position:
                    name = ""
        cells.append("%d.从站%s %s" % (index, position, name) if position and name else ("%d.从站%s" % (index, position) if position else "%d.--" % index))
    while len(cells) < 8:
        cells.append("%d.--" % (len(cells) + 1))
    rows = ["%-28s %s" % (cells[row], cells[row + 4]) for row in range(4)]
    message = "扫描从站完成，扫描到%d个驱动。\n驱动列表:\n%s" % (len(slaves), "\n".join(rows))
    if len(slaves) > 8:
        message += "\n另有%d个见从站下拉" % (len(slaves) - 8)
    return message
