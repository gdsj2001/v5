from __future__ import annotations

import json
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional

from v5_settings_action_contract import (
    DRIVE_ACTION_TIMEOUTS,
    DRIVE_TIMEOUT_S,
    EVENT_LOG,
    MAX_ACTIOND_EVENT_BYTES,
    MAX_ACTIOND_RESULT_BYTES,
    RUN_DIR,
)
from v5_settings_restart import run_restart_handoff

AUTH_MODULE_DIR = Path(__file__).resolve().parent.parent / "auth_download"
if AUTH_MODULE_DIR.is_dir() and str(AUTH_MODULE_DIR) not in sys.path:
    sys.path.insert(0, str(AUTH_MODULE_DIR))

import v5_drive_bus_action
from v5_drive_result import compact_action_result_payload
import v5_device_authorization_download
import v5_device_dna_register
import v5_drive_profile_download
import v5_drive_profile_resident_snapshot

ACTIONS: Dict[str, Dict[str, Any]] = {
    "device_dna_register": {"owner": "auth_download", "result_path": "/run/8ax_v5_auth_download/device_dna_register_result.json", "handler": "auth", "auth_action": "device_dna_register"},
    "device_authorization_download": {"owner": "auth_download", "result_path": "/run/8ax_v5_auth_download/device_authorization_download_result.json", "handler": "auth", "auth_action": "device_authorization_download"},
    "drive_profile_server_download": {"owner": "auth_download", "result_path": "/run/8ax_v5_auth_download/drive_profile_server_download_result.json", "handler": "auth", "auth_action": "drive_profile_server_download"},
    "drive_scan_slaves": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("scan")), "handler": "drive", "drive_action": "scan"},
    "drive_factory_reset": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("factory-reset")), "handler": "drive", "drive_action": "factory-reset"},
    "drive_parameter_read": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("read")), "handler": "drive", "drive_action": "read"},
    "drive_fault_reset": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("fault-reset")), "handler": "drive", "drive_action": "fault-reset"},
    "drive_set_parameters": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("set-drive")), "handler": "drive", "drive_action": "set-drive"},
    "settings_axis_zero": {"owner": "drive_profile", "result_path": str(v5_drive_bus_action.result_path("axis-zero")), "handler": "drive", "drive_action": "axis-zero"},
    "settings_save_and_restart": {"owner": "settings_restart", "result_path": "/run/8ax_v5_product_ui/settings_save_restart_result.json", "handler": "restart"},
}

status_lock = threading.Lock()
last_status: Dict[str, Any] = {
    "schema": "v5.settings_actiond_status.v1",
    "available": False,
    "busy": False,
    "ok": False,
    "action": "",
    "run_id": "",
    "owner": "",
    "code": "",
    "message_cn": "待命",
    "vpsDistributionId": "",
    "result_path": "",
    "axis": "",
    "settings_mcs_position": 0.0,
    "settings_mcs_position_valid": False,
    "restart_required": False,
    "restart_deferred": False,
    "backend_restart_required": False,
    "state": "idle",
    "cancel_allowed": False,
}

def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def bounded_json_text(payload: Dict[str, Any], max_bytes: int, overflow_code: str) -> str:
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    if len(text.encode("utf-8")) <= max_bytes:
        return text + "\n"
    overflow = {
        "schema": str(payload.get("schema") or "v5.settings_action_result.v1"),
        "generated_at": now_utc(),
        "ok": False,
        "code": overflow_code,
        "message_cn": "常驻动作结果超过内存预算，已 fail-closed。",
        "action": str(payload.get("action") or ""),
        "owner": str(payload.get("owner") or ""),
        "write_executed": False,
        "motion_executed": False,
        "result_bytes": len(text.encode("utf-8")),
        "max_result_bytes": max_bytes,
    }
    return json.dumps(overflow, ensure_ascii=False, indent=2) + "\n"


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(bounded_json_text(payload, MAX_ACTIOND_RESULT_BYTES, "SETTINGS_ACTION_RESULT_BUDGET_EXCEEDED"), encoding="utf-8")
    tmp.replace(path)


