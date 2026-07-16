from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Any, Dict

from v5_drive_bus_contract import DriveActionError


DRIVE_WINDOW_CLI = Path("/usr/libexec/8ax/v5_command_gate_drive_window")
RUN_ID_RE = re.compile(r"^[A-Za-z0-9_.:-]{1,64}$")
MAX_GATE_OUTPUT_BYTES = 8192


def _validated_run_id(run_id: str) -> str:
    value = str(run_id or "")
    if not RUN_ID_RE.fullmatch(value):
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_RUN_ID_INVALID",
            "设置驱动安全窗口缺少合法 run_id，未写驱动。",
            {"run_id_length": len(value)},
        )
    return value


def _run_gate(argv: list[str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        argv,
        text=True,
        capture_output=True,
        timeout=max(1.0, min(float(timeout_s), 10.0)),
        check=False,
    )


def _invoke(action: str, run_id: str, timeout_s: float, restore: bool = False) -> Dict[str, Any]:
    safe_run_id = _validated_run_id(run_id)
    argv = [str(DRIVE_WINDOW_CLI), action, safe_run_id]
    if action == "finish":
        argv.append("1" if restore else "0")
    try:
        proc = _run_gate(argv, timeout_s)
    except FileNotFoundError as exc:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_MISSING",
            "设置驱动 native 安全窗口入口不存在，未写驱动。",
            {"path": str(DRIVE_WINDOW_CLI)},
        ) from exc
    except subprocess.TimeoutExpired as exc:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_TIMEOUT",
            "设置驱动 native 安全窗口状态切换超时，已保持去使能。",
            {"action": action, "timeout_s": max(1.0, min(float(timeout_s), 10.0))},
        ) from exc
    except OSError as exc:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_EXEC_FAILED",
            "设置驱动 native 安全窗口入口执行失败，已保持去使能。",
            {"action": action, "error": type(exc).__name__},
        ) from exc

    stdout = proc.stdout or ""
    if len(stdout.encode("utf-8", errors="replace")) > MAX_GATE_OUTPUT_BYTES:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_RESULT_OVERSIZE",
            "设置驱动 native 安全窗口返回超出预算，已保持去使能。",
            {"action": action, "returncode": proc.returncode},
        )
    try:
        payload = json.loads(stdout)
        if not isinstance(payload, dict):
            raise ValueError("result_not_object")
    except Exception as exc:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_RESULT_INVALID",
            "设置驱动 native 安全窗口未返回合法结果，已保持去使能。",
            {"action": action, "returncode": proc.returncode},
        ) from exc
    if str(payload.get("run_id") or "") != safe_run_id:
        raise DriveActionError(
            "DRIVE_WRITE_WINDOW_GATE_RUN_ID_MISMATCH",
            "设置驱动 native 安全窗口返回了其它事务结果，已保持去使能。",
            {"action": action},
        )
    if proc.returncode != 0 or not bool(payload.get("ok")):
        raise DriveActionError(
            str(payload.get("code") or "DRIVE_WRITE_WINDOW_GATE_REJECTED"),
            str(payload.get("message_cn") or "设置驱动 native 安全窗口未建立，未写驱动。"),
            {
                "action": action,
                "returncode": proc.returncode,
                "gate_code": str(payload.get("code") or ""),
                "initial_machine_enabled": payload.get("initial_machine_enabled"),
                "final_machine_enabled": payload.get("final_machine_enabled"),
            },
        )
    return payload


def begin(run_id: str, timeout_s: float) -> Dict[str, Any]:
    return _invoke("begin", run_id, timeout_s)


def finish(run_id: str, timeout_s: float, restore: bool = False) -> Dict[str, Any]:
    return _invoke("finish", run_id, timeout_s, restore=restore)


def finish_safely(run_id: str, timeout_s: float, restore: bool = False) -> Dict[str, Any]:
    try:
        return finish(run_id, timeout_s, restore=restore)
    except DriveActionError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "message_cn": exc.message_cn,
            "detail": exc.detail,
        }


def abort(run_id: str, timeout_s: float = 3.0) -> Dict[str, Any]:
    return _invoke("abort", run_id, timeout_s)
