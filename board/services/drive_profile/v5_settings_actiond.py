#!/usr/bin/env python3
from __future__ import annotations

import json
import grp
import os
import pwd
import signal
import socket
import threading
import time
import ctypes
import resource
from typing import Any, Dict

from v5_settings_action_contract import (
    MAX_ACTIOND_REQUEST_BYTES,
    MAX_ACTIOND_RESULT_BYTES,
    RUN_DIR,
    SOCKET_PATH,
)
from v5_settings_action_runtime import (
    ACTIONS,
    append_event,
    bounded_json_text,
    execute_action,
    get_last_status,
    now_utc,
    set_last_status,
    v5_drive_bus_action,
)
from v5_settings_restart import restart_service, run_restart_handoff, service_status

SOCKET_OWNER_NAME = "root"
SOCKET_GROUP_NAME = "petalinux"


def secure_socket_permissions() -> None:
    try:
        owner_uid = pwd.getpwnam(SOCKET_OWNER_NAME).pw_uid
        group_gid = grp.getgrnam(SOCKET_GROUP_NAME).gr_gid
    except KeyError as exc:
        raise RuntimeError(
            f"settings actiond socket identity is unavailable: {SOCKET_OWNER_NAME}:{SOCKET_GROUP_NAME}"
        ) from exc
    try:
        os.chown(SOCKET_PATH, owner_uid, group_gid)
        os.chmod(SOCKET_PATH, 0o660)
    except OSError:
        try:
            SOCKET_PATH.unlink()
        except FileNotFoundError:
            pass
        raise

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

lock_process_memory("v5_settings_actiond")

stop_event = threading.Event()
active_job_lock = threading.Lock()
active_job: Dict[str, Any] = {}

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
        secure_socket_permissions()
        server.listen(8)
        server.settimeout(0.5)
        append_event({"event": "ready", "socket": str(SOCKET_PATH), "socket_owner": SOCKET_OWNER_NAME, "socket_group": SOCKET_GROUP_NAME, "socket_mode": "0660", "actions": sorted(ACTIONS), "drive_resident_preload": drive_resident_preload})
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
