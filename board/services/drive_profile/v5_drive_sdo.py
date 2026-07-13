from __future__ import annotations

import json
import re
import subprocess
import time
from typing import Any, Dict, List, Tuple

import v5_drive_bus_context as context
import v5_drive_bus_contract as contract
from v5_drive_bus_contract import DriveActionError, MAX_COMMAND_OUTPUT_BYTES, TYPE_MAP, finite_float
from v5_drive_result import compact_sdo_io

ETHERCAT_SLAVE_STATES = frozenset({"INIT", "PREOP", "BOOT", "SAFEOP", "OP"})

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


def parse_ethercat_slave_line(line: str) -> Dict[str, str]:
    text = str(line or "").strip()
    parts = text.split()
    if len(parts) < 4:
        raise ValueError("ethercat slave row has fewer than four fields")
    try:
        position = str(int(parts[0]))
    except (TypeError, ValueError) as exc:
        raise ValueError("ethercat slave position is invalid") from exc
    state = parts[2].upper()
    if state not in ETHERCAT_SLAVE_STATES:
        raise ValueError("ethercat slave state is invalid")
    name_start = 4 if parts[3] in {"+", "E", "-"} else 3
    name = " ".join(parts[name_start:]).strip()
    return {"line": text, "position": position, "state": state, "name": name}


def run_ethercat_slaves(timeout_s: float) -> Dict[str, Any]:
    result = run_command(["ethercat", "slaves"], timeout_s)
    if result["code"] == "ETHERCAT_TOOL_MISSING":
        return {"ok": False, "code": "ETHERCAT_TOOL_MISSING", "message_cn": "板端缺少 ethercat 工具，不能扫描从站。", "slaves": []}
    if result["code"] == "ETHERCAT_COMMAND_TIMEOUT":
        return {"ok": False, "code": "ETHERCAT_SCAN_TIMEOUT", "message_cn": "扫描从站超时。", "slaves": []}
    slaves: List[Dict[str, Any]] = []
    parse_failures: List[Dict[str, str]] = []
    for line in result["stdout"].splitlines():
        text = line.strip()
        if not text:
            continue
        try:
            slaves.append(parse_ethercat_slave_line(text))
        except ValueError as exc:
            parse_failures.append({"line": text, "error": str(exc)})
    if result["ok"] and parse_failures:
        return {"ok": False, "code": "ETHERCAT_SCAN_STATE_INVALID", "message_cn": "EtherCAT 从站状态解析失败，拒绝按不完整扫描结果发送命令。", "returncode": result["returncode"], "stdout": result["stdout"], "stderr": result["stderr"], "slaves": slaves, "parse_failures": parse_failures}
    if result["ok"] and not slaves:
        return {"ok": False, "code": "DRIVE_SCAN_NO_SLAVES", "message_cn": "未扫描到 EtherCAT 从站。", "returncode": result["returncode"], "stdout": result["stdout"], "stderr": result["stderr"], "slaves": []}
    return {"ok": bool(result["ok"]), "code": "DRIVE_SCAN_OK" if result["ok"] else "DRIVE_SCAN_FAILED", "message_cn": "扫描从站完成。" if result["ok"] else "扫描从站失败。", "returncode": result["returncode"], "stdout": result["stdout"], "stderr": result["stderr"], "slaves": slaves}


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
    if context.resident_snapshot_cache is not None:
        return context.resident_snapshot_cache
    if not context.resident_preload_active:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_NOT_PRELOADED", "驱动 profile resident snapshot 未在启动阶段载入内存，动作热路径拒绝读盘。", str(contract.RESIDENT_SNAPSHOT))
    if not contract.RESIDENT_SNAPSHOT.is_file():
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_MISSING", "驱动 profile 运行内存快照不存在，读取驱动未执行。", str(contract.RESIDENT_SNAPSHOT))
    try:
        snapshot = json.loads(contract.RESIDENT_SNAPSHOT.read_text(encoding="utf-8"))
    except Exception as exc:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_INVALID", "驱动 profile 运行内存快照损坏，读取驱动未执行。", "%s: %s" % (type(exc).__name__, exc))
    profiles = snapshot.get("profiles") if isinstance(snapshot, dict) else None
    if not isinstance(profiles, list) or not profiles:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_EMPTY", "驱动 profile 运行内存快照为空，读取驱动未执行。", snapshot)
    context.resident_snapshot_cache = snapshot
    return snapshot


def replace_resident_snapshot(snapshot: Dict[str, Any]) -> Dict[str, Any]:
    profiles = snapshot.get("profiles") if isinstance(snapshot, dict) else None
    if not isinstance(profiles, list) or not profiles:
        raise DriveActionError("DRIVE_PROFILE_RESIDENT_SNAPSHOT_EMPTY", "驱动 profile 运行内存快照为空，读取驱动未执行。", snapshot)
    context.resident_snapshot_cache = snapshot
    maps = snapshot.get("maps") if isinstance(snapshot.get("maps"), list) else []
    return {
        "ok": True,
        "loaded": True,
        "generated_at": snapshot.get("generated_at", ""),
        "profile_count": len(profiles),
        "map_file_count": snapshot.get("map_file_count", 0),
        "maps": [
            {
                "scope": item.get("scope", ""),
                "ok": bool(item.get("ok")),
                "sha256": item.get("sha256", ""),
                "profile_count": item.get("profile_count", 0),
                "map_version": item.get("map_version", ""),
            }
            for item in maps if isinstance(item, dict)
        ],
    }


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


