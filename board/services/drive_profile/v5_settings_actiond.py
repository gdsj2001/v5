#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
import ctypes
import resource
from pathlib import Path
from typing import Any, Dict, Optional



def lock_process_memory(process_name: str) -> None:
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_MEMLOCK)
        target = hard if hard != resource.RLIM_INFINITY else resource.RLIM_INFINITY
        if soft != target:
            resource.setrlimit(resource.RLIMIT_MEMLOCK, (target, hard))
    except Exception:
        pass
    libc = ctypes.CDLL("libc.so.6", use_errno=True)
    if libc.mlockall(1 | 2) != 0:
        err = ctypes.get_errno()
        raise SystemExit(f"{process_name} mlockall(MCL_CURRENT|MCL_FUTURE) failed: errno={err}")

AUTH_MODULE_DIR = Path(__file__).resolve().parent.parent / "auth_download"
if AUTH_MODULE_DIR.is_dir() and str(AUTH_MODULE_DIR) not in sys.path:
    sys.path.insert(0, str(AUTH_MODULE_DIR))

import v5_drive_bus_action
import v5_device_authorization_download
import v5_device_dna_register
import v5_drive_profile_download
import v5_drive_profile_resident_snapshot



RUN_DIR = Path("/run/8ax_v5_product_ui")
SOCKET_PATH = RUN_DIR / "settings_actiond.sock"
EVENT_LOG = RUN_DIR / "settings_actiond_events.jsonl"
DRIVE_TIMEOUT_S = 8.0
DRIVE_ACTION_TIMEOUTS: Dict[str, float] = {
    "scan": 20.0,
    "read": 45.0,
    "fault-reset": 45.0,
    "factory-reset": 90.0,
    "set-drive": 180.0,
    "axis-zero": 20.0,
}
MAX_ACTIOND_REQUEST_BYTES = 4096
MAX_ACTIOND_RESULT_BYTES = 65536
MAX_ACTIOND_EVENT_BYTES = 8192
SERVICE_RESTART_TIMEOUTS: Dict[str, float] = {
    "v5-linuxcnc-command-gate": 35.0,
    "v5-rtcp-status-publisher": 12.0,
    "v5-wcs-status-publisher": 20.0,
    "v5-state-publisher": 12.0,
    "v5-ui-relay": 15.0,
}
CANONICAL_CLEAN_RESTART_SERVICES = [
    "v5-ui-relay",
    "v5-touch-diagnostics",
    "v5-settings-actiond",
    "v5-state-publisher",
    "v5-wcs-status-publisher",
    "v5-rtcp-status-publisher",
    "v5-linuxcnc-command-gate",
]

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

lock_process_memory("v5_settings_actiond")

stop_event = threading.Event()
status_lock = threading.Lock()
active_job_lock = threading.Lock()
active_job: Dict[str, Any] = {}
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
    "result_path": "",
    "axis": "",
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
            "result_path": str(spec.get("result_path", "")),
            "axis": axis,
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


def reload_position_publisher() -> Dict[str, Any]:
    script = Path('/etc/init.d/v5-wcs-status-publisher')
    if not script.exists():
        return {"ok": False, "code": "POSITION_PUBLISHER_RELOAD_SCRIPT_MISSING", "script": str(script)}
    try:
        proc = subprocess.run([str(script), 'restart'], text=True, capture_output=True, timeout=6.0, check=False)
    except Exception as exc:
        return {"ok": False, "code": "POSITION_PUBLISHER_RELOAD_EXCEPTION", "detail": "%s: %s" % (type(exc).__name__, exc), "script": str(script)}
    return {
        "ok": proc.returncode == 0,
        "code": "POSITION_PUBLISHER_RELOADED" if proc.returncode == 0 else "POSITION_PUBLISHER_RELOAD_FAILED",
        "returncode": proc.returncode,
        "stdout": proc.stdout[-1000:],
        "stderr": proc.stderr[-1000:],
        "script": str(script),
    }


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


