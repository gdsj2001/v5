#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

RUN_DIR = Path("/run/8ax_v5_drive")
RESIDENT_SNAPSHOT = RUN_DIR / "drive_profile_resident_snapshot.json"
PROJECT_ROOT = Path(os.environ.get("V5_PROJECT_ROOT", "/opt/8ax/v5"))
SELF_PARAMETER_TABLE = PROJECT_ROOT / "config/settings/self_parameter_table.tsv"
SETTINGS_RUNTIME_JSON = Path(os.environ.get("V5_SETTINGS_RUNTIME_JSON", "/opt/8ax/phase0_bus5/settings_runtime.json"))
RUNTIME_SETTINGS_INI = Path(os.environ.get("V5_RUNTIME_SETTINGS_INI", str(PROJECT_ROOT / "linuxcnc/ini/v5_bus.ini")))
_RESIDENT_SNAPSHOT_CACHE: Dict[str, Any] | None = None
_SETTINGS_RUNTIME_CACHE: Dict[str, Any] | None = None
_RUNTIME_INI_SECTIONS_CACHE: Dict[str, Dict[str, Dict[str, str]]] = {}
_RESIDENT_PRELOAD_ACTIVE = False
MAX_COMMAND_OUTPUT_BYTES = 16000
MAX_RESULT_JSON_BYTES = 65536
AXIS_ORDER = ("X", "Y", "Z", "A", "B", "C", "GANTRY", "TOOLMAG")
REQUIRED_READ_COMMANDS = (
    "drive.read_statusword",
    "drive.read_error_code",
    "drive.read_mode",
    "drive.read_actual_position",
    "drive.read_egear",
)
OPTIONAL_READ_COMMANDS = (
    "drive.read_actual_velocity",
    "drive.read_abs_multi_turn",
    "drive.read_abs_single_turn",
    "drive.read_abs_position64",
    "drive.read_torque",
    "drive.read_current",
    "drive.read_temperature",
    "drive.read_dc_link_voltage",
    "drive.read_bus_state",
    "drive.read_following_error",
    "drive.read_io_status",
)
TYPE_MAP = {
    "bool": "bool",
    "int8": "int8",
    "uint8": "uint8",
    "int16": "int16",
    "uint16": "uint16",
    "int32": "int32",
    "uint32": "uint32",
    "int64": "int64",
    "uint64": "uint64",
    "float": "float",
    "real32": "float",
    "double": "double",
    "real64": "double",
}
SETTINGS_RUNTIME_SCHEMA = "re.v3.settings_runtime.drive_only.v1"
MAX_SETTINGS_RUNTIME_NODES = 20000
MAX_SETTINGS_RUNTIME_DEPTH = 24
SETTINGS_RUNTIME_FORBIDDEN_KEYS = frozenset({
    "active_wcs",
    "config_cache",
    "current_tool",
    "g5x",
    "g92",
    "g92_offset",
    "hal_snapshot",
    "ini_readback",
    "linuxcnc_snapshot_after",
    "linuxcnc_snapshot_before",
    "machine_state",
    "modal",
    "motion_state",
    "native_status",
    "post_status",
    "pre_status",
    "rtcp",
    "rtcp_actual",
    "runtime_ini",
    "runtime_ini_readback",
    "runtime_ini_sections",
    "runtime_modal_text",
    "safety",
    "settings_projection",
    "shm",
    "shm_frame",
    "status",
    "status_epoch",
    "status_frame",
    "stillness",
    "tlo",
    "tool",
    "tool_length",
    "tool_table",
    "wcs",
    "wcs_offset",
    "wcs_offsets",
})
SETTINGS_RUNTIME_FORBIDDEN_PREFIXES = (
    "g92_",
    "modal_",
    "rtcp_",
    "tlo_",
    "tool_",
    "wcs_",
)
SETTINGS_RUNTIME_WRITE_DROP_KEYS = frozenset({
    "axis_section",
    "ini_readback",
    "joint_section",
    "linuxcnc_snapshot_after",
    "linuxcnc_snapshot_before",
    "post_status",
    "pre_status",
    "raw_limit_save",
    "g53_geometry",
    "rtcp_g53_geometry",
    "rtcp_geometry",
    "runtime_ini",
    "runtime_ini_readback",
    "runtime_ini_sections",
    "runtime_scale_check_after",
    "runtime_scale_check_before",
    "status_frame",
    "updated_sections",
})


