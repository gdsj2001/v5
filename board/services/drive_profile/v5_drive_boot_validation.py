from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Dict

import v5_drive_bus_action
from v5_command_gate_zero_client import probe_axis_slave_mapping


BOOT_APPLY_RESULT_SCHEMA = "v5.drive_boot_apply_result.v1"
BOOT_APPLY_RESULT_PATH = v5_drive_bus_action.result_path("boot-apply")
BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
TRANSIENT_CODES = frozenset({
    "DRIVE_BOOT_NATIVE_MAPPING_NOT_READY",
    "DRIVE_ACTION_RESIDENT_PRELOAD_INCOMPLETE",
    "DRIVE_WRITE_WINDOW_GATE_TIMEOUT",
    "DRIVE_WRITE_WINDOW_GATE_EXEC_FAILED",
    "DRIVE_WRITE_WINDOW_GATE_RESULT_INVALID",
    "DRIVE_WRITE_WINDOW_CLOSE_FAILED",
    "DRIVE_WRITE_WINDOW_MACHINE_OFF_NOT_CONFIRMED",
    "DRIVE_SET_BATCH_READBACK_FAILED",
})


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _boot_id() -> str:
    try:
        return BOOT_ID_PATH.read_text(encoding="ascii").strip()
    except OSError:
        return ""


def _result(ok: bool, code: str, message_cn: str,
            **extra: Any) -> Dict[str, Any]:
    result: Dict[str, Any] = {
        "schema": BOOT_APPLY_RESULT_SCHEMA,
        "generated_at": now_utc(),
        "boot_id": _boot_id(),
        "ok": bool(ok),
        "code": code,
        "message_cn": message_cn,
        "motion_executed": False,
    }
    result.update(extra)
    return result


def _current_boot_result() -> Dict[str, Any] | None:
    try:
        payload = json.loads(BOOT_APPLY_RESULT_PATH.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return None
    if not isinstance(payload, dict):
        return None
    if (payload.get("schema") != BOOT_APPLY_RESULT_SCHEMA or
            not payload.get("ok") or
            str(payload.get("boot_id") or "") != _boot_id()):
        return None
    return payload


def _persist(result: Dict[str, Any]) -> Dict[str, Any]:
    v5_drive_bus_action.write_json(BOOT_APPLY_RESULT_PATH, result)
    return result


def run_post_restart_drive_apply(timeout_s: float = 8.0) -> Dict[str, Any]:
    """Apply final INI/mapping/profile once per board boot and keep Machine Off."""
    completed = _current_boot_result()
    if completed is not None:
        return completed
    try:
        probe_timeout = min(max(float(timeout_s), 0.1), 1.0)
        try:
            mapping = probe_axis_slave_mapping(probe_timeout)
        except OSError as exc:
            return _result(
                False,
                "DRIVE_BOOT_NATIVE_MAPPING_NOT_READY",
                "重启后 native 轴从站映射入口尚未就绪，保持 Machine Off。",
                detail="%s: %s" % (type(exc).__name__, exc),
                drive_write_executed=False,
            )
        if mapping.get("available") and not mapping.get("applicable"):
            return _persist(_result(
                True,
                "DRIVE_BOOT_APPLY_NOT_APPLICABLE",
                "当前不是 BUS 运行模式，不执行驱动启动应用。",
                drive_write_executed=False,
                readback_complete=True,
            ))
        if not mapping.get("ok"):
            return _result(
                False,
                "DRIVE_BOOT_NATIVE_MAPPING_NOT_READY",
                "重启后 native 轴从站映射尚未就绪，保持 Machine Off。",
                detail=mapping,
                drive_write_executed=False,
            )
        preload = v5_drive_bus_action.preload_resident_state()
        if not preload.get("ok"):
            return _result(
                False,
                str(preload.get("code") or
                    "DRIVE_ACTION_RESIDENT_PRELOAD_INCOMPLETE"),
                "重启后最终 model、mapping、INI 或 profile 未完整载入，保持 Machine Off。",
                owner_reload=preload,
                drive_write_executed=False,
            )
        raw = v5_drive_bus_action.run_action(
            "boot-apply",
            timeout_s,
            False,
            {
                "action": "boot-apply",
                "trigger": "post_restart_boot",
                "_run_id": "boot-%d" % time.monotonic_ns(),
            },
        )
        result = _result(
            bool(raw.get("ok")),
            "DRIVE_BOOT_APPLY_OK" if raw.get("ok") else str(
                raw.get("code") or "DRIVE_BOOT_APPLY_FAILED"),
            "重启后最终INI、轴从站映射和profile已统一应用，驱动整批fresh readback一致；当前保持Machine Off。"
            if raw.get("ok") else str(
                raw.get("message_cn") or
                "重启后驱动统一应用失败，保持Machine Off。"),
            owner_reload=preload,
            drive_apply=raw,
            drive_write_executed=bool(raw.get("drive_write_executed")),
            readback_complete=bool(raw.get("ok")),
        )
        return _persist(result)
    except Exception as exc:
        return _result(
            False,
            "DRIVE_BOOT_APPLY_EXCEPTION",
            "重启后驱动统一应用异常，保持 Machine Off。",
            detail="%s: %s" % (type(exc).__name__, exc),
            drive_write_executed=False,
        )


def run_boot_drive_apply_until_final(
        stop_event: Any,
        deadline_s: float = 120.0,
        retry_wait_s: float = 1.0,
        apply_timeout_s: float = 8.0) -> Dict[str, Any]:
    deadline = time.monotonic() + max(float(deadline_s), 0.1)
    result = _result(
        False,
        "DRIVE_BOOT_APPLY_NOT_RUN",
        "重启后驱动统一应用尚未执行，保持 Machine Off。",
    )
    while not stop_event.is_set() and time.monotonic() < deadline:
        result = run_post_restart_drive_apply(apply_timeout_s)
        if result.get("ok"):
            break
        if str(result.get("code") or "") not in TRANSIENT_CODES:
            break
        stop_event.wait(max(float(retry_wait_s), 0.0))
    if not result.get("ok"):
        _persist(result)
    return result