def append_event(payload: Dict[str, Any]) -> None:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    payload.setdefault("schema", "v5.settings_actiond_event.v1")
    payload.setdefault("generated_at", now_utc())
    with EVENT_LOG.open("a", encoding="utf-8") as fp:
        text = json.dumps(payload, ensure_ascii=False, sort_keys=True)
        if len(text.encode("utf-8")) > MAX_ACTIOND_EVENT_BYTES:
            payload = {
                "schema": "v5.settings_actiond_event.v1",
                "generated_at": now_utc(),
                "event": str(payload.get("event") or "oversize_event"),
                "ok": False,
                "code": "SETTINGS_ACTION_EVENT_BUDGET_EXCEEDED",
                "event_bytes": len(text.encode("utf-8")),
                "max_event_bytes": MAX_ACTIOND_EVENT_BYTES,
            }
            text = json.dumps(payload, ensure_ascii=False, sort_keys=True)
        fp.write(text + "\n")


def set_last_status(
        action: str,
        run_id: str,
        spec: Dict[str, Any],
        busy: bool,
        result: Optional[Dict[str, Any]] = None,
        axis_hint: str = "",
        state: str = "",
        cancel_allowed: bool = False) -> None:
    payload = result or {}
    message = str(payload.get("display_message_cn") or payload.get("message_cn") or ("执行中" if busy else ""))
    axis = str(payload.get("axis") or axis_hint or "")
    vps_distribution_id = str(payload.get("vpsDistributionId") or "")
    if action != "device_dna_register" or len(vps_distribution_id) != 6 or not vps_distribution_id.isdigit():
        vps_distribution_id = ""
    with status_lock:
        last_status.update({
            "schema": "v5.settings_actiond_status.v1",
            "available": True,
            "busy": bool(busy),
            "ok": bool(payload.get("ok")) if result is not None else False,
            "action": action,
            "run_id": run_id,
            "owner": spec.get("owner", ""),
            "code": str(payload.get("code") or ("RUNNING" if busy else "")),
            "message_cn": message,
            "vpsDistributionId": vps_distribution_id,
            "result_path": str(spec.get("result_path", "")),
            "axis": axis,
            "settings_mcs_position": float(payload.get("settings_mcs_position") or 0.0),
            "settings_mcs_position_valid": bool(payload.get("settings_mcs_position_valid")),
            "restart_required": bool(payload.get("restart_required")),
            "restart_deferred": bool(payload.get("restart_deferred")),
            "backend_restart_required": bool(payload.get("backend_restart_required")),
            "state": state or ("running" if busy else "finished"),
            "cancel_allowed": bool(cancel_allowed and busy),
            "generated_at": now_utc(),
        })


def get_last_status() -> Dict[str, Any]:
    with status_lock:
        return dict(last_status)


def refresh_drive_profile_resident_snapshot() -> Dict[str, Any]:
    try:
        snapshot = v5_drive_profile_resident_snapshot.build_snapshot(v5_drive_profile_resident_snapshot.RUNTIME_PROFILE_ROOT)
        v5_drive_profile_resident_snapshot.atomic_write_json(v5_drive_profile_resident_snapshot.DEFAULT_OUT, snapshot)
        return v5_drive_bus_action.replace_resident_snapshot(snapshot)
    except Exception as exc:
        return {
            "ok": False,
            "code": "DRIVE_PROFILE_RESIDENT_REFRESH_EXCEPTION",
            "detail": "%s: %s" % (type(exc).__name__, exc),
            "out": str(v5_drive_profile_resident_snapshot.DEFAULT_OUT),
        }

def commit_drive_profile_resident_snapshot_in_parent(
        worker_result: Dict[str, Any]) -> Dict[str, Any]:
    """Commit the worker-produced snapshot into actiond's long-lived cache."""
    try:
        snapshot_path = v5_drive_profile_resident_snapshot.DEFAULT_OUT
        snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
        if not isinstance(snapshot, dict):
            raise ValueError("snapshot_not_object")
        expected = worker_result.get("resident_snapshot_refresh")
        if not isinstance(expected, dict) or not expected.get("ok"):
            raise ValueError("worker_refresh_identity_missing")
        expected_generation = str(expected.get("generated_at") or "")
        actual_generation = str(snapshot.get("generated_at") or "")
        expected_profiles = int(expected.get("profile_count") or 0)
        profiles = snapshot.get("profiles")
        actual_profiles = len(profiles) if isinstance(profiles, list) else 0
        if (not expected_generation or actual_generation != expected_generation or
                actual_profiles != expected_profiles):
            raise ValueError(
                "worker_refresh_identity_mismatch expected=%s/%d actual=%s/%d" % (
                    expected_generation, expected_profiles,
                    actual_generation, actual_profiles))
        committed = v5_drive_bus_action.replace_resident_snapshot(snapshot)
        committed["parent_cache_committed"] = True
        committed["snapshot_path"] = str(snapshot_path)
        return committed
    except Exception as exc:
        return {
            "ok": False,
            "code": "DRIVE_PROFILE_PARENT_RESIDENT_COMMIT_FAILED",
            "detail": "%s: %s" % (type(exc).__name__, exc),
            "snapshot_path": str(v5_drive_profile_resident_snapshot.DEFAULT_OUT),
        }


