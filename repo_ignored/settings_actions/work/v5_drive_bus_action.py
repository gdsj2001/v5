#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List


RUN_DIR = Path("/run/8ax_v5_drive")


def now_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(path)


def run_ethercat_slaves(timeout_s: float) -> Dict[str, Any]:
    try:
        proc = subprocess.run(
            ["ethercat", "slaves"],
            text=True,
            capture_output=True,
            timeout=timeout_s,
            check=False,
        )
    except FileNotFoundError:
        return {
            "ok": False,
            "code": "ETHERCAT_TOOL_MISSING",
            "message_cn": "板端缺少 ethercat 工具，不能扫描从站。",
            "slaves": [],
        }
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "code": "ETHERCAT_SCAN_TIMEOUT",
            "message_cn": "扫描从站超时。",
            "slaves": [],
        }
    slaves: List[Dict[str, Any]] = []
    for line in proc.stdout.splitlines():
        text = line.strip()
        if not text:
            continue
        parts = text.split()
        slaves.append({"line": text, "position": parts[0] if parts else ""})
    return {
        "ok": proc.returncode == 0,
        "code": "DRIVE_SCAN_OK" if proc.returncode == 0 else "DRIVE_SCAN_FAILED",
        "message_cn": "扫描从站完成。" if proc.returncode == 0 else "扫描从站失败。",
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "slaves": slaves,
    }


def unsupported(action: str) -> Dict[str, Any]:
    return {
        "ok": False,
        "code": "DRIVE_ACTION_CANONICAL_GATE_MISSING",
        "message_cn": "该驱动动作的 canonical 读写 gate 尚未接入；已 fail-closed，未写驱动。",
        "action": action,
        "write_executed": False,
        "motion_executed": False,
    }


def result_path(action: str) -> Path:
    names = {
        "scan": "drive_scan_result.json",
        "factory-reset": "drive_factory_reset_result.json",
        "read": "drive_read_result.json",
        "fault-reset": "drive_fault_reset_result.json",
        "set-drive": "drive_set_result.json",
    }
    return RUN_DIR / names.get(action, "drive_action_result.json")


def main() -> int:
    parser = argparse.ArgumentParser(description="v5 drive bus settings actions")
    parser.add_argument("--action", required=True, choices=("scan", "factory-reset", "read", "fault-reset", "set-drive"))
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()
    if args.action == "scan":
        result = run_ethercat_slaves(args.timeout)
    else:
        result = unsupported(args.action)
    result.update({
        "schema": "v5.drive_bus_action.v1",
        "generated_at": now_utc(),
        "action": args.action,
    })
    write_json(result_path(args.action), result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