def service_status(name: str, timeout_s: float = 4.0) -> Dict[str, Any]:
    script = Path('/etc/init.d') / name
    if not script.exists():
        return {"ok": False, "code": "SERVICE_SCRIPT_MISSING", "script": str(script)}
    try:
        proc = subprocess.run([str(script), 'status'], text=True, capture_output=True, timeout=timeout_s, check=False)
    except Exception as exc:
        return {"ok": False, "code": "SERVICE_STATUS_EXCEPTION", "detail": "%s: %s" % (type(exc).__name__, exc), "script": str(script)}
    return {
        "ok": proc.returncode == 0,
        "code": "SERVICE_RUNNING" if proc.returncode == 0 else "SERVICE_NOT_RUNNING",
        "returncode": proc.returncode,
        "stdout": proc.stdout[-1000:],
        "stderr": proc.stderr[-1000:],
        "script": str(script),
    }


def restart_service(name: str, timeout_s: float = 8.0) -> Dict[str, Any]:
    script = Path('/etc/init.d') / name
    if not script.exists():
        return {"service": name, "ok": False, "code": "SERVICE_SCRIPT_MISSING", "script": str(script)}
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    safe_name = ''.join(ch if ch.isalnum() or ch in ('-', '_') else '_' for ch in name)
    log_path = RUN_DIR / ("settings_restart_%s.log" % safe_name)
    try:
        with log_path.open('w', encoding='utf-8') as fp:
            proc = subprocess.run([str(script), 'restart'], text=True, stdout=fp, stderr=subprocess.STDOUT, timeout=timeout_s, check=False)
    except subprocess.TimeoutExpired as exc:
        status = service_status(name)
        detail = str(exc)
        try:
            output = log_path.read_text(encoding='utf-8', errors='replace')[-2000:]
        except Exception:
            output = ''
        return {"service": name, "ok": False, "code": "SERVICE_RESTART_TIMEOUT", "script": str(script), "timeout_s": timeout_s, "detail": detail, "stdout": output, "log_path": str(log_path), "status": status}
    except Exception as exc:
        status = service_status(name)
        return {"service": name, "ok": False, "code": "SERVICE_RESTART_EXCEPTION", "script": str(script), "detail": "%s: %s" % (type(exc).__name__, exc), "log_path": str(log_path), "status": status}
    try:
        output = log_path.read_text(encoding='utf-8', errors='replace')[-2000:]
    except Exception:
        output = ''
    status = service_status(name)
    ok = proc.returncode == 0 and bool(status.get('ok'))
    return {
        "service": name,
        "ok": ok,
        "code": "SERVICE_RESTARTED" if ok else "SERVICE_RESTART_FAILED",
        "returncode": proc.returncode,
        "stdout": output,
        "stderr": "",
        "script": str(script),
        "timeout_s": timeout_s,
        "log_path": str(log_path),
        "status": status,
    }