def reconcile_worker_result_in_parent(
        action: str, spec: Dict[str, Any], worker_result: Dict[str, Any]) -> Dict[str, Any]:
    result = dict(worker_result)
    if action != "drive_profile_server_download" or not result.get("ok"):
        return result
    parent_commit = commit_drive_profile_resident_snapshot_in_parent(result)
    result["parent_resident_snapshot_commit"] = parent_commit
    if not parent_commit.get("ok"):
        result["download_ok_before_parent_resident_commit"] = True
        result["ok"] = False
        result["code"] = "SERVER_DOWNLOAD_PARENT_RESIDENT_COMMIT_FAILED"
        result["message_cn"] = "服务器下载已完成，但设置动作常驻 profile 快照提交失败，后续驱动动作已 fail-closed。"
        result["display_message_cn"] = result["message_cn"]
    write_json(Path(str(spec.get("result_path", RUN_DIR / "settings_action_result.json"))), result)
    return result


def run_auth_action(action: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    auth_action = str(spec.get("auth_action", ""))
    request: Dict[str, Any] = {"action": action}
    if auth_action == "device_dna_register":
        result = v5_device_dna_register.run_action(request)
    elif auth_action == "device_authorization_download":
        result = v5_device_authorization_download.run_action(request)
    elif auth_action == "drive_profile_server_download":
        result = v5_drive_profile_download.run_action(request)
        if isinstance(result, dict) and result.get("ok"):
            refresh = refresh_drive_profile_resident_snapshot()
            result["resident_snapshot_refresh"] = refresh
            if not refresh.get("ok"):
                result["download_ok_before_resident_refresh"] = True
                result["ok"] = False
                result["code"] = "SERVER_DOWNLOAD_RESIDENT_REFRESH_FAILED"
                result["message_cn"] = "服务器下载已完成，但驱动 profile resident 快照刷新失败，后续驱动动作已 fail-closed。"
                result["display_message_cn"] = result["message_cn"]
    else:
        result = {
            "ok": False,
            "code": "SETTINGS_AUTH_ACTION_UNKNOWN",
            "message_cn": "未知授权/下载设置动作，未执行。",
        }
    result = dict(result) if isinstance(result, dict) else {"ok": False, "code": "SETTINGS_AUTH_ACTION_BAD_RESULT", "message_cn": "授权/下载动作返回格式错误。"}
    result.setdefault("schema", "v5.settings_action_result.v1")
    result.setdefault("generated_at", now_utc())
    result.setdefault("action", action)
    result.setdefault("owner", spec.get("owner", ""))
    result.setdefault("write_executed", False)
    result.setdefault("motion_executed", False)
    return result


def drive_timeout_for_action(drive_action: str) -> float:
    return DRIVE_ACTION_TIMEOUTS.get(str(drive_action or ""), DRIVE_TIMEOUT_S)


def drive_sync_evidence(result: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "ok": bool(result.get("ok")),
        "code": str(result.get("code") or ""),
        "message_cn": str(result.get("display_message_cn") or result.get("message_cn") or ""),
        "write_executed": bool(result.get("write_executed")),
        "motion_executed": bool(result.get("motion_executed")),
    }


def run_drive_action_from_current_owners(
        drive_action: str,
        timeout_s: float,
        request: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    reload_result = v5_drive_bus_action.preload_resident_state()
    reload_evidence = {
        "ok": bool(reload_result.get("ok")),
        "settings_runtime_loaded": bool(reload_result.get("settings_runtime_loaded")),
        "runtime_ini_loaded": bool(reload_result.get("runtime_ini_loaded")),
        "self_slave_bindings_loaded": bool(reload_result.get("self_slave_bindings_loaded")),
        "drive_snapshot_loaded": bool(reload_result.get("drive_snapshot_loaded")),
    }
    if not reload_evidence["ok"]:
        result = {
            "schema": "v5.drive_bus_action.v1",
            "generated_at": now_utc(),
            "action": drive_action,
            "ok": False,
            "code": "DRIVE_ACTION_OWNER_RELOAD_FAILED",
            "message_cn": "当前运动模型、逻辑轴从站映射或驱动参数未能同代重载，未写驱动并保持 Machine Off。",
            "write_executed": False,
            "drive_write_executed": False,
            "motion_executed": False,
            "owner_reload": reload_evidence,
        }
        v5_drive_bus_action.write_json(v5_drive_bus_action.result_path(drive_action), result)
        return result
    result = v5_drive_bus_action.run_action(
        drive_action, timeout_s, False, request or {"action": drive_action})
    result["owner_reload"] = reload_evidence
    v5_drive_bus_action.write_json(v5_drive_bus_action.result_path(drive_action), result)
    return result


def run_save_restart_with_drive_sync(
        action: str,
        spec: Dict[str, Any],
        request: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    drive_request = dict(request or {})
    drive_request["action"] = "drive_set_parameters"
    drive_request["trigger"] = "settings_save_and_restart"
    drive_result = run_drive_action_from_current_owners(
        "set-drive",
        drive_timeout_for_action("set-drive"),
        drive_request,
    )
    drive_evidence = drive_sync_evidence(drive_result)
    if not drive_evidence["ok"]:
        return {
            "schema": "v5.settings_action_result.v1",
            "generated_at": now_utc(),
            "action": action,
            "owner": spec.get("owner", ""),
            "ok": False,
            "code": "SETTINGS_SAVE_RESTART_DRIVE_SYNC_FAILED",
            "message_cn": "保存并重启前设置驱动未闭合，电子齿轮或必需驱动参数 fresh readback 未一致，已保持不重启。",
            "display_message_cn": drive_evidence["message_cn"] or "设置驱动失败，已保持不重启。",
            "write_executed": drive_evidence["write_executed"],
            "motion_executed": drive_evidence["motion_executed"],
            "restart_executed": False,
            "restart_commit_required": False,
            "drive_sync": drive_evidence,
        }
    result = run_restart_handoff(action, spec)
    result["drive_sync"] = drive_evidence
    if result.get("ok"):
        result["message_cn"] = "设置驱动及电子齿轮 fresh readback 已一致，系统级重启已准备，等待关闭结果窗后提交。"
        result["display_message_cn"] = "设置驱动校验完成，点击关闭后黑屏并重启。"
    return result


def execute_action(action: str, request: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    spec = ACTIONS.get(action)
    if not spec:
        return {
            "schema": "v5.settings_action_result.v1",
            "generated_at": now_utc(),
            "action": action,
            "ok": False,
            "code": "UNKNOWN_SETTINGS_ACTION",
            "message_cn": "未知设置动作，未执行。",
        }
    result_path = Path(str(spec.get("result_path", RUN_DIR / "settings_action_result.json")))
    axis_hint = str((request or {}).get("axis") or "")
    try:
        if spec.get("handler") == "drive":
            drive_action = str(spec.get("drive_action", ""))
            result = run_drive_action_from_current_owners(
                drive_action,
                drive_timeout_for_action(drive_action),
                request or {"action": action},
            )
            if action == "settings_axis_zero" and bool(result.get("ok")):
                write_json(result_path, result)
        elif spec.get("handler") == "auth":
            result = run_auth_action(action, spec)
            write_json(result_path, result)
        elif spec.get("handler") == "restart":
            result = run_save_restart_with_drive_sync(action, spec, request)
            write_json(result_path, result)
        else:
            result = {
                "schema": "v5.settings_action_result.v1",
                "generated_at": now_utc(),
                "action": action,
                "owner": spec.get("owner", ""),
                "ok": False,
                "code": "SETTINGS_ACTION_HANDLER_UNKNOWN",
                "message_cn": "未知设置动作 handler，未执行。",
                "write_executed": False,
                "motion_executed": False,
            }
            write_json(result_path, result)
    except Exception as exc:
        result = {
            "schema": "v5.settings_action_result.v1",
            "generated_at": now_utc(),
            "action": action,
            "owner": spec.get("owner", "") if spec else "",
            "ok": False,
            "code": "SETTINGS_ACTIOND_EXCEPTION",
            "message_cn": "常驻设置动作执行异常；未写驱动、未运动。",
            "detail": "%s: %s" % (type(exc).__name__, exc),
            "write_executed": False,
            "motion_executed": False,
            "axis": axis_hint,
        }
        write_json(result_path, result)
    if spec.get("handler") == "drive" and isinstance(result, dict):
        result = compact_action_result_payload(result)
    return result