class DriveActionError(RuntimeError):
    def __init__(self, code: str, message_cn: str, detail: Any = None) -> None:
        super().__init__(message_cn)
        self.code = code
        self.message_cn = message_cn
        self.detail = detail


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
    validate_settings_runtime_drive_only(clean)
    return clean


def drive_only_scale_evidence(scale_evidence: Dict[str, Any]) -> Dict[str, Any]:
    allowed = (
        "unit",
        "counts_per_unit",
        "source",
        "pitch_mm_per_rev",
        "inferred_pitch_mm_per_rev",
        "motor_rev",
        "load_rev",
        "motor_revs_per_load_rev",
        "reducer_ratio",
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


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    if len(text.encode("utf-8")) > MAX_RESULT_JSON_BYTES:
        payload = {
            "schema": str(payload.get("schema") or "v5.drive_bus_action.v1"),
            "generated_at": now_utc(),
            "action": str(payload.get("action") or ""),
            "ok": False,
            "code": "DRIVE_ACTION_RESULT_BUDGET_EXCEEDED",
            "message_cn": "驱动动作结果超过常驻内存预算，已 fail-closed。",
            "write_executed": False,
            "motion_executed": False,
            "result_bytes": len(text.encode("utf-8")),
            "max_result_bytes": MAX_RESULT_JSON_BYTES,
        }
        text = json.dumps(payload, ensure_ascii=False, indent=2)
    tmp.write_text(text + "\n", encoding="utf-8")
    tmp.replace(path)


def run_command(argv: List[str], timeout_s: float) -> Dict[str, Any]:
    def tail_text(value: Any) -> str:
        if isinstance(value, bytes):
            text = value.decode("utf-8", errors="replace")
        else:
            text = str(value or "")
        data = text.encode("utf-8", errors="replace")
        if len(data) <= MAX_COMMAND_OUTPUT_BYTES:
            return text
        return data[-MAX_COMMAND_OUTPUT_BYTES:].decode("utf-8", errors="replace")

    try:
        proc = subprocess.run(argv, text=True, capture_output=True, timeout=timeout_s, check=False)
    except FileNotFoundError:
        return {"ok": False, "code": "ETHERCAT_TOOL_MISSING", "returncode": 127, "stdout": "", "stderr": "ethercat not found"}
    except subprocess.TimeoutExpired as exc:
        return {"ok": False, "code": "ETHERCAT_COMMAND_TIMEOUT", "returncode": None, "stdout": tail_text(exc.stdout), "stderr": tail_text(exc.stderr or "timeout")}
    return {"ok": proc.returncode == 0, "code": "OK" if proc.returncode == 0 else "ETHERCAT_COMMAND_FAILED", "returncode": proc.returncode, "stdout": tail_text(proc.stdout), "stderr": tail_text(proc.stderr)}


def run_ethercat_slaves(timeout_s: float) -> Dict[str, Any]:
    result = run_command(["ethercat", "slaves"], timeout_s)
    if result["code"] == "ETHERCAT_TOOL_MISSING":
        return {"ok": False, "code": "ETHERCAT_TOOL_MISSING", "message_cn": "板端缺少 ethercat 工具，不能扫描从站。", "slaves": []}
    if result["code"] == "ETHERCAT_COMMAND_TIMEOUT":
        return {"ok": False, "code": "ETHERCAT_SCAN_TIMEOUT", "message_cn": "扫描从站超时。", "slaves": []}
    slaves: List[Dict[str, Any]] = []
    for line in result["stdout"].splitlines():
        text = line.strip()
        if not text:
            continue
        parts = text.split()
        try:
            position = str(int(parts[0])) if parts else ""
        except Exception:
            position = parts[0] if parts else ""
        name = parts[-1] if parts else ""
        if name == "+" or name == position:
            name = ""
        slaves.append({"line": text, "position": position, "name": name})
    if result["ok"] and not slaves:
        return {"ok": False, "code": "DRIVE_SCAN_NO_SLAVES", "message_cn": "未扫描到 EtherCAT 从站。", "returncode": result["returncode"], "stdout": result["stdout"], "stderr": result["stderr"], "slaves": []}
    return {"ok": bool(result["ok"]), "code": "DRIVE_SCAN_OK" if result["ok"] else "DRIVE_SCAN_FAILED", "message_cn": "扫描从站完成。" if result["ok"] else "扫描从站失败。", "returncode": result["returncode"], "stdout": result["stdout"], "stderr": result["stderr"], "slaves": slaves}


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
        row for row in read_parameter_tsv(SELF_PARAMETER_TABLE)
        if not (row[0] == "SETTINGS" and row[1] == "slave_options")
    ]
    rows.append(("SETTINGS", "slave_options", ",".join(options)))
    write_parameter_tsv(SELF_PARAMETER_TABLE, rows)


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