def run_restart_handoff(action: str, spec: Dict[str, Any]) -> Dict[str, Any]:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    handoff_script = RUN_DIR / "settings_clean_restart_handoff.sh"
    handoff_log = RUN_DIR / "settings_clean_restart_handoff.log"
    service_list = " ".join(CANONICAL_CLEAN_RESTART_SERVICES)
    handoff_script.write_text("""#!/bin/sh
set -u
LOG="%s"
exec >>"$LOG" 2>&1
echo "clean_restart_handoff begin $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
sync || true
for svc in %s; do
  if [ -x "/etc/init.d/$svc" ]; then
    echo "stop $svc"
    "/etc/init.d/$svc" stop || true
  fi
done
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill "$pid" 2>/dev/null || true
done
sleep 0.3
for pidfile in /run/8ax/*.pid; do
  [ -f "$pidfile" ] || continue
  pid=$(cat "$pidfile" 2>/dev/null || true)
  case "$pid" in
    ''|*[!0-9]*) continue ;;
  esac
  kill -KILL "$pid" 2>/dev/null || true
done
rm -f /run/8ax/*.pid
rm -f /run/8ax_v5_product_ui/*.sock
rm -f /dev/shm/v3_status_shm
rm -f /dev/shm/v5_native_*.bin
rm -f /run/8ax_v5_drive/drive_profile_resident_snapshot.json
rm -f /run/8ax_v5_product_ui/settings_actiond_events.jsonl
rm -f /run/8ax_v5_product_ui/touch_events.jsonl
sync || true
echo "clean_restart_handoff reboot $(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')"
if command -v reboot >/dev/null 2>&1; then
  reboot -f
elif [ -x /sbin/reboot ]; then
  /sbin/reboot -f
else
  echo b >/proc/sysrq-trigger
fi
""" % (str(handoff_log), service_list), encoding="utf-8")
    handoff_script.chmod(0o755)
    try:
        # This is the cancellable pre-commit window. After Popen succeeds the
        # detached handoff owns the reboot and the action becomes terminal.
        time.sleep(1.0)
        subprocess.Popen(
            ["/bin/sh", str(handoff_script)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        ok = True
        code = "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED"
    except Exception as exc:
        ok = False
        code = "SETTINGS_SAVE_RESTART_HANDOFF_SPAWN_FAILED"
        append_event({"event": "clean_restart_spawn_failed", "action": action, "ok": False, "detail": "%s: %s" % (type(exc).__name__, exc)})
    return {
        "schema": "v5.settings_action_result.v1",
        "generated_at": now_utc(),
        "action": action,
        "owner": spec.get("owner", ""),
        "ok": ok,
        "code": code,
        "message_cn": "保存并重启已进入开发板干净重启流程。" if ok else "保存并重启 handoff 启动失败。",
        "display_message_cn": "保存并重启已进入开发板干净重启流程。" if ok else "保存并重启 handoff 启动失败。",
        "write_executed": False,
        "motion_executed": False,
        "restart_executed": True,
        "clean_restart_equivalent": "board_reboot",
        "handoff_script": str(handoff_script),
        "handoff_log": str(handoff_log),
        "stop_order": CANONICAL_CLEAN_RESTART_SERVICES,
    }


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
            result = v5_drive_bus_action.run_action(drive_action, drive_timeout_for_action(drive_action), True, request or {"action": action})
            if action == "settings_axis_zero" and bool(result.get("ok")):
                reload_result = reload_position_publisher()
                result["position_publisher_reload"] = reload_result
                if not reload_result.get("ok"):
                    result["ok"] = False
                    result["code"] = "SETTINGS_AXIS_ZERO_POSITION_RELOAD_FAILED"
                    result["message_cn"] = "设0零点已写入，但坐标显示发布器重载失败，当前机械值未证明已刷新。"
                    result["display_message_cn"] = result["message_cn"]
                write_json(result_path, result)
        elif spec.get("handler") == "auth":
            result = run_auth_action(action, spec)
            write_json(result_path, result)
        elif spec.get("handler") == "restart":
            result = run_restart_handoff(action, spec)
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
    return result


def write_pipe_payload(fd: int, payload: Dict[str, Any]) -> None:
    data = bounded_json_text(
        payload,
        MAX_ACTIOND_RESULT_BYTES,
        "SETTINGS_ACTION_RESULT_BUDGET_EXCEEDED").encode("utf-8")
    offset = 0
    while offset < len(data):
        offset += os.write(fd, data[offset:])


def action_worker(write_fd: int, action: str, request: Dict[str, Any]) -> None:
    try:
        os.setsid()
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        write_pipe_payload(write_fd, execute_action(action, request))
    except BaseException as exc:
        try:
            write_pipe_payload(write_fd, {
                "schema": "v5.settings_action_result.v1",
                "generated_at": now_utc(),
                "action": action,
                "ok": False,
                "code": "SETTINGS_ACTION_WORKER_EXCEPTION",
                "message_cn": "设置动作子进程异常终止。",
                "detail": "%s: %s" % (type(exc).__name__, exc),
            })
        except BaseException:
            pass
    finally:
        try:
            os.close(write_fd)
        except OSError:
            pass
        os._exit(0)


def read_pipe_payload(read_fd: int) -> bytes:
    chunks = []
    total = 0
    while total <= MAX_ACTIOND_RESULT_BYTES:
        chunk = os.read(read_fd, min(4096, MAX_ACTIOND_RESULT_BYTES + 1 - total))
        if not chunk:
            break
        chunks.append(chunk)
        total += len(chunk)
    return b"".join(chunks)


def monitor_action(pid: int, read_fd: int, action: str, run_id: str,
                   spec: Dict[str, Any], axis_hint: str) -> None:
    try:
        raw = read_pipe_payload(read_fd)
    finally:
        os.close(read_fd)
    _waited_pid, wait_status = os.waitpid(pid, 0)
    with active_job_lock:
        cancelled = bool(active_job.get("run_id") == run_id and active_job.get("cancel_requested"))
    if cancelled:
        result = {
            "schema": "v5.settings_action_result.v1",
            "generated_at": now_utc(),
            "action": action,
            "owner": spec.get("owner", ""),
            "ok": False,
            "code": "SETTINGS_ACTION_CANCELLED",
            "message_cn": "后台动作已取消，子进程及进程组已结束。",
            "cancelled": True,
            "axis": axis_hint,
        }
        terminal_state = "cancelled"
    else:
        try:
            result = json.loads(raw.decode("utf-8"))
            if not isinstance(result, dict):
                raise ValueError("worker_result_not_object")
        except Exception as exc:
            result = {
                "schema": "v5.settings_action_result.v1",
                "generated_at": now_utc(),
                "action": action,
                "owner": spec.get("owner", ""),
                "ok": False,
                "code": "SETTINGS_ACTION_WORKER_RESULT_MISSING",
                "message_cn": "设置动作子进程未返回有效终态。",
                "detail": "%s: %s" % (type(exc).__name__, exc),
                "wait_status": wait_status,
                "axis": axis_hint,
            }
        terminal_state = "success" if bool(result.get("ok")) else "failed"
    with active_job_lock:
        if active_job.get("run_id") == run_id:
            active_job.clear()
    set_last_status(action, run_id, spec, False, result, axis_hint=axis_hint, state=terminal_state)
    append_event({
        "event": terminal_state,
        "action": action,
        "run_id": run_id,
        "ok": bool(result.get("ok")),
        "code": result.get("code", ""),
        "result_path": str(spec.get("result_path", "")),
    })


def start_action_process(action: str, run_id: str, request: Dict[str, Any]) -> Dict[str, Any]:
    spec = ACTIONS[action]
    axis_hint = str(request.get("axis") or "")
    with active_job_lock:
        if active_job:
            return {
                "ok": False,
                "accepted": False,
                "schema": "v5.settings_actiond_response.v1",
                "action": action,
                "run_id": run_id,
                "code": "SETTINGS_ACTION_BUSY",
                "active_run_id": str(active_job.get("run_id") or ""),
            }
        read_fd, write_fd = os.pipe()
        try:
            pid = os.fork()
        except OSError as exc:
            os.close(read_fd)
            os.close(write_fd)
            return {
                "ok": False,
                "accepted": False,
                "schema": "v5.settings_actiond_response.v1",
                "action": action,
                "run_id": run_id,
                "code": "SETTINGS_ACTION_WORKER_FORK_FAILED",
                "detail": "%s: %s" % (type(exc).__name__, exc),
            }
        if pid == 0:
            os.close(read_fd)
            action_worker(write_fd, action, request)
        os.close(write_fd)
        active_job.update({
            "pid": pid,
            "action": action,
            "run_id": run_id,
            "cancel_requested": False,
        })
    set_last_status(
        action, run_id, spec, True, axis_hint=axis_hint,
        state="running", cancel_allowed=True)
    append_event({
        "event": "started",
        "action": action,
        "run_id": run_id,
        "owner": spec.get("owner", ""),
        "pid": pid,
        "result_path": str(spec.get("result_path", "")),
    })
    thread = threading.Thread(
        target=monitor_action,
        args=(pid, read_fd, action, run_id, spec, axis_hint),
        daemon=True)
    thread.start()
    return {
        "ok": True,
        "accepted": True,
        "schema": "v5.settings_actiond_response.v1",
        "action": action,
        "run_id": run_id,
        "owner": spec.get("owner", ""),
        "result_path": spec.get("result_path", ""),
    }


def signal_job(pid: int, sig: int) -> None:
    try:
        os.killpg(pid, sig)
    except ProcessLookupError:
        return
    except OSError:
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            pass


def force_cancel_after_grace(pid: int, run_id: str) -> None:
    time.sleep(1.0)
    with active_job_lock:
        still_active = active_job.get("pid") == pid and active_job.get("run_id") == run_id
    if still_active:
        signal_job(pid, signal.SIGKILL)


def cancel_action_process(run_id: str) -> Dict[str, Any]:
    with active_job_lock:
        if not active_job:
            return {"ok": False, "accepted": False, "code": "SETTINGS_ACTION_NOT_RUNNING", "run_id": run_id}
        if active_job.get("run_id") != run_id:
            return {
                "ok": False,
                "accepted": False,
                "code": "SETTINGS_ACTION_RUN_ID_MISMATCH",
                "run_id": run_id,
                "active_run_id": str(active_job.get("run_id") or ""),
            }
        if active_job.get("cancel_requested"):
            return {"ok": True, "accepted": True, "code": "SETTINGS_ACTION_CANCEL_ALREADY_REQUESTED", "run_id": run_id}
        active_job["cancel_requested"] = True
        pid = int(active_job["pid"])
        action = str(active_job["action"])
        spec = ACTIONS[action]
    set_last_status(
        action, run_id, spec, True,
        result={"code": "SETTINGS_ACTION_CANCELLING", "message_cn": "正在取消后台动作..."},
        state="cancelling", cancel_allowed=False)
    append_event({"event": "cancel_requested", "action": action, "run_id": run_id, "pid": pid})
    signal_job(pid, signal.SIGTERM)
    threading.Thread(target=force_cancel_after_grace, args=(pid, run_id), daemon=True).start()
    return {"ok": True, "accepted": True, "code": "SETTINGS_ACTION_CANCEL_REQUESTED", "run_id": run_id}


def handle_client(conn: socket.socket) -> None:
    with conn:
        data = b""
        oversize = False
        while b"\n" not in data and len(data) < MAX_ACTIOND_REQUEST_BYTES:
            chunk = conn.recv(min(4096, MAX_ACTIOND_REQUEST_BYTES - len(data)))
            if not chunk:
                break
            data += chunk
        if b"\n" not in data and len(data) >= MAX_ACTIOND_REQUEST_BYTES:
            oversize = True
        try:
            if oversize:
                raise ValueError("request_budget_exceeded")
            request = json.loads(data.decode("utf-8").strip() or "{}")
            if str(request.get("query") or "") == "last_status":
                response = get_last_status()
                conn.sendall((json.dumps(response, ensure_ascii=False, sort_keys=True) + "\n").encode("utf-8"))
                return
            if str(request.get("action") or "") == "job_cancel":
                response = cancel_action_process(str(request.get("run_id") or ""))
                conn.sendall(bounded_json_text(
                    response,
                    MAX_ACTIOND_RESULT_BYTES,
                    "SETTINGS_ACTIOND_RESPONSE_BUDGET_EXCEEDED").encode("utf-8"))
                return
            action = str(request.get("action") or "")
        except Exception:
            action = ""
        spec = ACTIONS.get(action)
        run_id = "%d-%06d" % (int(time.time()), int(time.monotonic() * 1000000) % 1000000)
        if spec:
            response = start_action_process(action, run_id, request)
        else:
            response = {"ok": False, "accepted": False, "schema": "v5.settings_actiond_response.v1", "action": action, "run_id": run_id, "code": "SETTINGS_ACTIOND_REQUEST_BUDGET_EXCEEDED" if oversize else "UNKNOWN_SETTINGS_ACTION"}
        conn.sendall(bounded_json_text(response, MAX_ACTIOND_RESULT_BYTES, "SETTINGS_ACTIOND_RESPONSE_BUDGET_EXCEEDED").encode("utf-8"))


def serve() -> int:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    drive_resident_preload = v5_drive_bus_action.preload_resident_state()
    try:
        SOCKET_PATH.unlink()
    except FileNotFoundError:
        pass
    signal.signal(signal.SIGTERM, lambda _signum, _frame: stop_event.set())
    signal.signal(signal.SIGINT, lambda _signum, _frame: stop_event.set())
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(SOCKET_PATH))
        os.chmod(SOCKET_PATH, 0o666)
        server.listen(8)
        server.settimeout(0.5)
        append_event({"event": "ready", "socket": str(SOCKET_PATH), "actions": sorted(ACTIONS), "drive_resident_preload": drive_resident_preload})
        while not stop_event.is_set():
            try:
                conn, _addr = server.accept()
            except socket.timeout:
                continue
            except OSError:
                if stop_event.is_set():
                    break
                raise
            handle_client(conn)
    with active_job_lock:
        active_run_id = str(active_job.get("run_id") or "")
    if active_run_id:
        cancel_action_process(active_run_id)
    try:
        SOCKET_PATH.unlink()
    except FileNotFoundError:
        pass
    append_event({"event": "stopped", "socket": str(SOCKET_PATH)})
    return 0


if __name__ == "__main__":
    raise SystemExit(serve())