def ethercat_download(position: str, obj: Any, data_type: str, value: Any, timeout_s: float) -> Dict[str, Any]:
    index, subindex = sdo_object_tokens(obj)
    if not index or not subindex:
        raise DriveActionError("DRIVE_PROFILE_COMMAND_OBJECT_MISSING", "驱动 profile 命令缺少 SDO 写对象，未访问驱动。", obj)
    if value is None:
        raise DriveActionError("DRIVE_PROFILE_WRITE_VALUE_MISSING", "驱动 profile 命令缺少写入值，未访问驱动。", {"index": index, "subindex": subindex})
    argv = ["ethercat", "download", "-p", str(position)]
    if data_type:
        argv.extend(["-t", data_type])
    argv.extend([index, subindex, str(int(value))])
    result = run_command(argv, timeout_s)
    result.update({"argv": argv, "index": index, "subindex": subindex, "data_type": data_type, "write_value": int(value)})
    return result


def command_write_supported(command_name: str, command: Dict[str, Any]) -> None:
    if not isinstance(command, dict) or command.get("supported") is False:
        raise DriveActionError("DRIVE_PROFILE_COMMAND_UNSUPPORTED", "驱动 profile 缺少必需写命令，已 fail-closed。", {"standard_command": command_name})
    if str(command.get("access") or "").strip().lower() not in {"write", "rw"}:
        raise DriveActionError("DRIVE_PROFILE_COMMAND_NOT_WRITABLE", "驱动 profile 必需命令不可写，已 fail-closed。", {"standard_command": command_name, "access": command.get("access")})


def write_command(position: str, command_name: str, command: Dict[str, Any], value: Any = None) -> Dict[str, Any]:
    command_write_supported(command_name, command)
    timeout_s = command_timeout(command, 1.0)
    data_type = scalar_data_type(command)
    operations: List[Dict[str, Any]] = []
    if command_name == "drive.set_egear" or str(command.get("data_type") or "").endswith("_pair"):
        if not isinstance(value, dict):
            raise DriveActionError("DRIVE_EGEAR_TARGET_MISSING", "设置驱动缺少电子齿轮分子/分母，未写驱动。", value)
        numerator = int(value.get("numerator") or 0)
        denominator = int(value.get("denominator") or 0)
        if numerator <= 0 or denominator <= 0:
            raise DriveActionError("DRIVE_EGEAR_TARGET_INVALID", "电子齿轮目标必须为正整数，未写驱动。", value)
        objects = command.get("objects") if isinstance(command.get("objects"), dict) else {}
        ordered = (("numerator", numerator), ("denominator", denominator))
        for key, item_value in ordered:
            op = ethercat_download(position, objects.get(key), data_type, item_value, timeout_s)
            op["component"] = key
            compact_op = compact_sdo_io(op)
            compact_op["component"] = key
            operations.append(compact_op)
            if not op.get("ok"):
                break
        ok = len(operations) == 2 and all(bool(op.get("ok")) for op in operations)
        return {"ok": ok, "code": "OK" if ok else "DRIVE_WRITE_SDO_FAILED", "standard_command": command_name, "operations": operations}

    sequence = command.get("write_sequence")
    if isinstance(sequence, list) and sequence:
        for step in sequence:
            if not isinstance(step, dict):
                continue
            step_value = step.get("value", value if value is not None else command.get("write_value"))
            op = ethercat_download(position, command.get("object"), data_type, step_value, timeout_s)
            operations.append(compact_sdo_io(op))
            delay_ms = finite_float(step.get("delay_ms")) or 0.0
            if delay_ms > 0.0:
                time.sleep(delay_ms / 1000.0)
            if not op.get("ok"):
                break
    else:
        op_value = value if value is not None else command.get("write_value")
        operations.append(compact_sdo_io(ethercat_download(position, command.get("object"), data_type, op_value, timeout_s)))
    ok = bool(operations) and all(bool(op.get("ok")) for op in operations)
    return {"ok": ok, "code": "OK" if ok else "DRIVE_WRITE_SDO_FAILED", "standard_command": command_name, "operations": operations}


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


def parse_slave_position_token(value: Any) -> str:
    text = str(value if value is not None else "").strip()
    if not text:
        return ""
    text = re.split(r"[\s:;,|]+", text, 1)[0].strip()
    if not re.fullmatch(r"\d+", text):
        raise DriveActionError("DRIVE_TARGET_SLAVE_INVALID", "轴绑定的从站号非法，未写驱动。", {"slave_index": value})
    return str(int(text))