def normalize_hex(value: Any, width: int = 8) -> str:
    text = str(value or "").strip().lower()
    if not text:
        return ""
    try:
        number = int(text, 16) if text.startswith("0x") else int(text, 10)
    except Exception:
        return text
    return "0x%0*x" % (width, number)


def read_resident_snapshot() -> Dict[str, Any]:
    global _RESIDENT_SNAPSHOT_CACHE
    if _RESIDENT_SNAPSHOT_CACHE is not None:
        return _RESIDENT_SNAPSHOT_CACHE
    if not _RESIDENT_PRELOAD_ACTIVE:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_NOT_PRELOADED", "驱动 profile resident snapshot 未在启动阶段载入内存，动作热路径拒绝读盘。", str(RESIDENT_SNAPSHOT))
    if not RESIDENT_SNAPSHOT.is_file():
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_MISSING", "驱动 profile 运行内存快照不存在，读取驱动未执行。", str(RESIDENT_SNAPSHOT))
    try:
        snapshot = json.loads(RESIDENT_SNAPSHOT.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_INVALID", "驱动 profile 运行内存快照损坏，读取驱动未执行。", "%s: %s" % (type(exc).__name__, exc))
    profiles = snapshot.get("profiles") if isinstance(snapshot, dict) else None
    if not isinstance(profiles, list) or not profiles:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_EMPTY", "驱动 profile 运行内存快照为空，读取驱动未执行。", snapshot)
    _RESIDENT_SNAPSHOT_CACHE = snapshot
    return snapshot


def parse_slave_identity(position: str, timeout_s: float) -> Dict[str, Any]:
    result = run_command(["ethercat", "slaves", "-p", str(position), "-v"], timeout_s)
    detail = {"position": str(position), "identity_ok": False, "identity_stdout": result.get("stdout", ""), "identity_stderr": result.get("stderr", "")}
    text = "%s\n%s" % (result.get("stdout", ""), result.get("stderr", ""))
    patterns = {
        "vendor_id": r"Vendor\s+Id\s*[:=]\s*(0x[0-9a-fA-F]+|\d+)",
        "product_code": r"Product\s+code\s*[:=]\s*(0x[0-9a-fA-F]+|\d+)",
        "revision": r"Revision\s+(?:number|no\.)?\s*[:=]\s*(0x[0-9a-fA-F]+|\d+)",
    }
    for key, pattern in patterns.items():
        match = re.search(pattern, text, re.IGNORECASE)
        if match:
            detail[key] = normalize_hex(match.group(1), 8)
    detail["identity_ok"] = bool(detail.get("vendor_id") and detail.get("product_code"))
    return detail


def profile_matches(profile: Dict[str, Any], slave: Dict[str, Any]) -> bool:
    vendor = normalize_hex(profile.get("vendor_id"), 8)
    product = normalize_hex(profile.get("product_code"), 8)
    revision = normalize_hex(profile.get("revision"), 8)
    if vendor and vendor != str(slave.get("vendor_id", "")).lower():
        return False
    if product and product != str(slave.get("product_code", "")).lower():
        return False
    if revision and slave.get("revision") and revision != str(slave.get("revision", "")).lower():
        return False
    return True


def select_profile(slave: Dict[str, Any], snapshot: Dict[str, Any]) -> Dict[str, Any]:
    matches: List[Dict[str, Any]] = []
    for profile in snapshot.get("profiles", []):
        if isinstance(profile, dict) and profile_matches(profile, slave):
            matches.append(profile)
    if not matches:
        raise DriveActionError("DRIVE_PROFILE_NO_MATCH", "运行内存快照中没有匹配真实从站的 profile。", slave)
    matches.sort(key=lambda item: 1 if str(item.get("map_source") or "").lower() == "private" else 0, reverse=True)
    return matches[0]


def sdo_object_tokens(obj: Any) -> Tuple[str, str]:
    if isinstance(obj, str):
        left, sep, right = obj.partition(":")
        return (left.strip(), right.strip()) if sep and left and right else ("", "")
    if not isinstance(obj, dict):
        return "", ""
    return str(obj.get("index") or "").strip(), str(obj.get("subindex") or "").strip()


def scalar_data_type(command: Dict[str, Any]) -> str:
    data_type = str(command.get("scalar_data_type") or command.get("element_data_type") or command.get("data_type") or "").strip()
    if data_type.endswith("_pair"):
        data_type = data_type[:-5]
    return TYPE_MAP.get(data_type, data_type)


def command_timeout(command: Dict[str, Any], default_s: float) -> float:
    try:
        timeout_ms = float(command.get("timeout_ms") or 0)
    except Exception:
        timeout_ms = 0.0
    return timeout_ms / 1000.0 if timeout_ms > 0 else default_s


def signed_type_bits(data_type: str) -> int:
    match = re.fullmatch(r"int(8|16|32|64)", str(data_type or "").strip().lower())
    return int(match.group(1)) if match else 0


def parse_integer_token(token: str) -> int:
    text = str(token or "").strip()
    return int(text, 16) if text.lower().startswith(("0x", "+0x", "-0x")) else int(text, 10)


def parse_upload_value(stdout: str, data_type: str = "") -> Any:
    tokens = re.findall(r"[-+]?0x[0-9a-fA-F]+|[-+]?\d+", stdout.strip())
    if not tokens:
        return None
    bits = signed_type_bits(data_type)
    if bits:
        for token in reversed(tokens):
            if "x" not in token.lower():
                try:
                    return int(token, 10)
                except Exception:
                    pass
        try:
            number = parse_integer_token(tokens[0])
        except Exception:
            return tokens[0]
        sign_bit = 1 << (bits - 1)
        modulus = 1 << bits
        return number - modulus if number & sign_bit else number
    try:
        return parse_integer_token(tokens[0])
    except Exception:
        return tokens[0]


def ethercat_upload(position: str, obj: Any, data_type: str, timeout_s: float) -> Dict[str, Any]:
    index, subindex = sdo_object_tokens(obj)
    if not index or not subindex:
        raise DriveActionError("DRIVE_PROFILE_COMMAND_OBJECT_MISSING", "驱动 profile 命令缺少 SDO 对象，未访问驱动。", obj)
    argv = ["ethercat", "upload", "-p", str(position)]
    if data_type:
        argv.extend(["-t", data_type])
    argv.extend([index, subindex])
    result = run_command(argv, timeout_s)
    result.update({"argv": argv, "index": index, "subindex": subindex, "data_type": data_type, "value": parse_upload_value(result.get("stdout", ""), data_type)})
    return result


def read_command(position: str, command_name: str, command: Dict[str, Any], required: bool) -> Dict[str, Any]:
    if not isinstance(command, dict) or command.get("supported") is False:
        return {"ok": not required, "supported": False, "required": required, "code": "DRIVE_PROFILE_COMMAND_UNSUPPORTED"}
    if str(command.get("access") or "").strip().lower() not in {"read", "ro", "rw", ""}:
        return {"ok": not required, "supported": False, "required": required, "code": "DRIVE_PROFILE_COMMAND_NOT_READABLE"}
    timeout_s = command_timeout(command, 0.8)
    data_type = scalar_data_type(command)
    if command_name == "drive.read_egear" or str(command.get("data_type") or "").endswith("_pair"):
        pair = command.get("objects") if isinstance(command.get("objects"), dict) else command.get("verify_object")
        if not isinstance(pair, dict):
            raise DriveActionError("DRIVE_PROFILE_COMMAND_OBJECT_MISSING", "驱动 profile 缺少电子齿轮读回对象。", {"standard_command": command_name})
        num = ethercat_upload(position, pair.get("numerator"), data_type, timeout_s)
        den = ethercat_upload(position, pair.get("denominator"), data_type, timeout_s)
        ok = bool(num.get("ok") and den.get("ok") and (num.get("value") or 0) > 0 and (den.get("value") or 0) > 0)
        return {"ok": ok, "supported": True, "required": required, "code": "OK" if ok else "DRIVE_READ_EGEAR_FAILED", "numerator": num, "denominator": den}
    upload = ethercat_upload(position, command.get("object"), data_type, timeout_s)
    return {"ok": bool(upload.get("ok")), "supported": True, "required": required, "code": "OK" if upload.get("ok") else "DRIVE_READ_SDO_FAILED", "upload": upload}


def read_drive(timeout_s: float) -> Dict[str, Any]:
    scan = run_ethercat_slaves(timeout_s)
    if not scan.get("ok"):
        scan.update({"read_attempted": False})
        return scan
    if not scan.get("slaves"):
        return {"ok": False, "code": "DRIVE_READ_NO_SLAVES", "message_cn": "未扫描到 EtherCAT 从站，读取驱动未执行。", "slaves": [], "read_attempted": False, "write_executed": False, "motion_executed": False}
    try:
        snapshot = read_resident_snapshot()
    except DriveActionError as exc:
        return {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail, "read_attempted": False, "write_executed": False, "motion_executed": False}
    targets: List[Dict[str, Any]] = []
    failures: List[Dict[str, Any]] = []
    for slave in scan.get("slaves", []):
        position = str(slave.get("position") or "")
        try:
            identity = parse_slave_identity(position, min(timeout_s, 3.0))
            if not identity.get("identity_ok"):
                raise DriveActionError("DRIVE_SLAVE_IDENTITY_MISSING", "真实从站身份读取不完整，未选择 profile。", identity)
            profile = select_profile(identity, snapshot)
            commands = profile.get("commands", {}) if isinstance(profile.get("commands"), dict) else {}
            reads: Dict[str, Any] = {}
            target_ok = True
            for name in REQUIRED_READ_COMMANDS:
                item = read_command(position, name, commands.get(name, {}), True)
                reads[name] = item
                target_ok = target_ok and bool(item.get("ok"))
            for name in OPTIONAL_READ_COMMANDS:
                command = commands.get(name)
                if isinstance(command, dict) and command.get("supported") is not False:
                    reads[name] = read_command(position, name, command, False)
            targets.append({"ok": target_ok, "position": position, "identity": identity, "profile_id": profile.get("profile_id", ""), "selected_map_path": profile.get("profile_map_path", ""), "selected_map_sha256": profile.get("profile_map_sha256", ""), "map_source": profile.get("map_source", ""), "reads": reads})
            if not target_ok:
                failures.append({"position": position, "code": "DRIVE_READ_PARTIAL"})
        except DriveActionError as exc:
            failure = {"position": position, "code": exc.code, "message_cn": exc.message_cn, "detail": exc.detail}
            targets.append({"ok": False, **failure})
            failures.append(failure)
    ok = bool(targets) and not failures
    return {"ok": ok, "code": "DRIVE_READ_OK" if ok else "DRIVE_READ_PARTIAL", "message_cn": "读取驱动完成。" if ok else "读取驱动未完整闭合。", "targets": targets, "failures": failures, "snapshot_generated_at": snapshot.get("generated_at"), "snapshot_profile_count": snapshot.get("profile_count"), "read_attempted": True, "write_executed": False, "motion_executed": False}



def finite_float(value: Any) -> float | None:
    try:
        number = float(value)
    except Exception:
        return None
    return number if math.isfinite(number) else None


def axis_unit(axis: str) -> str:
    return "deg" if str(axis or "").upper() in {"A", "B", "C"} else "mm"


def read_runtime_ini_sections(path: Path) -> Dict[str, Dict[str, str]]:
    cache_key = str(path)
    if cache_key in _RUNTIME_INI_SECTIONS_CACHE:
        return _RUNTIME_INI_SECTIONS_CACHE[cache_key]
    if not _RESIDENT_PRELOAD_ACTIVE:
        raise DriveActionError("RUNTIME_INI_RESIDENT_NOT_PRELOADED", "active runtime INI 未在启动阶段载入内存，动作热路径拒绝读盘。", str(path))
    sections: Dict[str, Dict[str, str]] = {}
    if not path.is_file():
        _RUNTIME_INI_SECTIONS_CACHE[cache_key] = sections
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
    _RUNTIME_INI_SECTIONS_CACHE[cache_key] = sections
    return sections


def runtime_ini_value(sections: Dict[str, Dict[str, str]], names: List[str], keys: List[str]) -> float | None:
    for name in names:
        table = sections.get(str(name).upper(), {})
        for key in keys:
            value = finite_float(table.get(str(key).upper()))
            if value is not None:
                return value
    return None


def load_settings_runtime() -> Dict[str, Any]:
    global _SETTINGS_RUNTIME_CACHE
    if _SETTINGS_RUNTIME_CACHE is not None:
        return _SETTINGS_RUNTIME_CACHE
    if not _RESIDENT_PRELOAD_ACTIVE:
        raise DriveActionError("SETTINGS_RUNTIME_RESIDENT_NOT_PRELOADED", "settings_runtime drive-only resident owner 未在启动阶段载入内存，动作热路径拒绝读盘。", str(SETTINGS_RUNTIME_JSON))
    if not SETTINGS_RUNTIME_JSON.is_file():
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_MISSING", "settings_runtime resident owner 未加载，不能校验设0。", str(SETTINGS_RUNTIME_JSON))
    try:
        payload = json.loads(SETTINGS_RUNTIME_JSON.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_INVALID", "settings_runtime resident owner 损坏，不能校验设0。", "%s: %s" % (type(exc).__name__, exc))
    validate_settings_runtime_drive_only(payload)
    axes = payload.get("axes") if isinstance(payload, dict) else None
    if not isinstance(axes, list):
        raise DriveActionError("SETTINGS_AXIS_ZERO_AXES_MISSING", "settings_runtime resident owner 缺少 axes，不能校验设0。", payload)
    _SETTINGS_RUNTIME_CACHE = payload
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


def derive_counts_per_unit(axis: str, axis_cfg: Dict[str, Any], axis_index: int) -> Tuple[float, Dict[str, Any]]:
    unit = axis_unit(axis)
    sections = read_runtime_ini_sections(RUNTIME_SETTINGS_INI)
    joint_name = "JOINT_%d" % axis_index
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
        "runtime_ini": str(RUNTIME_SETTINGS_INI),
        "joint_section": joint_name,
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
        "settings_runtime_json": str(SETTINGS_RUNTIME_JSON),
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


def write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def update_runtime_ini_raw_limits(axis: str, axis_index: int, old_zero_physical: float, new_zero_physical: float) -> Dict[str, Any]:
    if not RUNTIME_SETTINGS_INI.is_file():
        raise DriveActionError("SETTINGS_AXIS_ZERO_RUNTIME_INI_MISSING", "runtime INI 不存在，不能同次写入 raw limit。", str(RUNTIME_SETTINGS_INI))
    original = RUNTIME_SETTINGS_INI.read_text(encoding="utf-8", errors="ignore")
    lines = original.splitlines()
    section = ""
    axis_section = "AXIS_%s" % str(axis or "").upper()
    joint_section = "JOINT_%d" % axis_index
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
    if old_zero_physical < raw_min_current or old_zero_physical > raw_max_current:
        raise DriveActionError("SETTINGS_AXIS_ZERO_OLD_ZERO_OUTSIDE_RAW_LIMIT", "旧零位证据落在当前 raw 限位区间外，不能继续滚动重算限位。", {"old_zero_physical": old_zero_physical, "raw_min_limit": raw_min_current, "raw_max_limit": raw_max_current, "runtime_ini": str(RUNTIME_SETTINGS_INI)})
    min_distance = raw_min_current - old_zero_physical
    max_distance = raw_max_current - old_zero_physical
    new_min = new_zero_physical + min_distance
    new_max = new_zero_physical + max_distance
    if not (math.isfinite(new_min) and math.isfinite(new_max) and new_min < new_max):
        raise DriveActionError("SETTINGS_AXIS_ZERO_RAW_LIMIT_INVALID", "按新零位重算 raw limit 后区间非法，未写入。", {"new_min": new_min, "new_max": new_max})
    out = []
    section = ""
    touched: Dict[str, Dict[str, bool]] = {axis_section: {"MIN_LIMIT": False, "MAX_LIMIT": False}, joint_section: {"MIN_LIMIT": False, "MAX_LIMIT": False}}
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
        out.append(raw)
    if not any(v["MIN_LIMIT"] and v["MAX_LIMIT"] for v in touched.values()):
        raise DriveActionError("SETTINGS_AXIS_ZERO_RAW_LIMIT_WRITE_MISSING", "runtime INI raw limit 未找到可写字段。", touched)
    write_text_atomic(RUNTIME_SETTINGS_INI, "\n".join(out) + "\n")
    _RUNTIME_INI_SECTIONS_CACHE.pop(str(RUNTIME_SETTINGS_INI), None)
    global _RESIDENT_PRELOAD_ACTIVE
    old_preload = _RESIDENT_PRELOAD_ACTIVE
    _RESIDENT_PRELOAD_ACTIVE = True
    try:
        read_runtime_ini_sections(RUNTIME_SETTINGS_INI)
    finally:
        _RESIDENT_PRELOAD_ACTIVE = old_preload
    return {
        "runtime_ini": str(RUNTIME_SETTINGS_INI),
        "axis_section": axis_section,
        "joint_section": joint_section,
        "old_zero_physical": old_zero_physical,
        "new_zero_physical": new_zero_physical,
        "ui_min_limit_distance": min_distance,
        "ui_max_limit_distance": max_distance,
        "raw_min_limit": new_min,
        "raw_max_limit": new_max,
        "updated_sections": [name for name, item in touched.items() if item["MIN_LIMIT"] and item["MAX_LIMIT"]],
    }


def persist_axis_zero_model(runtime: Dict[str, Any],
                            axis: str,
                            axis_index: int,
                            current_counts: float,
                            counts_per_unit: float,
                            scale_evidence: Dict[str, Any],
                            read_evidence: Dict[str, Any]) -> Dict[str, Any]:
    global _SETTINGS_RUNTIME_CACHE
    axis_cfg, _ = find_runtime_axis(runtime, axis)
    zero_model = axis_cfg.get("zero_model") if isinstance(axis_cfg.get("zero_model"), dict) else {}
    old_counts = saved_zero_counts(axis_cfg)
    old_zero_physical = finite_float(zero_model.get("raw_zero_position"))
    if old_zero_physical is None:
        old_zero_physical = old_counts / counts_per_unit
    new_zero_physical = current_counts / counts_per_unit
    raw_limit_save = update_runtime_ini_raw_limits(axis, axis_index, old_zero_physical, new_zero_physical)
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
        "apply_state": "count_domain_zero_saved_pending_final_restart",
        "captured_at": now_utc(),
        "position_count_source": "drive.read_actual_position",
        "actual_counts": current_counts,
        "zero_counts": current_counts,
        "zero_anchor_counts": current_counts,
        "raw_zero_position": new_zero_physical,
        "raw_zero_formula": "zero_counts / active_runtime_ini.SCALE",
        "counts_per_unit": counts_per_unit,
        "unit": axis_unit(axis),
        "scale_chain": drive_only_scale_evidence(scale_evidence),
        "drive_position": drive_position,
    })
    axis_cfg["zero_model"] = new_zero_model
    axis_cfg["zero_model_writeback"] = {
        "ok": True,
        "code": "SETTINGS_AXIS_ZERO_MODEL_SAVED",
        "written_at": now_utc(),
        "settings_runtime_json": str(SETTINGS_RUNTIME_JSON),
    }
    runtime_for_write = sanitize_settings_runtime_drive_only(runtime)
    write_json(SETTINGS_RUNTIME_JSON, runtime_for_write)
    runtime.clear()
    runtime.update(runtime_for_write)
    _SETTINGS_RUNTIME_CACHE = runtime_for_write
    reread_axis, _ = find_runtime_axis(runtime_for_write, axis)
    reread_counts = saved_zero_counts(reread_axis)
    if abs(reread_counts - current_counts) > 0.5:
        raise DriveActionError("SETTINGS_AXIS_ZERO_OWNER_READBACK_MISMATCH", "resident owner 零位写入后内存回读不一致。", {"written_counts": current_counts, "readback_counts": reread_counts})
    return {
        "settings_runtime_json": str(SETTINGS_RUNTIME_JSON),
        "written_counts": current_counts,
        "readback_counts": reread_counts,
        "old_zero_counts": old_counts,
        "old_zero_physical": old_zero_physical,
        "new_zero_physical": new_zero_physical,
        "raw_limit_save": raw_limit_save,
    }

