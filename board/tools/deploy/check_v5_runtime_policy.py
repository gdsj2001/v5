#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN_SHM_CONSUMER_DIRS = (
    ROOT / "services" / "command_gate",
    ROOT / "services" / "drive_profile",
    ROOT / "services" / "auth_download",
)
SHM_INCLUDE_RE = re.compile(r'#\s*include\s+"v5_status_shm(?:_mmap)?\.h"')
FORBIDDEN_SHM_FIELD_RE = re.compile(
    r"\b(wcs|g92|rtcp|tool|safety|mode|stillness|settings|runtime_modal_text)\b",
    re.IGNORECASE,
)
FORBIDDEN_CPU_RE = re.compile(r"\b(CPU_SET\s*\(\s*0\b|taskset\b.*(?:-c\s*0\b|-pc\s*0\b|\b0x1\b)|SCHED_FIFO)\b")
SOURCE_SUFFIXES = {".c", ".h", ".py", ".sh"}


def iter_sources(root: Path):
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            if path.name == "check_v5_runtime_policy.py":
                continue
            rel = path.relative_to(ROOT)
            if "repo_ignored" in rel.parts:
                continue
            yield path


def fail(path: Path, line_no: int, code: str, text: str) -> int:
    rel = path.relative_to(ROOT)
    print(f"{code}: {rel}:{line_no}: {text.strip()}", file=sys.stderr)
    return 1


def check_shm_consumers() -> int:
    rc = 0
    for root in FORBIDDEN_SHM_CONSUMER_DIRS:
        if not root.exists():
            continue
        for path in iter_sources(root):
            for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
                if SHM_INCLUDE_RE.search(line):
                    rc |= fail(path, line_no, "FORBIDDEN_SHM_CONSUMER", line)
    return rc


def check_shm_abi() -> int:
    header = ROOT / "app" / "include" / "v5_status_shm.h"
    rc = 0
    if not header.exists():
        print("MISSING_SHM_HEADER: app/include/v5_status_shm.h", file=sys.stderr)
        return 1
    for line_no, line in enumerate(header.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
        if FORBIDDEN_SHM_FIELD_RE.search(line):
            rc |= fail(header, line_no, "FORBIDDEN_SHM_ABI_FIELD", line)
    return rc


def check_cpu_policy() -> int:
    rc = 0
    for path in iter_sources(ROOT):
        for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            if FORBIDDEN_CPU_RE.search(line):
                rc |= fail(path, line_no, "FORBIDDEN_CPU0_OR_FIFO_POLICY", line)
    return rc


def check_settings_runtime_schema_guard() -> int:
    module_path = ROOT / "services" / "drive_profile" / "v5_drive_bus_action.py"
    spec = importlib.util.spec_from_file_location("v5_drive_bus_action_policy_check", module_path)
    if spec is None or spec.loader is None:
        print(f"SETTINGS_RUNTIME_GUARD_IMPORT_FAILED: {module_path}", file=sys.stderr)
        return 1
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    good_payload = {
        "schema": module.SETTINGS_RUNTIME_SCHEMA,
        "axes": [
            {
                "axis": "X",
                "drive_profile_id": "demo",
                "zero_model": {
                    "zero_anchor_counts": 100,
                    "counts_per_unit": 1000.0,
                    "drive_position": {
                        "actual_position_counts": 100,
                        "write_status": "write_verified_readback",
                    },
                },
            }
        ],
    }
    bad_payload = {
        "schema": module.SETTINGS_RUNTIME_SCHEMA,
        "axes": [
            {
                "axis": "X",
                "zero_model": {
                    "nested": {
                        "wcs_offsets": [0.0, 0.0, 0.0],
                    }
                },
            }
        ],
    }
    try:
        module.validate_settings_runtime_drive_only(good_payload)
    except Exception as exc:
        print(f"SETTINGS_RUNTIME_GUARD_REJECTED_GOOD: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    try:
        module.validate_settings_runtime_drive_only(bad_payload)
    except module.DriveActionError:
        return 0
    except Exception as exc:
        print(f"SETTINGS_RUNTIME_GUARD_WRONG_EXCEPTION: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    print("SETTINGS_RUNTIME_GUARD_ACCEPTED_FORBIDDEN_WCS", file=sys.stderr)
    return 1


def check_remote_relay_access_control() -> int:
    rc = 0
    source = ROOT / "app" / "src" / "v5_lvgl_remote_display.c"
    init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    if not source.exists() or not init.exists():
        print("REMOTE_RELAY_ACCESS_CONTROL_MISSING_SOURCE", file=sys.stderr)
        return 1
    source_text = source.read_text(encoding="utf-8", errors="ignore")
    init_text = init.read_text(encoding="utf-8", errors="ignore")
    required_source = (
        "V5_UI_REMOTE_ALLOW_CIDRS",
        "remote_peer_allowed",
        "remote_peer_not_allowed",
        "parse_bind_address",
    )
    for token in required_source:
        if token not in source_text:
            print(f"REMOTE_RELAY_ACCESS_CONTROL_MISSING: {source.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    required_init = (
        "V5_UI_REMOTE_BIND",
        "V5_UI_REMOTE_ALLOW_CIDRS",
        "REMOTE_ALLOW_CIDRS",
    )
    for token in required_init:
        if token not in init_text:
            print(f"REMOTE_RELAY_INIT_ALLOWLIST_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    return rc


def main() -> int:
    return (
        check_shm_consumers() |
        check_shm_abi() |
        check_cpu_policy() |
        check_settings_runtime_schema_guard() |
        check_remote_relay_access_control()
    )


if __name__ == "__main__":
    raise SystemExit(main())
