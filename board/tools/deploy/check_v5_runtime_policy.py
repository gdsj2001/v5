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
SELF_PARAMETER_TABLE_DEPLOY_ROW = {
    "config/settings/self_parameter_table.tsv": "/opt/8ax/v5/config/settings/self_parameter_table.tsv",
}
BOARD_OWNER_DEPLOY_ROWS = {
    "config/settings/drive_parameter_table.tsv": ("runtime_seed", "/opt/8ax/v5/config/settings/drive_parameter_table.tsv", "0644"),
    "linuxcnc/ini/v5_bus.ini": ("linuxcnc", "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini", "0644"),
    "linuxcnc/runtime/var/linuxcnc.var": ("runtime_seed", "/opt/8ax/v5/linuxcnc/var/linuxcnc.var", "0644"),
    "linuxcnc/runtime/var/tool.tbl": ("runtime_seed", "/opt/8ax/v5/linuxcnc/var/tool.tbl", "0644"),
}


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


def check_settings_parameter_table_deploy_kind() -> int:
    manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    if not manifest.exists():
        print("SETTINGS_TABLE_MANIFEST_MISSING: config/deploy/v5_runtime_deploy_manifest.tsv", file=sys.stderr)
        return 1
    rows = {}
    rc = 0
    for line_no, line in enumerate(manifest.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            print(f"DEPLOY_MANIFEST_BAD_ROW: {manifest.relative_to(ROOT)}:{line_no}", file=sys.stderr)
            return 1
        kind, source, destination, mode = parts
        if source == "config/settings/microkernel_parameter_table.tsv":
            print(f"RETIRED_MICROKERNEL_PARAMETER_TABLE_DEPLOYED: {manifest.relative_to(ROOT)}:{line_no}", file=sys.stderr)
            rc = 1
        if kind == "runtime_seed_merge" and (
            source not in SELF_PARAMETER_TABLE_DEPLOY_ROW or
            SELF_PARAMETER_TABLE_DEPLOY_ROW.get(source) != destination or
            mode != "0644"
        ):
            print(
                "SETTINGS_TABLE_DEPLOY_MERGE_NOT_SELF_TABLE: "
                f"{manifest.relative_to(ROOT)}:{line_no} {source} -> {destination} mode={mode}",
                file=sys.stderr,
            )
            rc = 1
        rows[source] = (kind, destination, mode, line_no)
    for source, destination in SELF_PARAMETER_TABLE_DEPLOY_ROW.items():
        row = rows.get(source)
        if row is None:
            print(f"SETTINGS_TABLE_DEPLOY_ROW_MISSING: {source}", file=sys.stderr)
            rc = 1
            continue
        kind, actual_destination, mode, line_no = row
        if kind != "runtime_seed_merge" or actual_destination != destination or mode != "0644":
            print(
                "SETTINGS_TABLE_DEPLOY_KIND_WRONG: "
                f"{manifest.relative_to(ROOT)}:{line_no} {source} kind={kind} dest={actual_destination} mode={mode}",
                file=sys.stderr,
            )
            rc = 1
    for source, expected in BOARD_OWNER_DEPLOY_ROWS.items():
        row = rows.get(source)
        if row is None:
            print(f"BOARD_OWNER_DEPLOY_ROW_MISSING: {source}", file=sys.stderr)
            rc = 1
            continue
        kind, actual_destination, mode, line_no = row
        expected_kind, expected_destination, expected_mode = expected
        if (kind, actual_destination, mode) != expected:
            print(
                "BOARD_OWNER_DEPLOY_KIND_WRONG: "
                f"{manifest.relative_to(ROOT)}:{line_no} {source} "
                f"kind={kind} dest={actual_destination} mode={mode} "
                f"expected={expected_kind},{expected_destination},{expected_mode}",
                file=sys.stderr,
            )
            rc = 1
    return rc


def check_settings_parameter_table_backup_before_merge() -> int:
    script = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    if not script.exists():
        print("SETTINGS_TABLE_INSTALL_SCRIPT_MISSING: tools/deploy/install_v5_runtime.sh", file=sys.stderr)
        return 1
    text = script.read_text(encoding="utf-8", errors="ignore")
    forbidden = (
        "config/settings/microkernel_parameter_table.tsv:/opt/8ax/v5/config/settings/microkernel_parameter_table.tsv:0644",
        "config/settings/drive_parameter_table.tsv:/opt/8ax/v5/config/settings/drive_parameter_table.tsv:0644\") ;;",
    )
    for token in forbidden:
        if token in text:
            print(f"SETTINGS_TABLE_MERGE_FORBIDDEN_TOKEN: {script.relative_to(ROOT)} has {token}", file=sys.stderr)
            return 1
    required = (
        "config/settings/self_parameter_table.tsv:/opt/8ax/v5/config/settings/self_parameter_table.tsv:0644",
        "parameter_table_backup_dir",
        "project_root=\"${project_root%/board}\"",
        "backup_dir.mkdir(parents=True, exist_ok=True)",
        "backup = backup_dir /",
        "shutil.copy2(dst, backup)",
        ".bak.",
        "expected_text",
        "tmp.write_text",
        "os.replace(tmp, dst)",
        "actual_text = dst.read_text",
        "actual_keys",
        "expected_keys",
        "shutil.copy2(backup, dst)",
        "format=ok",
    )
    for token in required:
        if token not in text:
            print(f"SETTINGS_TABLE_BACKUP_BEFORE_MERGE_MISSING: {script.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            return 1
    if text.index("shutil.copy2(dst, backup)") > text.index("tmp.write_text"):
        print("SETTINGS_TABLE_BACKUP_AFTER_WRITE: backup must happen before tmp write", file=sys.stderr)
        return 1
    if text.index("shutil.copy2(dst, backup)") > text.index("os.replace(tmp, dst)"):
        print("SETTINGS_TABLE_BACKUP_AFTER_REPLACE: backup must happen before replace", file=sys.stderr)
        return 1
    return 0


def check_board_owner_refresh_before_bundle() -> int:
    script = ROOT / "tools" / "deploy" / "push_v5_runtime_to_board.sh"
    if not script.exists():
        print("BOARD_OWNER_REFRESH_SCRIPT_MISSING: tools/deploy/push_v5_runtime_to_board.sh", file=sys.stderr)
        return 1
    text = script.read_text(encoding="utf-8", errors="ignore")
    required = (
        "refresh_board_owner_files",
        "merge_board_self_parameter_table",
        "pull_board_owner_file",
        "local_backup_dir",
        "config/settings/self_parameter_table.tsv",
        "config/settings/drive_parameter_table.tsv",
        "linuxcnc/ini/v5_bus.ini",
        "linuxcnc/runtime/var/linuxcnc.var",
        "linuxcnc/runtime/var/tool.tbl",
        "/opt/8ax/v5/config/settings/self_parameter_table.tsv",
        "/opt/8ax/v5/config/settings/drive_parameter_table.tsv",
        "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini",
        "/opt/8ax/v5/linuxcnc/var/linuxcnc.var",
        "/opt/8ax/v5/linuxcnc/var/tool.tbl",
        "scp -q -P",
        "shutil.copy2(local, backup)",
        "os.replace(tmp_local, local)",
    )
    for token in required:
        if token not in text:
            print(f"BOARD_OWNER_REFRESH_MISSING: {script.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            return 1
    call_token = '\nrefresh_board_owner_files\n\nif [ "$refresh_only" -eq 1 ]; then'
    archive_token = "\narchive_dir="
    if call_token not in text or archive_token not in text or text.index(call_token) > text.index(archive_token):
        print("BOARD_OWNER_REFRESH_NOT_BEFORE_ARCHIVE", file=sys.stderr)
        return 1
    sync_script = ROOT / "tools" / "sync_win_source_to_vm.py"
    sync_text = sync_script.read_text(encoding="utf-8", errors="ignore")
    sync_required = (
        "refresh_board_owner_files_before_sync",
        "--refresh-board-owner-files",
        "V5_REPO_ROOT",
        "V5_LOCAL_OWNER_BACKUP_DIR",
        "V5_BOARD_SSH",
        "build_manifest(root, patterns)",
    )
    for token in sync_required:
        if token not in sync_text:
            print(f"BOARD_OWNER_REFRESH_SYNC_HOOK_MISSING: {sync_script.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            return 1
    sync_call_token = "refresh_code = refresh_board_owner_files_before_sync"
    if sync_call_token not in sync_text or sync_text.index(sync_call_token) > sync_text.index("build_manifest(root, patterns)"):
        print("BOARD_OWNER_REFRESH_SYNC_HOOK_AFTER_MANIFEST", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    return (
        check_shm_consumers() |
        check_shm_abi() |
        check_cpu_policy() |
        check_settings_runtime_schema_guard() |
        check_remote_relay_access_control() |
        check_settings_parameter_table_deploy_kind() |
        check_settings_parameter_table_backup_before_merge() |
        check_board_owner_refresh_before_bundle()
    )


if __name__ == "__main__":
    raise SystemExit(main())