def unsupported(action: str) -> Dict[str, Any]:
    codes = {
        "factory-reset": ("DRIVE_RESET_PRECHECK_FAILED", "复位驱动 canonical 写入/读回 gate 尚未接入；已 fail-closed，未写驱动。"),
        "fault-reset": ("DRIVE_FAULT_RESET_PARTIAL", "清除故障 canonical 写入/读回 gate 尚未接入；已 fail-closed，未写驱动。"),
        "set-drive": ("DRIVE_SET_PARTIAL", "设置驱动 canonical 写入/读回 gate 尚未接入；已 fail-closed，未写驱动。"),
    }
    code, message = codes.get(action, ("DRIVE_ACTION_CANONICAL_WRITE_GATE_MISSING", "该驱动写动作的 canonical 写入和读回 gate 尚未接入；已 fail-closed，未写驱动。"))
    return {
        "ok": False,
        "code": code,
        "message_cn": message,
        "action": action,
        "write_executed": False,
        "motion_executed": False,
        "failed_stage": "canonical_write_gate",
    }


def preload_resident_state() -> Dict[str, Any]:
    global _RESIDENT_PRELOAD_ACTIVE
    status: Dict[str, Any] = {"schema": "v5.drive_bus_action.resident_preload.v1"}
    old_preload = _RESIDENT_PRELOAD_ACTIVE
    _RESIDENT_PRELOAD_ACTIVE = True
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
            sections = read_runtime_ini_sections(RUNTIME_SETTINGS_INI)
            status["runtime_ini_loaded"] = bool(sections)
            status["runtime_ini_section_count"] = len(sections)
        except DriveActionError as exc:
            status["runtime_ini_loaded"] = False
            status["runtime_ini_error"] = exc.code
    finally:
        _RESIDENT_PRELOAD_ACTIVE = old_preload
    ok = bool(status.get("settings_runtime_loaded") and status.get("drive_snapshot_loaded") and status.get("runtime_ini_loaded"))
    status["ok"] = ok
    status["preload_complete"] = ok
    if not ok:
        status["code"] = "DRIVE_ACTION_RESIDENT_PRELOAD_INCOMPLETE"
        status["message_cn"] = "驱动动作常驻内存闭包未完整预载，已 fail-closed。"
    return status


def result_path(action: str) -> Path:
    names = {"scan": "drive_scan_result.json", "factory-reset": "drive_factory_reset_result.json", "read": "drive_read_result.json", "fault-reset": "drive_fault_reset_result.json", "set-drive": "drive_set_result.json", "axis-zero": "settings_axis_zero_result.json"}
    return RUN_DIR / names.get(action, "drive_action_result.json")


def run_action(action: str, timeout_s: float = 8.0, write_result_file: bool = True, request: Dict[str, Any] | None = None) -> Dict[str, Any]:
    if action == "scan":
        result = run_ethercat_slaves(timeout_s)
        write_scan_self_parameter_table(result)
        if result.get("ok"):
            result["display_message_cn"] = format_scan_slave_display(result)
    elif action == "read":
        result = read_drive(timeout_s)
    elif action == "axis-zero":
        try:
            result = axis_zero_verify(request or {}, timeout_s)
        except DriveActionError as exc:
            result = {"ok": False, "code": exc.code, "message_cn": exc.message_cn, "display_message_cn": exc.message_cn, "detail": exc.detail, "write_executed": False, "motion_executed": False}
    elif action in {"factory-reset", "fault-reset", "set-drive"}:
        result = unsupported(action)
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


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 drive bus settings actions")
    parser.add_argument("--action", required=True, choices=("scan", "factory-reset", "read", "fault-reset", "set-drive", "axis-zero"))
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()
    preload_result = preload_resident_state()
    if not preload_result.get("ok"):
        print(json.dumps(preload_result, ensure_ascii=False, indent=2))
        return 1
    result = run_action(args.action, args.timeout, True, {"axis": os.environ.get("V5_AXIS_ZERO_AXIS", "X"), "slave_index": os.environ.get("V5_AXIS_ZERO_SLAVE", ""), "driver_mode": "bus", "target_scope": "bus_count_domain_zero", "apply_mode": "count_domain_zero"})
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
