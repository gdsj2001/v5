#!/usr/bin/env python3
from __future__ import annotations

import configparser
import hashlib
import importlib.util
import json
import re
import subprocess
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
MERGED_PARAMETER_TABLE_DEPLOY_ROWS = {
    "config/settings/self_parameter_table.tsv": "/opt/8ax/v5/config/settings/self_parameter_table.tsv",
    "config/settings/drive_parameter_table.tsv": "/opt/8ax/v5/config/settings/drive_parameter_table.tsv",
}
BOARD_OWNER_DEPLOY_ROWS = {
    "config/settings/drive_parameter_table.tsv": ("runtime_seed_merge", "/opt/8ax/v5/config/settings/drive_parameter_table.tsv", "0644"),
    "config/settings/settings_runtime.json": ("runtime_seed", "/opt/8ax/phase0_bus5/settings_runtime.json", "0644"),
    "linuxcnc/ini/v5_bus.ini": ("runtime_ini_cycle_merge", "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini", "0644"),
    "linuxcnc/runtime/var/linuxcnc.var": ("runtime_seed", "/opt/8ax/v5/linuxcnc/var/linuxcnc.var", "0644"),
    "linuxcnc/runtime/var/tool.tbl": ("runtime_seed", "/opt/8ax/v5/linuxcnc/var/tool.tbl", "0644"),
}


def iter_sources(root: Path):
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            if path.name in {
                "check_v5_runtime_policy.py",
                "check_v5_board_runtime_policy.py",
            }:
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
    module_dir = str(module_path.parent)
    path_added = module_dir not in sys.path
    if path_added:
        sys.path.insert(0, module_dir)
    try:
        spec.loader.exec_module(module)
    finally:
        if path_added:
            sys.path.remove(module_dir)
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
    migration_payload = {
        "schema": module.SETTINGS_RUNTIME_SCHEMA,
        "axes": [{
            "axis": "X",
            "velocity_feedforward_evidence": {"gain_after_reset_raw": 0},
            "keep": {"value": 7},
        }],
    }
    migrated = module.sanitize_settings_runtime_drive_only(migration_payload)
    if (
        "velocity_feedforward_evidence" in migrated["axes"][0]
        or migrated["axes"][0].get("keep") != {"value": 7}
    ):
        print("SETTINGS_RUNTIME_RETIRED_DRIVE_TUNING_MIGRATION_FAILED", file=sys.stderr)
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


def check_retired_drive_tuning_chain() -> int:
    drive_dir = ROOT / "services" / "drive_profile"
    forbidden_files = (
        drive_dir / "v5_drive_feedforward_action.py",
        drive_dir / "v5_drive_feedforward_recovery.py",
        drive_dir / "v5_drive_feedforward_action_smoke.py",
        drive_dir / "v5_drive_feedforward_recovery_smoke.py",
    )
    for path in forbidden_files:
        if path.exists():
            print(
                f"RETIRED_DRIVE_TUNING_FILE_SURVIVOR: {path.relative_to(ROOT)}",
                file=sys.stderr,
            )
            return 1
    forbidden_tokens = (
        "drive_velocity_feedforward_commission",
        "drive.read_velocity_feedforward_source",
        "drive.read_velocity_feedforward_filter",
        "drive.read_velocity_feedforward_gain",
        "drive.write_velocity_feedforward_gain",
        "drive.read_communication_eeprom_policy",
        "drive.write_communication_eeprom_policy",
    )
    consumers = (
        drive_dir / "v5_drive_bus_action.py",
        drive_dir / "v5_settings_actiond.py",
        drive_dir / "v5_settings_action_runtime.py",
        drive_dir / "v5_settings_action_contract.py",
        ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv",
        ROOT / "config" / "drive-profiles" / "public" / "driver_profile_map.json",
        ROOT / "config" / "drive-profiles" / "private" /
        "535e661e9ea313143fed0d86e9d982368ca9a70c7062823e25560f34ceef7f9d_driver_profile_map.json",
        ROOT / "tools" / "ci" / "run_v5_host_gate.ps1",
    )
    for path in consumers:
        text = path.read_text(encoding="utf-8", errors="strict")
        for token in forbidden_tokens:
            if token in text:
                print(
                    f"RETIRED_DRIVE_TUNING_TOKEN_SURVIVOR: "
                    f"{path.relative_to(ROOT)}:{token}",
                    file=sys.stderr,
                )
                return 1
    installer = (
        ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    ).read_text(encoding="utf-8", errors="strict")
    for required in (
        "cleanup_retired_drive_tuning_files",
        "/usr/libexec/8ax/drive_profile/v5_drive_feedforward_action.py",
        "/usr/libexec/8ax/drive_profile/v5_drive_feedforward_recovery.py",
        "velocity_feedforward_evidence",
    ):
        if required not in installer:
            print(
                f"RETIRED_DRIVE_TUNING_CLEANUP_MISSING: {required}",
                file=sys.stderr,
            )
            return 1
    return 0


def check_remote_relay_access_control() -> int:
    rc = 0
    source = ROOT / "app" / "src" / "v5_lvgl_remote_display.c"
    relay = ROOT / "services" / "ui" / "v5_remote_ui_relay.py"
    relay_smoke = (
        ROOT / "services" / "ui" / "v5_remote_ui_relay_coalesce_smoke.py"
    )
    auth_smoke = ROOT / "services" / "ui" / "v5_remote_ui_auth_smoke.py"
    tls_smoke = ROOT / "services" / "ui" / "v5_remote_ui_tls_smoke.py"
    manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    runtime_verify = ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh"
    cold_boot_measure = ROOT / "tools" / "deploy" / "measure_v5_cold_boot.py"
    relay_modules = (
        relay,
        ROOT / "services" / "ui" / "v5_remote_ui_relay_access.py",
        ROOT / "services" / "ui" / "v5_remote_ui_relay_stream.py",
        ROOT / "services" / "ui" / "v5_remote_ui_auth.py",
        ROOT / "services" / "ui" / "v5_remote_ui_local_client.py",
        ROOT / "services" / "ui" / "v5_remote_ui_state.py",
        ROOT / "services" / "ui" / "v5_remote_ui_dirty_geometry.py",
        ROOT / "services" / "ui" / "v5_remote_ui_shared_payload.py",
        ROOT / "services" / "ui" / "v5_remote_ui_support.py",
        ROOT / "services" / "ui" / "v5_status_shm_reader.py",
        ROOT / "services" / "ui" / "v5_remote_ui_protocol.py",
        ROOT / "services" / "ui" / "v5_remote_ui_contract.py",
    )
    init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    if (
        not source.exists()
        or not all(path.exists() for path in relay_modules)
        or not relay_smoke.exists()
        or not auth_smoke.exists()
        or not tls_smoke.exists()
        or not manifest.exists()
        or not init.exists()
        or not runtime_verify.exists()
        or not cold_boot_measure.exists()
    ):
        print("REMOTE_RELAY_ACCESS_CONTROL_MISSING_SOURCE", file=sys.stderr)
        return 1
    source_text = source.read_text(encoding="utf-8", errors="ignore")
    relay_text = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in relay_modules)
    relay_source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in relay_modules[:3]
    )
    relay_smoke_text = relay_smoke.read_text(encoding="utf-8", errors="ignore")
    auth_smoke_text = auth_smoke.read_text(encoding="utf-8", errors="ignore")
    tls_smoke_text = tls_smoke.read_text(encoding="utf-8", errors="ignore")
    manifest_text = manifest.read_text(encoding="utf-8", errors="ignore")
    init_text = init.read_text(encoding="utf-8", errors="ignore")
    runtime_verify_text = runtime_verify.read_text(encoding="utf-8", errors="ignore")
    cold_boot_measure_text = cold_boot_measure.read_text(
        encoding="utf-8", errors="ignore")
    required_source = (
        "remote_framebuffer.bgra",
        "remote_dirty",
        "mkfifo",
        "mmap",
    )
    for token in required_source:
        if token not in source_text:
            print(f"REMOTE_RELAY_ACCESS_CONTROL_MISSING: {source.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    forbidden_source = (
        "AF_INET",
        "SOCK_STREAM",
        "listen(",
        "accept(",
        "recv(",
        "/remote/frame/full",
        "/remote/input",
        "V5_UI_REMOTE_ALLOW_CIDRS",
    )
    for token in forbidden_source:
        if token in source_text:
            print(f"REMOTE_RELAY_UI_NETWORK_SURVIVOR: {source.relative_to(ROOT)} still has {token}", file=sys.stderr)
            rc = 1
    required_relay = (
        "V5_UI_REMOTE_BIND",
        "V5_UI_REMOTE_ALLOW_CIDRS",
        "remote_peer_not_allowed",
        "/remote/info",
        "/remote/frame/full",
        "/remote/stream",
        "/remote/input",
        "/remote/diagnostics",
        "remote_framebuffer.bgra",
        "remote_dirty",
        "remote_input",
        "metrics_snapshot",
        "cpu_samples_snapshot",
        "V5StatusShmReader",
        "cpu_sample_generation",
        "cpu_sample_monotonic_ns",
        "process_diagnostics",
        "full_frame_requests",
        "stream_active_sessions",
        "dirty_rect_frames",
        "framebuffer_mmap_refreshes",
        "dirty_payload_bytes",
        "dirty_payload_rects",
        "dirty_large_throttle_sleeps",
        "dirty_payload_union_frames",
        "stream_repair_full_frames",
        "stream_runtime_resets",
        "stream_runtime_history_gap_disconnects",
        "stream_runtime_invalid_dirty_disconnects",
        "previous_bands",
        '"stream_reset": True',
        "restart_stream=True",
        "if delivery.restart_stream:",
        "stream_idle_pings",
        "stream_send_failures",
        "input_active_sessions",
        "build_server_tls_context",
        "AuthStore.from_file",
        "tls_context.wrap_socket",
        "/local/health",
        "/remote/auth/challenge",
        "/remote/auth/session",
        "remote_auth_token_in_url_forbidden",
        "session_is_current",
        "TLSVersion.TLSv1_2",
        "V5Session",
    )
    for token in required_relay:
        if token not in relay_text:
            print(f"REMOTE_RELAY_PYTHON_BOUNDARY_MISSING: canonical relay modules lack {token}", file=sys.stderr)
            rc = 1
    for token in (
        "PreparedFrameDelivery(needs_full=True)",
        "if delivery.needs_full:",
    ):
        if token in relay_text:
            print(
                f"REMOTE_RELAY_RUNTIME_FULL_REPAIR_SURVIVOR: {token}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "v5_remote_ui_tls_smoke PASS",
        "context.wrap_socket(listener, server_side=True)",
        "check_plaintext_rejected",
        "certificate/device mismatch was accepted",
    ):
        if token not in tls_smoke_text:
            print(
                f"REMOTE_RELAY_TLS_SMOKE_CONTRACT_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "v5_remote_ui_auth_smoke PASS",
        "world-readable credential file accepted",
        "remote_scope_denied",
        "remote_session_invalid",
    ):
        if token not in auth_smoke_text:
            print(
                f"REMOTE_RELAY_AUTH_SMOKE_CONTRACT_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    for token in ("class CpuUsageSampler", 'cpu_percent("cpu0")'):
        if token in relay_text:
            print(
                f"REMOTE_RELAY_DUPLICATE_CPU_SAMPLER_SURVIVOR: {token}",
                file=sys.stderr,
            )
            rc = 1
    try:
        stream_body = relay_source_text[
            relay_source_text.index("    def handle_stream(self) -> None:"):
            relay_source_text.index("    def handle_input(self) -> None:")
        ]
    except ValueError:
        print("REMOTE_RELAY_STREAM_HANDLER_BOUNDARY_MISSING", file=sys.stderr)
        rc = 1
    else:
        if stream_body.count("self.state.full_frame()") != 1:
            print(
                "REMOTE_RELAY_INITIAL_FULL_FRAME_COUNT_INVALID",
                file=sys.stderr,
            )
            rc = 1
        if "if delivery.restart_stream:\n" not in stream_body:
            print("REMOTE_RELAY_RUNTIME_RESET_MISSING", file=sys.stderr)
            rc = 1
    for token in (
        "def check_continuous_30hz_input_is_coalesced_to_10hz() -> int:",
        "if len(steady_build_times) < 16 or len(steady_build_times) > 24:",
        "if cadence_hz < 8.0 or cadence_hz > 12.0:",
        "STREAM_TARGET_FPS != 10",
    ):
        if token not in relay_smoke_text:
            print(
                f"REMOTE_RELAY_10HZ_SMOKE_THRESHOLD_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    required_init = (
        "V5_UI_REMOTE_BIND",
        "V5_UI_REMOTE_ALLOW_CIDRS",
        "REMOTE_ALLOW_CIDRS",
        "v5_remote_ui_relay.py",
        "v5_remote_ui_local_client.py",
        "REMOTE_TLS_CERT",
        "REMOTE_TLS_KEY",
        "REMOTE_AUTH_CLIENTS",
        "https://127.0.0.1",
        "v5_ui_shell.pid",
    )
    for token in required_init:
        if token not in init_text:
            print(f"REMOTE_RELAY_INIT_ALLOWLIST_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    required_manifest_rows = (
        "module\tservices/ui/v5_remote_ui_relay_access.py\t/usr/libexec/8ax/v5_remote_ui_relay_access.py\t0644",
        "module\tservices/ui/v5_remote_ui_relay_stream.py\t/usr/libexec/8ax/v5_remote_ui_relay_stream.py\t0644",
        "module\tservices/ui/v5_remote_ui_auth.py\t/usr/libexec/8ax/v5_remote_ui_auth.py\t0644",
        "script\tservices/ui/v5_remote_ui_local_client.py\t/usr/libexec/8ax/v5_remote_ui_local_client.py\t0755",
    )
    for row in required_manifest_rows:
        if manifest_text.splitlines().count(row) != 1:
            print(
                f"REMOTE_RELAY_SECURITY_DEPLOY_ROW_INVALID: {row}",
                file=sys.stderr,
            )
            rc = 1
    for forbidden in ("http://127.0.0.1", "ws://127.0.0.1"):
        if (forbidden in relay_source_text or forbidden in init_text or
                forbidden in runtime_verify_text or
                forbidden in cold_boot_measure_text):
            print(
                f"REMOTE_RELAY_PLAINTEXT_FALLBACK_SURVIVOR: {forbidden}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "v5_remote_ui_local_client.py",
        "https://127.0.0.1:18080",
        "server-cert.pem",
        "clients.json",
        "--scope viewer",
        "--path /remote/info",
        "remote_framebuffer.bgra",
    ):
        if token not in runtime_verify_text:
            print(
                f"REMOTE_RELAY_RUNTIME_VERIFY_AUTH_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    for forbidden in ("wget ", "/remote/frame/full"):
        if forbidden in runtime_verify_text:
            print(
                f"REMOTE_RELAY_RUNTIME_VERIFY_PLAINTEXT_SURVIVOR: {forbidden}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "relay_info_probe",
        "/usr/libexec/8ax/v5_remote_ui_local_client.py",
        "https://127.0.0.1:18080",
        "--scope viewer",
        "--path /remote/info",
        "startup_servo_complete",
        '"servo-thread.tmax"',
        '"runtime_phase_step_max_ns"',
        '"startup_window_30s"',
    ):
        if token not in cold_boot_measure_text:
            print(
                f"COLD_BOOT_SECURE_STARTUP_PROOF_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    for forbidden in (
        "urllib.request",
        "http_probe",
        'urllib.request.urlopen("http://',
    ):
        if forbidden in cold_boot_measure_text:
            print(
                f"COLD_BOOT_PLAINTEXT_PROBE_SURVIVOR: {forbidden}",
                file=sys.stderr,
            )
            rc = 1
    return rc


def check_settings_actiond_socket_policy() -> int:
    actiond_path = ROOT / "services" / "drive_profile" / "v5_settings_actiond.py"
    if not actiond_path.is_file():
        print(f"SETTINGS_ACTIOND_SOCKET_OWNER_MISSING: {actiond_path}", file=sys.stderr)
        return 1
    actiond = actiond_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        'SOCKET_OWNER_NAME = "root"',
        'SOCKET_GROUP_NAME = "petalinux"',
        "pwd.getpwnam(SOCKET_OWNER_NAME).pw_uid",
        "grp.getgrnam(SOCKET_GROUP_NAME).gr_gid",
        "os.chown(SOCKET_PATH, owner_uid, group_gid)",
        "os.chmod(SOCKET_PATH, 0o660)",
    ):
        if token not in actiond:
            print(f"SETTINGS_ACTIOND_SOCKET_POLICY_MISSING: {token}", file=sys.stderr)
            return 1
    for forbidden in ("os.chmod(SOCKET_PATH, 0o666)", "os.chmod(SOCKET_PATH, 0o777)"):
        if forbidden in actiond:
            print(f"SETTINGS_ACTIOND_WORLD_WRITABLE_SOCKET_PRESENT: {forbidden}", file=sys.stderr)
            return 1
    return 0


def check_linuxcnc_rtapi_affinity_owner() -> int:
    init = ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate"
    backend_lifecycle = ROOT / "services" / "command_gate" / "v5_ethercat_backend_lifecycle.sh"
    backend_readiness_probe = ROOT / "services" / "command_gate" / "v5_backend_readiness_probe.c"
    backend_readiness_owner = (
        ROOT.parent / "linuxcnc" / "src" / "hal" / "user_comps" /
        "v5_native_hal_owner.comp"
    )
    backend_readiness_protocol = backend_readiness_owner.with_name(
        "v5_backend_readiness_protocol.h"
    )
    ethercat_recipe = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-kernel" /
        "ethercat-master" / "ethercat-master_git.bb"
    )
    lcec_health_patch = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" /
        "linuxcnc-ethercat" / "files" / "0002-v5-dc-reference-health-pins.patch"
    )
    lcec_initf_patch = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" /
        "linuxcnc-ethercat" / "files" / "0003-v5-require-initf-master-activation.patch"
    )
    retired_lcec_patches = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" /
        "linuxcnc-ethercat" / "files" / "0003-v5-defer-master-activation-to-first-read.patch",
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" /
        "linuxcnc-ethercat" / "files" / "0004-v5-resident-runtime-ready-pin.patch",
    )
    lcec_recipe = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" /
        "linuxcnc-ethercat" / "linuxcnc-ethercat_git.bb"
    )
    board_runtime_policy = ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py"
    ui_init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    net_core = (
        ROOT
        / "petalinux"
        / "project-spec"
        / "meta-user"
        / "recipes-apps"
        / "v5-base-overlay"
        / "files"
        / "network"
        / "v5_net_core.sh"
    )
    publisher_inits = (
        ROOT / "services" / "state_publisher" / "init.d" / "v5-state-publisher",
        ROOT / "services" / "state_publisher" / "init.d" / "v5-wcs-status-publisher",
    )
    position_publisher_init = (
        ROOT / "services" / "state_publisher" / "init.d" /
        "v5-position-status-publisher"
    )
    position_publisher = (
        ROOT / "services" / "state_publisher" /
        "v5_position_status_publisher.c"
    )
    position_sampler = (
        ROOT / "services" / "state_publisher" /
        "v5_position_status_sampler.c"
    )
    retired_position_sources = (
        ROOT / "services" / "state_publisher" /
        "v5_position_status_publisher.py",
        ROOT / "services" / "state_publisher" /
        "v5_position_status_publisher_test.py",
        ROOT / "services" / "state_publisher" /
        "v5_machine_status_projection_test.py",
    )
    machine_status_projection = (
        ROOT / "services" / "state_publisher" /
        "v5_machine_status_projection.py"
    )
    wcs_status_codec = (
        ROOT / "services" / "state_publisher" / "v5_wcs_status_codec.py"
    )
    bus_ini = ROOT / "linuxcnc" / "ini" / "v5_bus.ini"
    bus_hal = ROOT / "linuxcnc" / "hal" / "v5_bus_1ms.hal"
    linuxcnc_initf_sources = (
        ROOT.parent / "linuxcnc" / "src" / "hal" / "hal_lib.c",
        ROOT.parent / "linuxcnc" / "src" / "hal" / "utils" / "halcmd.c",
        ROOT.parent / "linuxcnc" / "src" / "hal" / "utils" / "halcmd_commands.cc",
        ROOT.parent / "linuxcnc" / "src" / "hal" / "utils" / "halcmd_commands.h",
    )
    motion_private = ROOT.parent / "linuxcnc" / "src" / "emc" / "motion" / "mot_priv.h"
    motion_export = ROOT.parent / "linuxcnc" / "src" / "emc" / "motion" / "motion.c"
    motion_control = ROOT.parent / "linuxcnc" / "src" / "emc" / "motion" / "control.c"
    relay_producer = ROOT / "services" / "ui" / "v5_remote_ui_shared_payload.py"
    deploy_manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    runtime_installer = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    net_init = net_core.with_name("S99v5-net")
    net_cpu_policy = net_core.with_name("v5_net_cpu_policy.sh")
    usb_wifi_apply = net_core.with_name("v5_usb_wifi_apply.sh")
    probe = ROOT / "services" / "command_gate" / "v5_linuxcncrsh_probe.c"
    if not init.exists():
        print("LINUXCNC_RTAPI_AFFINITY_INIT_MISSING: services/command_gate/init.d/v5-linuxcnc-command-gate", file=sys.stderr)
        return 1
    if not probe.exists():
        print("LINUXCNC_READ_ONLY_PROBE_MISSING: services/command_gate/v5_linuxcncrsh_probe.c", file=sys.stderr)
        return 1
    if not ui_init.exists():
        print("LINUXCNC_READ_ONLY_UI_INIT_MISSING: services/ui/init.d/v5-ui-relay", file=sys.stderr)
        return 1
    if (
        not backend_lifecycle.exists()
        or not backend_readiness_probe.exists()
        or not backend_readiness_owner.exists()
        or not backend_readiness_protocol.exists()
        or not ethercat_recipe.exists()
        or not lcec_health_patch.exists()
        or not lcec_initf_patch.exists()
        or not board_runtime_policy.exists()
        or not net_core.exists()
        or not net_init.exists()
        or not net_cpu_policy.exists()
        or not usb_wifi_apply.exists()
        or not relay_producer.exists()
        or not deploy_manifest.exists()
        or not runtime_installer.exists()
        or not position_publisher_init.exists()
        or not position_publisher.exists()
        or not position_sampler.exists()
        or not bus_ini.exists()
        or not bus_hal.exists()
        or not motion_private.exists()
        or not motion_export.exists()
        or not motion_control.exists()
        or any(not path.exists() for path in publisher_inits)
        or any(not path.exists() for path in linuxcnc_initf_sources)
    ):
        print("CPU_ISOLATION_OWNER_MISSING", file=sys.stderr)
        return 1
    text = init.read_text(encoding="utf-8", errors="ignore")
    required = (
        "linuxcnc_realtime_pids",
        "linuxcnc_privileged_helpers_ok",
        "/usr/bin/linuxcnc_module_helper",
        "set_linuxcnc_realtime_affinity",
        "set_ethercat_realtime_affinity",
        "linuxcnc_non_realtime_pids",
        "set_linuxcnc_non_realtime_affinity",
        "MICROKERNEL_NON_RT_NICE=-5",
        "set_linuxcnc_non_realtime_priority",
        'renice -n "$MICROKERNEL_NON_RT_NICE"',
        "linuxcncsvr milltask io linuxcncrsh v5_native_hal_owner",
        "rtapi_app:T#*",
        'taskset -pc 0 "$tid"',
        'taskset -pc 1 "$tid"',
        "LinuxCNC RTAPI process has no realtime servo thread",
        "taskset -a -pc 1",
        "BACKEND_READINESS_PROBE=/usr/libexec/8ax/v5_backend_readiness_probe",
        '"$BACKEND_READINESS_PROBE" --arm --wait data --timeout-ms 60000',
        '"$BACKEND_READINESS_PROBE" --wait motion --timeout-ms 120000',
        "backend_readiness_require motion",
        "backend_readiness_require_transaction",
        "--expect-generation",
        "--expect-owner-pid",
        "--expect-owner-start-ticks",
        "record_startup_event linuxcnc_spawned",
        "record_startup_event linuxcncrsh_probe_ready",
        "record_startup_event native_gate_socket_ready",
        "first_full_wkc",
        "dc_fresh_pair_ready",
        "cpu_contract_ready",
        "backend_ready_published",
        "drive-verified motion readiness",
        "REQUESTED_MODE_FILE=${V5_REQUESTED_MODE_FILE:-/opt/8ax/v5/config/settings/self_parameter_table.tsv}",
        "read_requested_driver_mode",
        '"SETTINGS" && $2 == "bus_pulse_setting"',
        "pulse_contract_runtime_selectable",
        "prepare_selected_transport",
        "/etc/init.d/ethercat start",
        "Pulse requested but contract is not runtime_selectable; refusing both motion backends",
    )
    rc = 0
    for token in required:
        if token not in text:
            print(f"LINUXCNC_RTAPI_AFFINITY_OWNER_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    for retired in (
        "backend_runtime_contract_ok",
        "wait_linuxcnc_backend_ready",
        "linuxcnc_realtime_scheduler_ok",
        "linuxcnc_realtime_affinity_ok",
        "linuxcnc_non_realtime_affinity_ok",
        "linuxcnc_non_realtime_priority_ok",
        "network_cpu_isolation_ok",
        "ethercat_backend_ready",
    ):
        if retired in text:
            print(f"LINUXCNC_RETIRED_SHELL_READINESS_SURVIVOR: {retired}", file=sys.stderr)
            rc = 1
    if '"$task/sched"' in text:
        print("LINUXCNC_RTAPI_SCHED_READER_SURVIVOR", file=sys.stderr)
        rc = 1
    lifecycle_text = backend_lifecycle.read_text(encoding="utf-8", errors="ignore")
    ui_init_text = ui_init.read_text(encoding="utf-8", errors="ignore")
    for service_init in (*publisher_inits, position_publisher_init, ui_init):
        service_text = service_init.read_text(encoding="utf-8", errors="strict")
        required_service_tokens = (
            "bind_service_init_to_cpu1() {",
            'done <"/proc/$$/status"',
            '[ "$cpu_list" = "1" ] && return 0',
            'exec /usr/bin/taskset -c 1 "$0" "$@"',
            'bind_service_init_to_cpu1 "$@"',
        )
        if (any(token not in service_text for token in required_service_tokens) or
                service_text.count("bind_service_init_to_cpu1") != 2 or
                "set -- $value" in service_text or
                service_text.find('bind_service_init_to_cpu1 "$@"') >
                service_text.find("LD_BIND_NOW=1")):
            print(
                "NON_RT_SERVICE_INIT_CPU1_REEXEC_MISSING: %s" %
                service_init.relative_to(ROOT), file=sys.stderr)
            rc = 1
    for token in (
        "configured_ethercat_slave_count",
        "ethercat_transport_scanned",
        "wait_ethercat_transport_scanned",
        "EtherCAT transport scan incomplete; refusing LinuxCNC/lcec activation",
        'if (($1 + 0) != (seen - 1)) bad = 1',
        'if ($3 != "PREOP") bad = 1',
        'if ($4 == "E") bad = 1',
        "capture_ethercat_attach_fault_baseline",
        "quiesce_ethercat_slaves_before_release",
        "ethercat_master_inactive",
    ):
        if token not in lifecycle_text:
            print(f"ETHERCAT_TRANSPORT_LIFECYCLE_MISSING: {token}", file=sys.stderr)
            rc = 1
    for retired in (
        "ethercat_no_post_attach_faults",
        "ethercat_backend_ready",
        "ethercat_domain_wkc_ready",
        "ethercat_resident_all_op",
        "ethercat_reference_clock_healthy",
        "halcmd getp",
        "ethercat domains",
    ):
        if retired in lifecycle_text:
            print(f"ETHERCAT_RETIRED_SHELL_READY_SURVIVOR: {retired}", file=sys.stderr)
            rc = 1
    transport_wait_index = text.find("wait_ethercat_transport_scanned || return 1")
    backend_start_index = text.find('su petalinux -c "cd /opt/8ax/v5/linuxcnc/ini')
    if (transport_wait_index < 0 or backend_start_index < 0 or
            transport_wait_index > backend_start_index):
        print("ETHERCAT_TRANSPORT_SCAN_BEFORE_LINUXCNC_MISSING", file=sys.stderr)
        rc = 1
    for forbidden in (
        "lcec.0.runtime-ready",
        "start_position_hal_consumer_before_ready",
        "position_consumer_attempted",
        "position_consumer_ready",
    ):
        if forbidden in lifecycle_text:
            print(f"ETHERCAT_RETIRED_READINESS_SURVIVOR: {forbidden}", file=sys.stderr)
            rc = 1
    if "halcmd" in ui_init_text:
        print("UI_BOOT_HAL_ATTACH_SURVIVOR", file=sys.stderr)
        rc = 1
    process_wait_index = text.find("wait_linuxcnc_process_set &&")
    affinity_index = text.find("set_linuxcnc_realtime_affinity &&", process_wait_index)
    linuxcncrsh_index = text.find("if ! start_gate; then")
    data_readiness_index = text.find("if ! arm_and_wait_backend_data_ready; then")
    native_gate_index = text.find("if ! start_native_gate; then", data_readiness_index)
    motion_readiness_index = text.find(
        "if ! wait_backend_motion_ready; then", native_gate_index)
    if (process_wait_index < 0 or affinity_index < process_wait_index or
            linuxcncrsh_index < affinity_index or
            data_readiness_index < linuxcncrsh_index or
            native_gate_index < data_readiness_index or
            motion_readiness_index < native_gate_index):
        print("CANONICAL_BACKEND_READINESS_EVENT_ORDER_MISSING", file=sys.stderr)
        rc = 1
    lcec_health_text = lcec_health_patch.read_text(encoding="utf-8", errors="strict")
    for token in (
        "activated",
        "domain-working-counter",
        "domain-wc-complete",
        "dc-time-valid",
        "dc-time-ok-seq",
        "dc-time-age-cycles",
        "dc-time-error-count",
        "ecrt_master_reference_clock_time",
    ):
        if token not in lcec_health_text:
            print(f"ETHERCAT_DC_HEALTH_PIN_MISSING: {token}", file=sys.stderr)
            rc = 1
    for forbidden in (
        "domain-wc-ok-seq",
        "domain-wc-error-count",
        "ecrt_domain_state(master->domain, &master->ds)",
    ):
        if forbidden in lcec_health_text:
            print(f"ETHERCAT_RETIRED_RESIDENT_WKC_SURVIVOR: {forbidden}", file=sys.stderr)
            rc = 1
    owner_text = backend_readiness_owner.read_text(encoding="utf-8", errors="strict")
    protocol_text = backend_readiness_protocol.read_text(encoding="utf-8", errors="strict")
    readiness_probe_text = backend_readiness_probe.read_text(encoding="utf-8", errors="strict")
    bus_hal_text = bus_hal.read_text(encoding="utf-8", errors="strict")
    for declared_pin in re.findall(
        r"^pin\s+(?:in|out|io)\s+\w+\s+([^;]+);", owner_text, re.MULTILINE
    ):
        hal_pin = (
            "v5-native-hal-owner."
            + declared_pin.replace("_", "-").replace("##[5]", "00")
        )
        if len(hal_pin) > 47:
            print(
                f"BACKEND_NATIVE_HAL_PIN_NAME_TOO_LONG: length={len(hal_pin)} name={hal_pin}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "backend_cpu_contract_capture",
        "rtapi_threads_contract",
        "backend_readiness_arm",
        "backend_readiness_tick",
        "backend_drive_verify",
        "backend_drive_invalidate",
        "registered_processes_failure",
        "V5_BACKEND_READINESS_SOCKET_PATH",
        "backend_domain_wkc",
        "backend_domain_wc_complete",
        "backend_dc_time_ok_seq",
        "backend_dc_time_error_count",
        "V5_BACKEND_IDENTITY_CHECK_NS",
    ):
        if token not in owner_text:
            print(f"BACKEND_NATIVE_READINESS_OWNER_MISSING: {token}", file=sys.stderr)
            rc = 1
    for forbidden in ("popen(", "system(", "halcmd", "ethercat domains"):
        if forbidden in owner_text:
            print(f"BACKEND_NATIVE_READINESS_EXTERNAL_SCAN_SURVIVOR: {forbidden}", file=sys.stderr)
            rc = 1
    for token in (
        "V5_BACKEND_READINESS_OP_ARM",
        "V5_BACKEND_READINESS_OP_DRIVE_VERIFY",
        "V5_BACKEND_READINESS_OP_DRIVE_INVALIDATE",
        "owner_start_ticks",
        "generation",
        "backend_data_ready",
        "motion_backend_ready",
        "first_full_wkc_ns",
        "dc_fresh_pair_ready_ns",
        "backend_ready_published_ns",
        "drive_verified",
        "drive_mapping_generation",
        "drive_transaction_sha256",
    ):
        if token not in protocol_text:
            print(f"BACKEND_READINESS_PROTOCOL_MISSING: {token}", file=sys.stderr)
            rc = 1
    for token in (
        "--verify-drive",
        "--invalidate-drive",
        "--drive-owner-generation",
        "--drive-transaction-sha256",
        "drive_identity_sha256",
    ):
        if token not in readiness_probe_text:
            print(f"BACKEND_READINESS_PROBE_DRIVE_ATTESTATION_MISSING: {token}", file=sys.stderr)
            rc = 1
    for token in (
        "--arm",
        "--wait",
        "--require",
        "--expect-generation",
        "--expect-owner-pid",
        "--expect-owner-start-ticks",
        "owner_start_ticks",
        "V5_BACKEND_READINESS_SOCKET_PATH",
    ):
        if token not in readiness_probe_text:
            print(f"BACKEND_READINESS_PROBE_MISSING: {token}", file=sys.stderr)
            rc = 1
    for token in (
        "--active-ini=/opt/8ax/v5/linuxcnc/ini/v5_bus.ini",
        "--expected-slaves=5",
        "--expected-wkc=10",
        "lcec.0.activated => v5-native-hal-owner.backend-activated",
        "lcec.0.domain-working-counter => v5-native-hal-owner.backend-domain-wkc",
        "lcec.0.domain-wc-complete => v5-native-hal-owner.backend-domain-wc-complete",
        "setp v5-native-hal-owner.backend-contract-wired TRUE",
    ):
        if token not in bus_hal_text:
            print(f"BACKEND_READINESS_HAL_WIRING_MISSING: {token}", file=sys.stderr)
            rc = 1
    for retired_patch in retired_lcec_patches:
        if retired_patch.exists():
            print(
                f"ETHERCAT_RETIRED_STARTUP_PATCH_SURVIVOR: {retired_patch.relative_to(ROOT)}",
                file=sys.stderr,
            )
            rc = 1
    lcec_recipe_text = lcec_recipe.read_text(encoding="utf-8", errors="strict")
    for token in (
        "file://0002-v5-dc-reference-health-pins.patch",
        "file://0003-v5-require-initf-master-activation.patch",
    ):
        if token not in lcec_recipe_text:
            print(f"ETHERCAT_INITF_ACTIVATION_MISSING: {token}", file=sys.stderr)
            rc = 1
    for forbidden in (
        "file://0003-v5-defer-master-activation-to-first-read.patch",
        "file://0004-v5-resident-runtime-ready-pin.patch",
        "initf_supported = 0",
        "using lcec module-load activation",
    ):
        if forbidden in lcec_recipe_text:
            print(f"ETHERCAT_RETIRED_STARTUP_PATCH_REFERENCE: {forbidden}", file=sys.stderr)
            rc = 1
    lcec_initf_text = lcec_initf_patch.read_text(encoding="utf-8", errors="strict")
    lcec_initf_added = "\n".join(
        line[1:] for line in lcec_initf_text.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    )
    for token in (
        "V5 initf special-cycle activation complete",
        "activation_failed",
        "initf_missing_warned",
        "cyclic EtherCAT I/O remains blocked",
    ):
        if token not in lcec_initf_added:
            print(f"ETHERCAT_INITF_FAIL_CLOSED_TOKEN_MISSING: {token}", file=sys.stderr)
            rc = 1
    for forbidden in (
        "initf_supported",
        "Falling back to inline activation",
    ):
        if forbidden in lcec_initf_added:
            print(f"ETHERCAT_INITF_FALLBACK_SURVIVOR: {forbidden}", file=sys.stderr)
            rc = 1
    bus_hal_text = bus_hal.read_text(encoding="utf-8", errors="strict")
    load_pos = bus_hal_text.find("loadrt lcec")
    initf_pos = bus_hal_text.find("initf lcec.activate servo-thread")
    read_pos = bus_hal_text.find("addf lcec.read-all servo-thread")
    if load_pos < 0 or initf_pos <= load_pos or read_pos <= initf_pos:
        print("ETHERCAT_INITF_HAL_ORDER_MISSING", file=sys.stderr)
        rc = 1
    linuxcnc_initf_text = "\n".join(
        path.read_text(encoding="utf-8", errors="strict")
        for path in linuxcnc_initf_sources
    )
    for token in (
        "hal_init_funct_to_thread",
        "init_funct_list",
        "rtapi_task_self_resync",
        '"initf"',
        "do_initf_cmd",
    ):
        if token not in linuxcnc_initf_text:
            print(f"LINUXCNC_INITF_SOURCE_TOKEN_MISSING: {token}", file=sys.stderr)
            rc = 1
    ethercat_recipe_text = ethercat_recipe.read_text(encoding="utf-8", errors="strict")
    if 'INITSCRIPT_PARAMS_${PN} = "stop 10 0 6 ."' not in ethercat_recipe_text:
        print("ETHERCAT_SELECTLINK_STOP_ONLY_INIT_POLICY_MISSING", file=sys.stderr)
        rc = 1
    if 'INITSCRIPT_PARAMS_${PN} = "start 90 5 .' in ethercat_recipe_text:
        print("ETHERCAT_UNCONDITIONAL_AUTOSTART_SURVIVOR", file=sys.stderr)
        rc = 1
    if "HALUI = halui" in bus_ini.read_text(
            encoding="utf-8", errors="ignore"):
        print(
            "POSITION_DISPLAY_BACKGROUND_HALUI_SURVIVOR: linuxcnc/ini/v5_bus.ini",
            file=sys.stderr,
        )
        rc = 1
    motion_private_text = motion_private.read_text(encoding="utf-8", errors="ignore")
    motion_export_text = motion_export.read_text(encoding="utf-8", errors="ignore")
    motion_control_text = motion_control.read_text(encoding="utf-8", errors="ignore")
    for token, source_text in (
        ("hal_float_t *feed_override", motion_private_text),
        ("hal_float_t *spindle_override", motion_private_text),
        ('"motion.feed-override"', motion_export_text),
        ('"spindle.%d.override"', motion_export_text),
        ("*(emcmot_hal_data->feed_override) = emcmotStatus->feed_scale;", motion_control_text),
        ("emcmotStatus->spindle_status[spindle_num].scale;", motion_control_text),
    ):
        if token not in source_text:
            print(
                f"POSITION_DISPLAY_NATIVE_OVERRIDE_PIN_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    board_runtime_text = board_runtime_policy.read_text(encoding="utf-8", errors="ignore")
    for token in (
        "audit_linuxcnc_privileged_helpers",
        "OK_LINUXCNC_RTAPI_SCHEDULER",
        "no rtapi_app:T realtime thread found",
        "tail[38]",
    ):
        if token not in board_runtime_text:
            print(f"LINUXCNC_RTAPI_BOARD_AUDIT_MISSING: {token}", file=sys.stderr)
            rc = 1
    if 'echo 0 >"/proc/irq/$irq/smp_affinity_list"' in text:
        print("LINUXCNC_DUPLICATE_NETWORK_IRQ_WRITER_PRESENT", file=sys.stderr)
        rc = 1
    status_reset_tokens = (
        "LinuxCNC backend residue remained before native status reset",
        "/dev/shm/v5_native_safety_latch.bin",
        "/dev/shm/v5_native_rtcp_status.bin",
        "/dev/shm/v5_native_g53_geometry_status.bin",
    )
    for token in status_reset_tokens:
        if token not in text:
            print(f"LINUXCNC_NATIVE_STATUS_RESET_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    status_reset_index = text.find("/dev/shm/v5_native_safety_latch.bin")
    backend_start_index = text.find('su petalinux -c "cd /opt/8ax/v5/linuxcnc/ini')
    if status_reset_index < 0 or backend_start_index < 0 or status_reset_index >= backend_start_index:
        print("LINUXCNC_NATIVE_STATUS_RESET_ORDER_INVALID", file=sys.stderr)
        rc = 1
    for token in ("ensure_machine_on_at_boot", "--machine-on"):
        if token in text:
            print(f"LINUXCNC_MACHINE_ON_EARLY_INIT_SURVIVOR: {init.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    ui_text = ui_init.read_text(encoding="utf-8", errors="ignore")
    required_ui = (
        "wait_boot_inputs_ready",
        "boot_stage ui_ready",
        "UI_INPUT_NICE=0",
        'nice -n "$UI_INPUT_NICE"',
        'renice -n "$UI_INPUT_NICE" -p "$pid"',
        "set_display_irq_affinity",
    )
    for token in required_ui:
        if token not in ui_text:
            print(f"LINUXCNC_MACHINE_ON_UI_INIT_CONTRACT_MISSING: {ui_init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    for token in ("default_route_iface", "set_non_microkernel_irq_affinity"):
        if token in ui_text:
            print(f"UI_DUPLICATE_NETWORK_AFFINITY_OWNER_PRESENT: {token}", file=sys.stderr)
            rc = 1
    if 'for task in /proc/"$pid"/task/[0-9]*' in ui_text:
        print("UI_FRAME_PRODUCER_NICE_OVERRIDE_SURVIVOR", file=sys.stderr)
        rc = 1
    net_text = net_core.read_text(encoding="utf-8", errors="ignore")
    required_net = (
        "apply_network_cpu_isolation",
        "set_iface_irq_affinity",
        "set_iface_queue_masks",
        "disable_irqbalance",
        'set_iface_irq_affinity "$ethercat_iface" 0 1',
        'set_iface_queue_masks "$ethercat_iface" 0 1',
        'set_iface_irq_affinity "$management_iface" 1 1',
        'set_iface_queue_masks "$management_iface" 2 2',
        "/proc/irq/$irq/smp_affinity_list",
        "rps_cpus",
        "xps_cpus",
        "start_dropbear_cpu1",
        "set_ethercat_softirq_priority",
        '. /usr/local/sbin/v5_net_cpu_policy.sh',
    )
    for token in required_net:
        if token not in net_text:
            print(f"NETWORK_CPU_ISOLATION_OWNER_MISSING: {net_core.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    net_cpu_policy_text = net_cpu_policy.read_text(encoding="utf-8", errors="strict")
    for token in (
        "dropbear_cpu1_affinity_ok",
        "enforce_dropbear_cpu1_affinity",
        '/usr/bin/taskset -c 1 "$DROPBEAR_BIN"',
        '/usr/bin/taskset -a -pc 1 "$v5_dropbear_pid"',
        "dropbear stopped because CPU1 affinity could not be guaranteed",
        "ETHERCAT_SOFTIRQ_PRIORITY=49",
        '/usr/bin/chrt -f -p "$ETHERCAT_SOFTIRQ_PRIORITY"',
        'v5_softirq_policy=$(awk \'{print $41}\'',
        'v5_softirq_priority=$(awk \'{print $40}\'',
    ):
        if token not in net_cpu_policy_text:
            print(
                f"NETWORK_CPU_POLICY_MODULE_MISSING: {net_cpu_policy.relative_to(ROOT)} lacks {token}",
                file=sys.stderr,
            )
            rc = 1
    net_init_text = net_init.read_text(encoding="utf-8", errors="strict")
    required_net_init = (
        "bind_network_init_to_cpu1() {",
        'done <"/proc/$$/status"',
        '[ "$cpu_list" = "1" ] && return 0',
        'exec /usr/bin/taskset -c 1 "$0" "$@"',
        'bind_network_init_to_cpu1 "$@" || exit 1',
    )
    for token in required_net_init:
        if token not in net_init_text:
            print(
                f"NETWORK_INIT_CPU1_REEXEC_MISSING: {net_init.relative_to(ROOT)} lacks {token}",
                file=sys.stderr,
            )
            rc = 1
    if "apply_network_cpu_isolation" not in usb_wifi_apply.read_text(encoding="utf-8", errors="ignore"):
        print("USB_WIFI_CPU_ISOLATION_OWNER_MISSING", file=sys.stderr)
        rc = 1
    for publisher_init in publisher_inits:
        publisher_text = publisher_init.read_text(encoding="utf-8", errors="ignore")
        for token in (
            "NON_MICROKERNEL_NICE=10",
            'taskset -c 1 nice -n "$NON_MICROKERNEL_NICE"',
        ):
            if token not in publisher_text:
                print(
                    f"DISPLAY_PUBLISHER_CPU_BUDGET_POLICY_MISSING: "
                    f"{publisher_init.relative_to(ROOT)} lacks {token}",
                    file=sys.stderr,
                )
                rc = 1
    wcs_init_text = publisher_inits[1].read_text(encoding="utf-8", errors="strict")
    for token in (
        "LINUXCNC_RUNTIME_USER=${V5_LINUXCNC_RUNTIME_USER:-petalinux}",
        'TOOL_MMAP_PATH=$LINUXCNC_PY_HOME/.tool.mmap',
        "require_tool_mmap() {",
        '[ ! -f "$TOOL_MMAP_PATH" ]',
        '[ ! -r "$TOOL_MMAP_PATH" ]',
        '[ ! -w "$TOOL_MMAP_PATH" ]',
    ):
        if token not in wcs_init_text:
            print(f"WCS_TOOL_MMAP_OWNER_GATE_MISSING: {token}", file=sys.stderr)
            rc = 1
    if '$1 == "root"' in wcs_init_text or "V5_LINUXCNC_PY_HOME" in wcs_init_text:
        print("WCS_ROOT_OR_OVERRIDE_HOME_SURVIVOR", file=sys.stderr)
        rc = 1
    position_init_text = position_publisher_init.read_text(
        encoding="utf-8", errors="ignore")
    for token in (
        "POSITION_NICE=0",
        'taskset -c 1 nice -n "$POSITION_NICE"',
        "V5_POSITION_STATUS_INTERVAL_MS:-33",
        "LOCKFILE=/run/8ax/v5_position_status_publisher.lock",
        "owner_is_live()",
        'flock -n "$LOCKFILE" true',
        "tr '\\000' '\\n'",
        'grep -Fqx "$DAEMON"',
        '/proc/$OWNER_PID/stat',
        '/proc/$OWNER_PID/cmdline',
    ):
        if token not in position_init_text:
            print(
                "POSITION_DISPLAY_PUBLISHER_CPU_POLICY_MISSING: "
                f"{position_publisher_init.relative_to(ROOT)} lacks {token}",
                file=sys.stderr,
            )
            rc = 1
    for retired in (
        "tr '\\000' ' '",
        'grep -F "$DAEMON"',
        "position_block_matches_owner()",
        "bus_block_matches_owner()",
        "RUNTIME_MODULE_ROOT=/usr/libexec/8ax",
        "PYTHONPATH=$RUNTIME_MODULE_ROOT:",
        "command -v python3",
        "POSITION_BLOCK_STRUCT",
    ):
        if retired in position_init_text:
            print(
                f"POSITION_EXACT_ARGV_POLICY_RESURRECTED: {retired}",
                file=sys.stderr,
            )
            rc = 1
    for retired in (
        'kill -0',
        'echo "$!" >"$PIDFILE"',
        'rm -f "$PIDFILE" "$STATUS_PATH"',
    ):
        if retired in position_init_text:
            print(
                f"POSITION_PIDFILE_SINGLETON_AUTHORITY_RESURRECTED: {retired}",
                file=sys.stderr,
            )
            rc = 1
    position_text = position_publisher.read_text(
        encoding="utf-8", errors="ignore")
    position_sampler_text = position_sampler.read_text(
        encoding="utf-8", errors="ignore")
    position_fast_path_text = position_text + position_sampler_text
    for forbidden in ("system(", "popen(", "v5_position_status_publisher.py"):
        if forbidden in position_text:
            print(
                f"POSITION_DISPLAY_FAST_PATH_BLOCKING_SOURCE_PRESENT: {forbidden}",
                file=sys.stderr,
            )
            rc = 1
    for retired_source in retired_position_sources:
        if retired_source.exists():
            print(
                "POSITION_RETIRED_PYTHON_SOURCE_SURVIVOR: "
                f"{retired_source.relative_to(ROOT)}",
                file=sys.stderr,
            )
            rc = 1
    for path, retired_tokens in (
        (machine_status_projection, (
            "NativeRotaryDisplayProjection", "write_position_status",
            "write_mock_position_status", "display_position_projection",
        )),
        (wcs_status_codec, (
            "pack_bus_status", "BusStatusMmapWriter", "HalBusStatusAccess",
        )),
    ):
        source_text = path.read_text(encoding="utf-8", errors="strict")
        for retired in retired_tokens:
            if retired in source_text:
                print(
                    "POSITION_RETIRED_PYTHON_BRANCH_SURVIVOR: "
                    f"{path.relative_to(ROOT)} token={retired}",
                    file=sys.stderr,
                )
                rc = 1
    board_runtime_policy_text = board_runtime_policy.read_text(
        encoding="utf-8", errors="ignore")
    for token in (
        'PROC_ROOT = "/proc"',
        'PROC_LOCKS_PATH = "/proc/locks"',
        'CMDLINE_PATH = "/proc/cmdline"',
        'ISOLATED_CPU_PATH = "/sys/devices/system/cpu/isolated"',
        "def audit_kernel_boot_cpu_layout() -> int:",
        "if isolcpus_tokens:",
        'print("OK_KERNEL_BOOT_CPU_LAYOUT isolcpus=absent isolated=empty")',
        "rc |= audit_kernel_boot_cpu_layout()",
        'POSITION_LOCK_PATH = "/run/8ax/v5_position_status_publisher.lock"',
        'POSITION_BLOCK_PATH = "/dev/shm/v5_native_position_status.bin"',
        "read_position_owner_record(pidfile)",
        "POSITION_DAEMON_PATH in argv",
        'lock_kind != "FLOCK" or lock_mode != "WRITE"',
        "position_lock_owner_pid() != owner_pid",
        "position_service_audit(pidfile)",
    ):
        if token not in board_runtime_policy_text:
            print(
                f"POSITION_LIVE_OWNER_POLICY_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    for retired in (
        '["flock", "-n", lock_path, "true"]',
        'cmdline = Path(f"/proc/{pid}/cmdline").read_bytes().replace',
    ):
        if retired in board_runtime_policy_text:
            print(
                f"POSITION_LIVE_OWNER_POLICY_RESURRECTED: {retired}",
                file=sys.stderr,
            )
            rc = 1
    for token in (
        "def audit_runtime_startup_boot_graph() -> int:",
        'RUNTIME_STARTUP_INIT = Path("/etc/init.d/v5-runtime-startup")',
        'BACKEND_READINESS_PROBE = Path("/usr/libexec/8ax/v5_backend_readiness_probe")',
        'Path(f"/etc/rc{level}.d/S05v5-runtime-startup")',
        'Path(f"/etc/rc{level}.d/K14v5-runtime-startup")',
        "FAIL_RUNTIME_SHADOW_BOOT_LINK",
        "rc |= audit_runtime_startup_boot_graph()",
        "def audit_userspace_housekeeping_affinity() -> int:",
        "FAIL_USERSPACE_HOUSEKEEPING_CPU1",
        "rc |= audit_userspace_housekeeping_affinity()",
    ):
        if token not in board_runtime_policy_text:
            print(f"RUNTIME_STARTUP_BOARD_AUDIT_MISSING: {token}", file=sys.stderr)
            rc = 1
    for token in (
        "clock_nanosleep",
        "V5_POSITION_HEARTBEAT_NS",
        "lifecycle.writer_identity",
        "hal_get_pin_value_by_name",
        "motion.feed-override",
        "spindle.0.override",
        "motion.current-vel",
        "spindle.0.speed-cmd-rps",
    ):
        if token not in position_fast_path_text:
            print(
                f"POSITION_DISPLAY_FAST_PATH_CONTRACT_MISSING: {token}",
                file=sys.stderr,
            )
            rc = 1
    relay_producer_text = relay_producer.read_text(encoding="utf-8", errors="ignore")
    for token in (
        "FRAME_PRODUCER_NICE = 10",
        "ensure_frame_producer_priority",
        "os.setpriority",
        "dirty_payload_shared_producer_priority_errors",
    ):
        if token not in relay_producer_text:
            print(f"REMOTE_FRAME_PRODUCER_PRIORITY_POLICY_MISSING: {token}", file=sys.stderr)
            rc = 1
    for token in ("threading.get_native_id", "/proc/self/task/"):
        if token in relay_producer_text:
            print(f"REMOTE_FRAME_PRODUCER_UNSUPPORTED_THREAD_ID_SURVIVOR: {token}", file=sys.stderr)
            rc = 1
    manifest_text = deploy_manifest.read_text(encoding="utf-8", errors="ignore")
    required_cpu_policy_rows = (
        "binary\tbuild/board/app/v5_backend_readiness_probe\t/usr/libexec/8ax/v5_backend_readiness_probe\t0755",
        "binary\tbuild/board/app/v5_position_status_publisher\t/usr/libexec/8ax/v5_position_status_publisher\t0755",
        "services/state_publisher/v5_polling_cadence.py\t/usr/libexec/8ax/v5_polling_cadence.py\t0644",
        "services/state_publisher/init.d/v5-position-status-publisher\t/etc/init.d/v5-position-status-publisher\t0755",
        "services/ui/v5_remote_ui_shared_payload.py\t/usr/libexec/8ax/v5_remote_ui_shared_payload.py\t0644",
        "services/ui/v5_remote_ui_dirty_geometry.py\t/usr/libexec/8ax/v5_remote_ui_dirty_geometry.py\t0644",
        "services/ui/v5_status_shm_reader.py\t/usr/libexec/8ax/v5_status_shm_reader.py\t0644",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_net_core.sh\t/usr/local/sbin/v5_net_core.sh\t0644",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_net_cpu_policy.sh\t/usr/local/sbin/v5_net_cpu_policy.sh\t0644",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/S99v5-net\t/etc/init.d/S99v5-net\t0755",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_usb_wifi_apply.sh\t/usr/local/sbin/v5_usb_wifi_apply.sh\t0755",
        "services/runtime_startup/init.d/v5-runtime-startup\t/etc/init.d/v5-runtime-startup\t0755",
    )
    for row in required_cpu_policy_rows:
        if row not in manifest_text:
            print(f"CPU_POLICY_DEPLOY_ROW_MISSING: {row}", file=sys.stderr)
            rc = 1
    required_shm_abi_rows = (
        "binary\tbuild/board/app/v5_lvgl_shell\t/usr/libexec/8ax/v5_lvgl_shell\t0755",
        "binary\tbuild/board/app/v5_state_publisher\t/usr/libexec/8ax/v5_state_publisher\t0755",
        "binary\tbuild/board/app/v5_position_status_publisher\t/usr/libexec/8ax/v5_position_status_publisher\t0755",
        "script\tservices/state_publisher/v5_wcs_status_publisher.py\t/usr/libexec/8ax/v5_wcs_status_publisher.py\t0755",
        "module\tservices/state_publisher/v5_polling_cadence.py\t/usr/libexec/8ax/v5_polling_cadence.py\t0644",
        "module\tservices/state_publisher/v5_machine_status_projection.py\t/usr/libexec/8ax/v5_machine_status_projection.py\t0644",
        "module\tservices/state_publisher/v5_wcs_status_codec.py\t/usr/libexec/8ax/v5_wcs_status_codec.py\t0644",
        "script\tservices/ui/v5_remote_ui_relay.py\t/usr/libexec/8ax/v5_remote_ui_relay.py\t0755",
        "module\tservices/ui/v5_remote_ui_relay_access.py\t/usr/libexec/8ax/v5_remote_ui_relay_access.py\t0644",
        "module\tservices/ui/v5_remote_ui_relay_stream.py\t/usr/libexec/8ax/v5_remote_ui_relay_stream.py\t0644",
        "script\tservices/ui/v5_ui_boot_ready.py\t/usr/libexec/8ax/v5_ui_boot_ready.py\t0755",
        "module\tservices/ui/v5_ui_boot_inputs.py\t/usr/libexec/8ax/v5_ui_boot_inputs.py\t0644",
        "module\tservices/ui/v5_status_shm_reader.py\t/usr/libexec/8ax/v5_status_shm_reader.py\t0644",
        "init\tservices/state_publisher/init.d/v5-position-status-publisher\t/etc/init.d/v5-position-status-publisher\t0755",
        "init\tservices/state_publisher/init.d/v5-wcs-status-publisher\t/etc/init.d/v5-wcs-status-publisher\t0755",
        "init\tservices/state_publisher/init.d/v5-state-publisher\t/etc/init.d/v5-state-publisher\t0755",
        "init\tservices/ui/init.d/v5-ui-relay\t/etc/init.d/v5-ui-relay\t0755",
    )
    for row in required_shm_abi_rows:
        if manifest_text.splitlines().count(row) != 1:
            print(f"SHM_ABI_ATOMIC_DEPLOY_ROW_INVALID: {row}", file=sys.stderr)
            rc = 1
    required_ethercat_rows = (
        "kernel_module\tbuild/ethercat/ec_master.ko\t/lib/modules/5.4.0-rt7-rt1-xilinx-v2020.2/ethercat/master/ec_master.ko\t0644",
        "kernel_module\tbuild/ethercat/ec_generic.ko\t/lib/modules/5.4.0-rt7-rt1-xilinx-v2020.2/ethercat/devices/ec_generic.ko\t0644",
        "binary\tbuild/ethercat/lcec.so\t/usr/lib/linuxcnc/modules/lcec.so\t0644",
        "module\tservices/command_gate/v5_ethercat_backend_lifecycle.sh\t/usr/libexec/8ax/v5_ethercat_backend_lifecycle.sh\t0644",
    )
    for row in required_ethercat_rows:
        if manifest_text.splitlines().count(row) != 1:
            print(f"ETHERCAT_ATOMIC_DEPLOY_ROW_INVALID: {row}", file=sys.stderr)
            rc = 1
    installer_text = runtime_installer.read_text(encoding="utf-8", errors="ignore")
    required_cpu_policy_installer = (
        "manifest_cpu_policy_only=1",
        "manifest_cpu_policy_net_module=0",
        "manifest_cpu_policy_net_module=1",
        "manifest_cpu_policy_net_init=0",
        "manifest_cpu_policy_net_init=1",
        "manifest_cpu_policy_position=0",
        "manifest_cpu_policy_runtime_startup=0",
        "manifest_cpu_policy_runtime_startup=1",
        "restart_scope=cpu_policy",
        'LOG=/run/8ax/v5_cpu_policy.log',
        "apply_cpu_policy_after_install()",
        "apply_network_cpu_isolation",
        "enforce_dropbear_cpu1_affinity",
        "restart_runtime_event_dag()",
        "/etc/init.d/v5-runtime-startup restart",
        "stop_position_publisher_before_backend",
        "stop_backend_publishers_before_backend",
        "manifest_wcs_publisher=0",
        "manifest_state_publisher=0",
        "manifest_state_publisher=1",
        "stop_writer_before_upgrade()",
        "stop_affected_writers_before_install",
        'PROC_ROOT=/proc',
        '"$PROC_ROOT"/[0-9]*/cmdline',
        'grep -Fqx "$writer_path"',
        "/usr/libexec/8ax/v5_state_publisher",
        "v5-state-publisher",
        "writer did not stop before upgrade",
        "manifest_shm_abi_touched=0",
        "manifest_shm_abi_complete=0",
        "manifest_shm_abi_required_rows=18",
        "SHM ABI deploy requires the complete Position/State/UI atomic bundle",
        "manifest_ethercat_required_rows=4",
        "manifest_ethercat_touched=0",
        "manifest_ethercat_lcec=0",
        "manifest_ethercat_lifecycle=0",
        "manifest_bus_cycle_touched=0",
        "manifest_bus_cycle_complete=0",
        "manifest_bus_cycle_ini=0",
        "manifest_bus_cycle_hal=0",
        "manifest_bus_cycle_xml=0",
        "manifest_bus_cycle_readiness_probe=0",
        "manifest_bus_cycle_command_gate_init=0",
        "binary:build/ethercat/lcec.so:0644",
        "module:services/command_gate/v5_ethercat_backend_lifecycle.sh:0644",
        "EtherCAT deploy requires the complete ec_master/ec_generic/lcec/lifecycle atomic bundle",
        "BUS 1ms cycle deploy requires INI/HAL/XML, current LinuxCNC owner, complete EtherCAT, readiness/Command Gate, and UI/SHM atomic domains",
        "LinuxCNC/Command Gate process remained active before atomic replacement",
        "EtherCAT restart scope requires exactly the registered ec_master/ec_generic/lcec/lifecycle bundle",
        'if [ "$apply" -eq 1 ] && [ "$manifest_ethercat_complete" -eq 1 ]; then',
        "stop_ethercat_modules_before_install",
        "depmod -a",
        "restart_scope=shm_abi",
        "stop_shm_abi_domain_before_install",
        "wait_position_shm_abi_readback",
        "V5_POSITION_ABI_READBACK_OK",
        "wait_state_shm_abi_readback",
        "V5_STATE_ABI_READBACK_OK",
        "start_shm_abi_domain_after_install",
        "LinuxCNC realtime owner did not recover before SHM domain start",
        "V5_SHM_ABI_ATOMIC_RESTART_OK scope=position,state,ui-relay",
    )
    for token in required_cpu_policy_installer:
        if token not in installer_text:
            print(f"CPU_POLICY_DEPLOY_SCOPE_MISSING: {token}", file=sys.stderr)
            rc = 1
    required_native_protocol_installer = (
        "manifest_native_protocol_command_gate=0",
        "manifest_native_protocol_command_gate=1",
        "manifest_native_protocol_zero_client=0",
        "manifest_native_protocol_zero_client=1",
        "native protocol deploy requires Command Gate server, Python zero client, and LinuxCNC owner/router as one atomic bundle",
        "$linuxcnc_package_root/usr/bin/v5_native_hal_owner",
        "$linuxcnc_package_root/usr/lib/linuxcnc/modules/v5_bus_axis_router.so",
        "LinuxCNC deploy bundle is missing native protocol owner",
    )
    for token in required_native_protocol_installer:
        if token not in installer_text:
            print(f"NATIVE_PROTOCOL_ATOMIC_DEPLOY_MISSING: {token}", file=sys.stderr)
            rc = 1
    stop_backend_start = installer_text.find("stop_backend_publishers_before_backend() {")
    stop_backend_end = installer_text.find("\n}\n", stop_backend_start)
    stop_backend_block = (
        installer_text[stop_backend_start:stop_backend_end]
        if stop_backend_start >= 0 and stop_backend_end > stop_backend_start else ""
    )
    runtime_dag_start = installer_text.find("restart_runtime_event_dag() {")
    runtime_dag_end = installer_text.find("\n}\n", runtime_dag_start)
    runtime_dag_block = (
        installer_text[runtime_dag_start:runtime_dag_end]
        if runtime_dag_start >= 0 and runtime_dag_end > runtime_dag_start else ""
    )
    if (not stop_backend_block or
            "stop_position_publisher_before_backend" not in stop_backend_block or
            "stop_wcs_publisher_before_backend" not in stop_backend_block or
            not runtime_dag_block or
            "/etc/init.d/v5-runtime-startup restart" not in runtime_dag_block or
            "v5-linuxcnc-command-gate restart" in runtime_dag_block or
            "v5-settings-actiond restart" in runtime_dag_block):
        print("RUNTIME_EVENT_DAG_RESTART_INVALID", file=sys.stderr)
        rc = 1
    writer_stop = installer_text.find("    stop_shm_abi_domain_before_install\n  else")
    manifest_install = installer_text.find(
        'while IFS="$tab" read -r kind source destination mode extra; do',
        writer_stop)
    if writer_stop < 0 or manifest_install < 0 or writer_stop >= manifest_install:
        print("RUNTIME_WRITER_STOP_BARRIER_ORDER_INVALID", file=sys.stderr)
        rc = 1
    if '"$retired_init" stop' in installer_text:
        print("RETIRED_WRITER_INIT_EXECUTION_RESURRECTED", file=sys.stderr)
        rc = 1
    cpu_scope_start = installer_text.find('elif [ "$restart_scope" = "cpu_policy" ]')
    cpu_scope_end = installer_text.find('elif [ "$restart_scope" = "settings" ]', cpu_scope_start)
    cpu_scope = installer_text[cpu_scope_start:cpu_scope_end] if cpu_scope_start >= 0 and cpu_scope_end > cpu_scope_start else ""
    if (
        not cpu_scope
        or cpu_scope.count("restart_runtime_event_dag") != 1
        or "restart-native" in cpu_scope
        or "/etc/init.d/v5-linuxcnc-command-gate restart\n" in cpu_scope
        or "/etc/init.d/v5-settings-actiond restart\n" in cpu_scope
        or cpu_scope.find("apply_cpu_policy_after_install") >
           cpu_scope.find("restart_runtime_event_dag")
    ):
        print("CPU_POLICY_SCOPE_BACKEND_RESTART_INVALID", file=sys.stderr)
        rc = 1
    all_scope_start = installer_text.find("    enable_auxiliary_boot_services\n")
    all_cpu_policy_index = installer_text.find(
        "    apply_cpu_policy_after_install\n", all_scope_start)
    all_backend_index = installer_text.find(
        "    restart_runtime_event_dag\n", all_scope_start)
    if (all_scope_start < 0 or all_cpu_policy_index < 0 or all_backend_index < 0 or
            all_cpu_policy_index > all_backend_index):
        print("ALL_SCOPE_CPU_POLICY_BEFORE_BACKEND_MISSING", file=sys.stderr)
        rc = 1
    barrier_call = "\n    wait_publisher_actual_barrier\n"
    if installer_text.count(barrier_call) != 8:
        print("PUBLISHER_ACTUAL_BARRIER_CALL_COUNT_INVALID", file=sys.stderr)
        rc = 1
    if installer_text.count("\n  wait_publisher_actual_barrier\n") != 1:
        print("SHM_ABI_ACTUAL_BARRIER_CALL_COUNT_INVALID", file=sys.stderr)
        rc = 1
    for token in (
        "wait_publisher_actual_barrier()",
        "PUBLISHER_ACTUAL_BARRIER=/usr/libexec/8ax/v5_ui_boot_ready.py",
        "PUBLISHER_SNAPSHOT_PATH=/run/8ax_v5_product_ui/ui_input_barrier.json",
        "active_ini=conflict",
        "publisher actual barrier rejects disabled Pulse runtime mode",
        '--publisher-snapshot-path "$PUBLISHER_SNAPSHOT_PATH"',
    ):
        if token not in installer_text:
            print(f"PUBLISHER_ACTUAL_BARRIER_CONTRACT_MISSING: {token}", file=sys.stderr)
            rc = 1
    scope_bounds = (
        ("backend", 'elif [ "$restart_scope" = "backend" ]',
         'elif [ "$restart_scope" = "ethercat" ]', True),
        ("ethercat", 'elif [ "$restart_scope" = "ethercat" ]',
         'elif [ "$restart_scope" = "wcs" ]', True),
        ("wcs", 'elif [ "$restart_scope" = "wcs" ]',
         'elif [ "$restart_scope" = "runtime_startup" ]', True),
        ("cpu_policy", 'elif [ "$restart_scope" = "cpu_policy" ]',
         'elif [ "$restart_scope" = "settings" ]', True),
        ("settings", 'elif [ "$restart_scope" = "settings" ]',
         "\n  else\n", True),
    )
    for name, start_token, end_token, expected in scope_bounds:
        start = installer_text.find(start_token)
        end = installer_text.find(end_token, start)
        body = installer_text[start:end] if start >= 0 and end > start else ""
        actual = body.count("wait_publisher_actual_barrier") == 1
        if not body or actual != expected:
            print(f"PUBLISHER_ACTUAL_BARRIER_SCOPE_INVALID: {name}", file=sys.stderr)
            rc = 1
    atomic_stop_start = installer_text.find("stop_shm_abi_domain_before_install()")
    atomic_stop_end = installer_text.find(
        "wait_position_shm_abi_readback()", atomic_stop_start)
    atomic_stop = installer_text[atomic_stop_start:atomic_stop_end]
    atomic_stop_order = tuple(atomic_stop.find(token) for token in (
        "/etc/init.d/v5-ui-relay stop",
        "v5-state-publisher",
        "v5-wcs-status-publisher",
        "v5-position-status-publisher",
    ))
    if (not atomic_stop or any(position < 0 for position in atomic_stop_order) or
            tuple(sorted(atomic_stop_order)) != atomic_stop_order):
        print("SHM_ABI_ATOMIC_STOP_ORDER_INVALID", file=sys.stderr)
        rc = 1
    atomic_start_start = installer_text.find("start_shm_abi_domain_after_install()")
    atomic_start_end = installer_text.find("retired_pid_matches_path()", atomic_start_start)
    atomic_start = installer_text[atomic_start_start:atomic_start_end]
    atomic_start_order = tuple(atomic_start.find(token) for token in (
        "/etc/init.d/v5-position-status-publisher start",
        "wait_position_shm_abi_readback",
        "/etc/init.d/v5-state-publisher start",
        "wait_state_shm_abi_readback",
        "/etc/init.d/v5-ui-relay start",
    ))
    if (not atomic_start or any(position < 0 for position in atomic_start_order) or
            tuple(sorted(atomic_start_order)) != atomic_start_order):
        print("SHM_ABI_ATOMIC_START_ORDER_INVALID", file=sys.stderr)
        rc = 1
    for token in (
        "drive_faults_clear",
        "wait_drive_faults_clear_for_machine_on",
        "ensure_machine_on_after_microkernel_ready",
        "--machine-on",
        "machine_on_ready",
    ):
        if token in ui_text:
            print(f"LINUXCNC_UI_INIT_AUTO_MACHINE_ON_PRESENT: {ui_init.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    probe_text = probe.read_text(encoding="utf-8", errors="ignore")
    required_probe = (
        "v5_linuxcncrsh_probe_machine",
        "linuxcncrsh machine probe ok",
    )
    for token in required_probe:
        if token not in probe_text:
            print(f"LINUXCNC_READ_ONLY_PROBE_CONTRACT_MISSING: {probe.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    for token in (
        "ensure_machine_on",
        "poll_sleep_ms",
        "--machine-on",
        "v5_linuxcncrsh_send_machine_on_sequence",
    ):
        if token in probe_text:
            print(f"LINUXCNC_PROBE_CONTROL_SURFACE_RESURRECTED: {probe.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    for token in ("LINUXCNCRSH_PORT", "LINUXCNCRSH_CONNECTPW"):
        if token in ui_text:
            print(f"LINUXCNC_UI_DEAD_PROBE_VARIABLE_RESURRECTED: {ui_init.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    if "LINUXCNCRSH_ENABLEPW" in text:
        print(f"LINUXCNC_GATE_DEAD_PROBE_VARIABLE_RESURRECTED: {init.relative_to(ROOT)} contains LINUXCNCRSH_ENABLEPW", file=sys.stderr)
        rc = 1
    verify = ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh"
    if not verify.exists():
        print("LINUXCNC_READ_ONLY_VERIFY_MISSING: tools/deploy/verify_v5_board_runtime.sh", file=sys.stderr)
        rc = 1
    else:
        verify_text = verify.read_text(encoding="utf-8", errors="ignore")
        for token in (
            "tr -d '\\\\r'",
            "grep -Eq '^MACHINE (ON|OFF)$'",
            "linuxcncrsh read-only machine-state confirmed",
        ):
            if token not in verify_text:
                print(f"LINUXCNC_READ_ONLY_VERIFY_CONTRACT_MISSING: {verify.relative_to(ROOT)} lacks {token}", file=sys.stderr)
                rc = 1
        if "linuxcncrsh machine auto-on confirmed" in verify_text:
            print(f"LINUXCNC_AUTO_ON_VERIFY_TEXT_RESURRECTED: {verify.relative_to(ROOT)}", file=sys.stderr)
            rc = 1
    return rc


def check_linuxcnc_control_transport_policy() -> int:
    rc = 0
    linuxcnc_root = ROOT.parent / "linuxcnc"
    emcrsh_path = linuxcnc_root / "src" / "emc" / "usr_intf" / "emcrsh.cc"
    common_nml_path = linuxcnc_root / "configs" / "common" / "linuxcnc.nml"
    local_nml_path = ROOT / "linuxcnc" / "ini" / "v5_local_shmem.nml"
    manifest_path = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    required_source_tokens = (
        "#include <arpa/inet.h>",
        '{"bind", 1, NULL, \'b\'}',
        'getopt_long(argc, argv, "hb:e:n:p:s:w:d:"',
        "inet_pton(AF_INET, optarg, &bind_address) != 1",
        'fprintf(stderr, "invalid bind address: %s\\n", optarg)',
        "server_address.sin_addr = bind_address",
    )
    try:
        emcrsh_text = emcrsh_path.read_text(encoding="utf-8", errors="strict")
    except OSError as exc:
        print(f"LINUXCNCRSH_BIND_SOURCE_MISSING: {exc}", file=sys.stderr)
        return 1
    for token in required_source_tokens:
        if token not in emcrsh_text:
            print(f"LINUXCNCRSH_BIND_OPTION_MISSING: {token}", file=sys.stderr)
            rc = 1
    if "server_address.sin_addr.s_addr = htonl(INADDR_ANY)" in emcrsh_text:
        print("LINUXCNCRSH_WILDCARD_BIND_RESURRECTED", file=sys.stderr)
        rc = 1

    expected_nml = common_nml_path.read_text(encoding="utf-8", errors="strict").replace(
        " TCP=5005", "")
    local_nml = local_nml_path.read_text(encoding="utf-8", errors="strict")
    nml_rows = lambda text: [" ".join(line.split()) for line in text.splitlines()
                             if line.startswith("B ") or line.startswith("P ")]
    if nml_rows(local_nml) != nml_rows(expected_nml):
        print("LINUXCNC_LOCAL_NML_BASELINE_MISMATCH", file=sys.stderr)
        rc = 1
    if "TCP=" in local_nml or re.search(r"\bREMOTE\b", local_nml):
        print("LINUXCNC_LOCAL_NML_REMOTE_TRANSPORT_RESURRECTED", file=sys.stderr)
        rc = 1

    expected_path = "/opt/8ax/v5/linuxcnc/ini/v5_local_shmem.nml"
    for name in ("v5_bus.ini", "v5_pulse.ini"):
        ini_path = ROOT / "linuxcnc" / "ini" / name
        parser = configparser.ConfigParser(strict=False, interpolation=None)
        parser.read(ini_path, encoding="utf-8")
        if parser.get("EMC", "NML_FILE", fallback="") != expected_path:
            print(f"LINUXCNC_LOCAL_NML_INI_MISSING: {name}", file=sys.stderr)
            rc = 1
        display = parser.get("DISPLAY", "DISPLAY", fallback="").split()
        if display.count("--bind") != 1 or "--bind" not in display or display[display.index("--bind") + 1:display.index("--bind") + 2] != ["127.0.0.1"]:
            print(f"LINUXCNCRSH_LOOPBACK_BIND_MISSING: {name}", file=sys.stderr)
            rc = 1

    rows = [line.split("\t") for line in manifest_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    matches = [row for row in rows if len(row) == 4 and row[2] == expected_path]
    if matches != [["linuxcnc", "linuxcnc/ini/v5_local_shmem.nml", expected_path, "0644"]]:
        print("LINUXCNC_LOCAL_NML_MANIFEST_INVALID", file=sys.stderr)
        rc = 1
    return rc


def audit_uio_device_permission_sources(sources) -> list[str]:
    failures = []
    product_sources = {name: sources[name] for name in ("init", "stepgen", "dna")}
    for name, text in product_sources.items():
        fixed = re.search(r"/dev/uio[0-9]+\b", text)
        if fixed:
            failures.append(f"UIO_FIXED_DEVICE_PATH_RESURRECTED:{name}:{fixed.group(0)}")
        chmod = re.search(r"\bchmod\s+0?666\b", text, re.IGNORECASE)
        if chmod:
            failures.append(f"UIO_WORLD_CHMOD_RESURRECTED:{name}")
    expected_rules = {
        'SUBSYSTEM=="uio", KERNEL=="uio*", ATTR{name}=="*stepgen*", SYMLINK+="v5-stepgen-uio", OWNER="root", GROUP="petalinux", MODE="0660"',
        'SUBSYSTEM=="uio", KERNEL=="uio*", ATTR{name}=="*dna*", SYMLINK+="v5-dna-uio", OWNER="root", GROUP="root", MODE="0600"',
    }
    rules = [line.strip() for line in sources["rules"].splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if len(rules) != 2 or set(rules) != expected_rules or any(rules.count(rule) != 1 for rule in expected_rules):
        failures.append("UIO_UDEV_RULESET_INVALID")
    for token in ("dialout", 'MODE="666"', 'MODE="0666"'):
        if token in "\n".join(rules):
            failures.append(f"UIO_UDEV_BROAD_PERMISSION_RESURRECTED:{token}")
    stepgen = sources["stepgen"]
    stepgen_required = (
        'V5_STEPGEN_UIO_PATH "/dev/v5-stepgen-uio"',
        "validate_stepgen_uio_device",
        "lstat(V5_STEPGEN_UIO_PATH",
        "S_ISLNK(link_info.st_mode)",
        "S_ISCHR(target_info.st_mode)",
        'getgrnam("petalinux")',
        "(target_info.st_mode & 07777) != 0660",
        "realpath(V5_STEPGEN_UIO_PATH, resolved)",
        'strncmp(resolved, "/dev", 4)',
        "identity->st_dev = target_info.st_dev",
        "identity->st_ino = target_info.st_ino",
        "identity->st_rdev = target_info.st_rdev",
        "validate_stepgen_uio_fd(fd, &uio_identity)",
        "fstat(device_fd",
        "target_info.st_dev != identity->st_dev",
        "target_info.st_ino != identity->st_ino",
        "target_info.st_rdev != identity->st_rdev",
        "close(fd);",
    )
    stepgen_start = stepgen.index("int rtapi_app_main(void)")
    stepgen_flow = stepgen[stepgen_start:]
    if (any(token not in stepgen for token in stepgen_required) or not (
            stepgen_flow.index("validate_stepgen_uio_device(uio_node") <
            stepgen_flow.index("open(uio_path") <
            stepgen_flow.index("validate_stepgen_uio_fd(fd, &uio_identity)") <
            stepgen_flow.index("mmap("))):
        failures.append("UIO_STEPGEN_STABLE_CONSUMER_MISSING")
    for token in ("V5_STEPGEN_UIO_DEVICE", "stepgen_uio_device_path"):
        if token in stepgen:
            failures.append(f"UIO_STEPGEN_PATH_OVERRIDE_RESURRECTED:{token}")
    if re.search(r"\bgetenv\s*\(", stepgen):
        failures.append("UIO_STEPGEN_PATH_OVERRIDE_RESURRECTED:getenv")
    dna = sources["dna"]
    dna_required = (
        'DEFAULT_UIO_DEVICE = "/dev/v5-dna-uio"',
        "def read_live_dna()",
        "identity = _validate_dna_uio_path()",
        "os.lstat(DEFAULT_UIO_DEVICE)",
        "stat.S_ISLNK(link_info.st_mode)",
        "stat.S_ISCHR(target_info.st_mode)",
        'os.path.dirname(resolved) != "/dev"',
        're.fullmatch(r"uio[0-9]+"',
        "target_info.st_mode & 0o7777",
        "return target_info.st_dev, target_info.st_ino, target_info.st_rdev",
        "_validate_dna_uio_fd(fd, identity)",
        "os.fstat(fd)",
        "(target_info.st_dev, target_info.st_ino, target_info.st_rdev) != identity",
        "finally:",
        "os.close(fd)",
    )
    dna_start = dna.find("def read_live_dna()")
    dna_flow = dna[dna_start:] if dna_start >= 0 else ""
    dna_order_tokens = (
        "identity = _validate_dna_uio_path()", "os.open(",
        "_validate_dna_uio_fd(fd, identity)", "_read_live_dna_fd(fd)",
    )
    if (any(token not in dna for token in dna_required) or
            any(token not in dna_flow for token in dna_order_tokens) or not (
            dna_flow.index("identity = _validate_dna_uio_path()") <
            dna_flow.index("os.open(") <
            dna_flow.index("_validate_dna_uio_fd(fd, identity)") <
            dna_flow.index("_read_live_dna_fd(fd)"))):
        failures.append("UIO_DNA_STABLE_CONSUMER_MISSING")
    if (re.search(r"def\s+read_live_dna\s*\([^)]*(?:path|device)", dna) or
            re.search(r"\bos\.getenv\s*\(|\bos\.environ\b", dna)):
        failures.append("UIO_DNA_PATH_OVERRIDE_RESURRECTED")
    board_policy = sources["board_policy"]
    match = re.search(r"^def audit_uio_devices\(\) -> int:\n(.*?)(?=^def )", board_policy,
                      re.MULTILINE | re.DOTALL)
    audit_body = match.group(1) if match else ""
    for token in (
        "validate_uio_records(records, petalinux_gid)",
        'rtapi_pids = pids_by_executable_name("rtapi_app")',
        'actiond_pids = pids_by_exact_argv_element(SETTINGS_ACTIOND_DAEMON_PATH)',
        'actiond_pidfile_pid = read_pid(PIDFILES["v5_settings_actiond"])',
        'consumer_ids["stepgen"] = read_fs_ids(rtapi_pids[0])',
        'consumer_ids["dna"] = read_fs_ids(actiond_pids[0])',
        "FAIL_UIO_STEPGEN_CONSUMER_MISSING",
        "FAIL_UIO_DNA_CONSUMER_MISSING",
        "FAIL_UIO_DNA_CONSUMER_COUNT",
        "FAIL_UIO_DNA_CONSUMER_PIDFILE_MISMATCH",
        "FAIL_UIO_UNEXPECTED_WRITABLE",
    ):
        if token not in board_policy:
            failures.append(f"UIO_LIVE_POLICY_MISSING:{token}")
    if not re.search(
            r"failures\.extend\(validate_uio_consumer_fsids\(\s*consumer_ids,\s*petalinux\.pw_uid,\s*petalinux_gid\)\)",
            audit_body,
            re.DOTALL):
        failures.append("UIO_LIVE_POLICY_MISSING:combined_consumer_validator_call")
    if not re.search(r"(?m)^\s*rc\s*\|=\s*audit_uio_devices\(\)\s*$", board_policy):
        failures.append("UIO_LIVE_POLICY_MISSING:rc |= audit_uio_devices()")
    return failures


def check_uio_device_permission_policy() -> int:
    paths = {
        "init": ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate",
        "rules": ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" / "v5-base-overlay" / "files" / "udev" / "99-uio.rules",
        "stepgen": ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" / "v5-stepgen-module" / "files" / "zynq_stepgen_hw.c",
        "dna": ROOT / "services" / "auth_download" / "device_dna_register_hardware.py",
        "board_policy": ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py",
    }
    sources = {name: path.read_text(encoding="utf-8", errors="strict")
               for name, path in paths.items()}
    failures = audit_uio_device_permission_sources(sources)
    for failure in failures:
        print(failure, file=sys.stderr)
    return int(bool(failures))


def audit_ethercat_device_permission_sources(sources) -> list[str]:
    failures = []
    recipe = sources["recipe"]
    for token in (
        "v5_ethercat_count_exact()",
        'awk -v v5_line="$1"',
        '[ -f "$v5_ethercat_init" ] || bbfatal',
        "v5_ethercat_permission_fail()",
        "$ETHERCATCTL stop",
        "exit 1",
        "v5_ethercat_apply_permissions()",
        "v5_ethercat_gid=$(id -g petalinux) || v5_ethercat_permission_fail",
        'chown root:petalinux "$v5_ethercat_node" || v5_ethercat_permission_fail',
        'chmod 0660 "$v5_ethercat_node" || v5_ethercat_permission_fail',
        'stat -c "%u:%g:%a" "$v5_ethercat_node"',
        '"0:${v5_ethercat_gid}:660"',
        '[ "$v5_ethercat_found" = 1 ] || v5_ethercat_permission_fail',
        "v5_ethercat_start_count=$(v5_ethercat_count_exact",
        '[ "$v5_ethercat_start_count" -eq 1 ] || bbfatal',
        "sed -i '/^    if \\$ETHERCATCTL start; then$/a\\        v5_ethercat_apply_permissions'",
        "v5_ethercat_call_count=$(v5_ethercat_count_exact",
        '[ "$v5_ethercat_call_count" -eq 1 ] || bbfatal',
        "v5_ethercat_call_number=$(awk",
        'v5_ethercat_expected_call_number=$(awk',
        '[ "$v5_ethercat_call_number" -eq "$v5_ethercat_expected_call_number" ] || bbfatal',
    ):
        if token not in recipe:
            failures.append(f"ETHERCAT_PERMISSION_HOOK_MISSING:{token}")
    transform_start = recipe.find("    v5_ethercat_count_exact()")
    transform_anchor = recipe.find("    v5_ethercat_call_number=$(awk", transform_start)
    transform = recipe[transform_start:recipe.find("\n}", transform_anchor)]
    if "|| true" in transform:
        failures.append("ETHERCAT_PERMISSION_TRANSFORM_LENIENT_FALLBACK")
    for name, text in sources.items():
        if re.search(r"\bchmod\s+0?666\b[^\n]*/dev/EtherCAT(?:\*|[0-9]+)", text):
            failures.append(f"ETHERCAT_WORLD_WRITABLE_RESURRECTED:{name}")
        fixed = re.search(r"/dev/EtherCAT[0-9]+\b", text)
        if fixed:
            failures.append(f"ETHERCAT_FIXED_DEVICE_PATH_RESURRECTED:{name}:{fixed.group(0)}")
        if name != "recipe" and any(
                re.search(r"(?:/dev/)?EtherCAT", line) and
                re.search(r"(?:\bchmod\b|\bchown\b|MODE\s*=)", line)
                for line in text.splitlines()):
            failures.append(f"ETHERCAT_SECOND_PERMISSION_OWNER:{name}")
    board_policy = sources["board_policy"]
    for token in (
        "validate_ethercat_records(records, petalinux_gid)",
        "FAIL_ETHERCAT_DEVICE_MISSING",
        "FAIL_ETHERCAT_DEVICE_MODE",
        "rc |= audit_ethercat_devices()",
    ):
        if token not in board_policy:
            failures.append(f"ETHERCAT_LIVE_POLICY_MISSING:{token}")
    return failures


def check_ethercat_device_permission_policy() -> int:
    paths = {
        "recipe": ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-kernel" / "ethercat-master" / "ethercat-master_git.bb",
        "rules": ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps" / "v5-base-overlay" / "files" / "udev" / "99-uio.rules",
        "init": ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate",
        "board_policy": ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py",
    }
    failures = audit_ethercat_device_permission_sources({
        name: path.read_text(encoding="utf-8", errors="strict") for name, path in paths.items()
    })
    for failure in failures:
        print(failure, file=sys.stderr)
    return int(bool(failures))


def check_tcf_retirement_policy() -> int:
    rc = 0
    verifier_path = ROOT / "tools" / "petalinux" / "verify_v5_petalinux_source.py"
    spec = importlib.util.spec_from_file_location("v5_tcf_production_closure_policy", verifier_path)
    if spec is None or spec.loader is None:
        print("TCF_PRODUCTION_CLOSURE_IMPORT_FAILED", file=sys.stderr)
        return 1
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    project_root = ROOT.parent
    failures = verifier.audit_tcf_production_closure(
        ROOT / "petalinux" / "project-spec" / "configs" / "rootfs_config",
        ROOT / "petalinux" / "v5_bitbake_source_inventory.json",
        ROOT / "third_party" / "petalinux-source-packages" / "v5_source_packages.json",
        ROOT / "third_party" / "petalinux-source-packages" / verifier.TCF_ARCHIVE_NAME,
    )
    for failure in failures:
        print(f"TCF_PRODUCTION_CLOSURE_FAIL:{failure}", file=sys.stderr)
        rc = 1
    requirements = {
        verifier_path: (
            "def audit_tcf_production_closure(",
            "def validate_tcf_production_closure(project_root, source_root):",
            "validate_tcf_production_closure(project_root, source_root)",
        ),
        ROOT / "tools" / "deploy" / "check_v5_board_runtime_policy.py": (
            "def audit_tcf_retirement() -> int:",
            'TCF_EXECUTABLE_NAME = "tcf-agent"',
            "TCF_LISTENER_PORT = 1534",
            "def collect_tcf_proc_evidence(proc_root: Path):",
            "FAIL_TCF_ROOTFS_MANIFEST",
        ),
        ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh": (
            "production TCF files, services, processes, and port 1534 absent",
            "tcf_absence_check=",
        ),
    }
    for path, tokens in requirements.items():
        text = path.read_text(encoding="utf-8", errors="strict")
        for token in tokens:
            if token not in text:
                print(f"TCF_RETIREMENT_GATE_MISSING:{path}:{token}", file=sys.stderr)
                rc = 1
    return rc


def check_bus_1ms_cycle_policy() -> int:
    bus_ini = ROOT / "linuxcnc" / "ini" / "v5_bus.ini"
    pulse_ini = ROOT / "linuxcnc" / "ini" / "v5_pulse.ini"
    bus_hal = ROOT / "linuxcnc" / "hal" / "v5_bus_1ms.hal"
    ethercat = ROOT / "linuxcnc" / "hal" / "ethercat-conf-1ms.xml"
    retired = (
        ROOT / "linuxcnc" / "hal" / "v5_bus_2ms.hal",
        ROOT / "linuxcnc" / "hal" / "ethercat-conf-2ms.xml",
    )
    safety = ROOT.parent / "linuxcnc" / "src" / "hal" / "components" / "v5_safety_latch.comp"
    home = ROOT.parent / "linuxcnc" / "src" / "hal" / "components" / "v5_bus_homecomp.comp"
    manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    installer = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    stress = ROOT / "tools" / "linuxcnc" / "v5_cpu1_motion_stress.py"
    runtime_verify = ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh"
    cold_boot_measure = ROOT / "tools" / "deploy" / "measure_v5_cold_boot.py"
    lcec_recipe = (
        ROOT / "petalinux" / "project-spec" / "meta-user" / "recipes-apps"
        / "linuxcnc-ethercat" / "linuxcnc-ethercat_git.bb"
    )
    lcec_health_patch = (
        lcec_recipe.parent / "files"
        / "0004-v5-runtime-cycle-health-sticky-pins.patch"
    )
    required = (
        bus_ini, pulse_ini, bus_hal, ethercat, safety, home, manifest,
        installer, stress, runtime_verify, cold_boot_measure, lcec_recipe,
        lcec_health_patch,
    )
    missing = [str(path.relative_to(ROOT.parent)) for path in required if not path.exists()]
    if missing:
        print("BUS_1MS_REQUIRED_SOURCE_MISSING:" + ",".join(missing), file=sys.stderr)
        return 1
    survivors = [str(path.relative_to(ROOT)) for path in retired if path.exists()]
    if survivors:
        print("BUS_1MS_RETIRED_SOURCE_SURVIVOR:" + ",".join(survivors), file=sys.stderr)
        return 1

    ini_text = bus_ini.read_text(encoding="utf-8", errors="strict")
    pulse_text = pulse_ini.read_text(encoding="utf-8", errors="strict")
    hal_text = bus_hal.read_text(encoding="utf-8", errors="strict")
    xml_text = ethercat.read_text(encoding="utf-8", errors="strict")
    safety_text = safety.read_text(encoding="utf-8", errors="strict")
    home_text = home.read_text(encoding="utf-8", errors="strict")
    manifest_text = manifest.read_text(encoding="utf-8", errors="strict")
    installer_text = installer.read_text(encoding="utf-8", errors="strict")
    stress_text = stress.read_text(encoding="utf-8", errors="strict")
    runtime_verify_text = runtime_verify.read_text(encoding="utf-8", errors="strict")
    cold_boot_measure_text = cold_boot_measure.read_text(
        encoding="utf-8", errors="strict"
    )
    lcec_recipe_text = lcec_recipe.read_text(encoding="utf-8", errors="strict")
    lcec_health_patch_text = lcec_health_patch.read_text(
        encoding="utf-8", errors="strict"
    )

    contracts = (
        (ini_text, r"^SERVO_PERIOD\s*=\s*1000000\s*$", "BUS_SERVO_PERIOD_NOT_1MS"),
        (ini_text, r"^HALFILE\s*=\s*/opt/8ax/v5/linuxcnc/hal/v5_bus_1ms\.hal\s*$", "BUS_HAL_OWNER_NOT_1MS"),
        (ini_text, r"^ARC_BLEND_GAP_CYCLES\s*=\s*8\s*$", "BUS_ARC_BLEND_WALL_TIME_NOT_PRESERVED"),
        (pulse_text, r"^SERVO_PERIOD\s*=\s*4500000\s*$", "PULSE_SERVO_PERIOD_CHANGED_BY_BUS_SLICE"),
    )
    for text, pattern, code in contracts:
        if not re.search(pattern, text, re.MULTILINE):
            print(code, file=sys.stderr)
            return 1
    if (
        '<master idx="0" appTimePeriod="1000000" refClockSyncCycles="10">' not in xml_text
        or xml_text.count('sync0Cycle="*1" sync0Shift="0"') != 5
    ):
        print("BUS_ETHERCAT_1MS_DC_IDENTITY_INVALID", file=sys.stderr)
        return 1
    if (
        "/opt/8ax/v5/linuxcnc/hal/ethercat-conf-1ms.xml" not in hal_text
        or "v5_bus_2ms.hal" in hal_text
        or "ethercat-conf-2ms.xml" in hal_text
    ):
        print("BUS_HAL_1MS_XML_OWNER_INVALID", file=sys.stderr)
        return 1
    for text, token, code in (
        (safety_text, 'param rw u32 watchdog_cycles = 100', "BUS_SAFETY_WATCHDOG_NOT_100MS"),
        (home_text, '#define V5_HOME_STABLE_CYCLES 6u', "BUS_HOME_STABLE_WALL_TIME_NOT_PRESERVED"),
        (home_text, '#define V5_HOME_WAIT_CYCLES 1000u', "BUS_HOME_WAIT_WALL_TIME_NOT_PRESERVED"),
        (manifest_text, 'runtime_ini_cycle_merge\tlinuxcnc/ini/v5_bus.ini\t/opt/8ax/v5/linuxcnc/ini/v5_bus.ini\t0644', "BUS_1MS_INI_CYCLE_MERGE_MANIFEST_MISSING"),
        (manifest_text, 'linuxcnc\tlinuxcnc/hal/v5_bus_1ms.hal\t/opt/8ax/v5/linuxcnc/hal/v5_bus_1ms.hal\t0644', "BUS_1MS_HAL_MANIFEST_MISSING"),
        (manifest_text, 'linuxcnc\tlinuxcnc/hal/ethercat-conf-1ms.xml\t/opt/8ax/v5/linuxcnc/hal/ethercat-conf-1ms.xml\t0644', "BUS_1MS_XML_MANIFEST_MISSING"),
        (installer_text, '/opt/8ax/v5/linuxcnc/hal/v5_bus_2ms.hal', "BUS_2MS_HAL_RETIREMENT_MISSING"),
        (installer_text, '/opt/8ax/v5/linuxcnc/hal/ethercat-conf-2ms.xml', "BUS_2MS_XML_RETIREMENT_MISSING"),
        (installer_text, 'merge_runtime_bus_ini_cycle() {', "BUS_1MS_INI_CYCLE_MERGER_MISSING"),
        (installer_text, '("EMCMOT", "SERVO_PERIOD"): "1000000"', "BUS_1MS_INI_SERVO_MERGE_MISSING"),
        (installer_text, '("HAL", "HALFILE"): "/opt/8ax/v5/linuxcnc/hal/v5_bus_1ms.hal"', "BUS_1MS_INI_HAL_MERGE_MISSING"),
        (installer_text, '("TRAJ", "ARC_BLEND_GAP_CYCLES"): "8"', "BUS_1MS_INI_TRAJ_MERGE_MISSING"),
        (installer_text, '[ "$manifest_bus_cycle_touched" -eq 1 ]', "BUS_1MS_ACTIOND_STOP_GUARD_MISSING"),
        (stress_text, 'os.sched_setaffinity', "BUS_CPU1_STRESS_AFFINITY_MISSING"),
        (stress_text, '/proc/self/status', "BUS_CPU1_STRESS_READBACK_MISSING"),
        (stress_text, 'SAMPLE_INTERVAL_NS = 100_000_000', "BUS_CPU1_STRESS_100MS_SAMPLER_MISSING"),
        (stress_text, 'hal.get_value', "BUS_CPU1_STRESS_HAL_READER_MISSING"),
        (stress_text, 'linuxcnc_module.stat', "BUS_CPU1_STRESS_MOTION_READER_MISSING"),
        (stress_text, 'read_cpu_ticks', "BUS_CPU1_STRESS_CPU_TICKS_MISSING"),
        (stress_text, 'query_backend_identity(expected=identity)', "BUS_CPU1_STRESS_GENERATION_CLOSE_MISSING"),
        (stress_text, 'external command attempted inside measurement window', "BUS_CPU1_STRESS_WINDOW_COMMAND_GUARD_MISSING"),
        (stress_text, 'evidence write attempted inside measurement window', "BUS_CPU1_STRESS_WINDOW_WRITE_GUARD_MISSING"),
        (stress_text, 'sticky counter saturated before measurement window', "BUS_CPU1_STRESS_STICKY_SATURATION_GUARD_MISSING"),
        (stress_text, 'hal_module.get_info_params()', "BUS_CPU1_STRESS_TMAX_INVENTORY_MISSING"),
        (stress_text, 'previous_tmax_ns', "BUS_CPU1_STRESS_TMAX_BASELINE_MISSING"),
        (stress_text, 'kernel_fault_start', "BUS_CPU1_STRESS_KERNEL_FAULT_BASELINE_MISSING"),
        (stress_text, 'runtime_log_start', "BUS_CPU1_STRESS_RUNTIME_LOG_BASELINE_MISSING"),
        (stress_text, 'function_tmax_overrun', "BUS_CPU1_STRESS_FUNCTION_TMAX_GATE_MISSING"),
        (stress_text, 'MAX_OBSERVE_DURATION_SECONDS = 600', "BUS_CPU1_OBSERVE_10MIN_MISSING"),
        (stress_text, 'TARGET_RUNTIME_PERIOD_ERROR_NS = 100_000', "BUS_CPU1_PERIOD_WARNING_TARGET_MISSING"),
        (stress_text, 'HARD_RUNTIME_PERIOD_ERROR_NS = 200_000', "BUS_CPU1_PERIOD_HARD_LIMIT_MISSING"),
        (stress_text, 'TARGET_COMBINED_BUDGET_NS = 800_000', "BUS_CPU1_COMBINED_TARGET_MISSING"),
        (stress_text, 'HARD_COMBINED_BUDGET_NS = EXPECTED_SERVO_PERIOD_NS', "BUS_CPU1_COMBINED_HARD_LIMIT_MISSING"),
        (stress_text, 'lcec.0.runtime-period-warning-count', "BUS_CPU1_PERIOD_WARNING_STICKY_MISSING"),
        (runtime_verify_text, 'lcec.0.runtime-phase-step-max', "BUS_RUNTIME_PHASE_STEP_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.runtime-period-ns', "BUS_RUNTIME_PERIOD_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.runtime-period-error-max', "BUS_RUNTIME_PERIOD_ERROR_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.runtime-period-warning-count', "BUS_RUNTIME_PERIOD_WARNING_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.runtime-period-fault-count', "BUS_RUNTIME_PERIOD_STICKY_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.domain-wc-incomplete-count', "BUS_RUNTIME_WKC_STICKY_READBACK_MISSING"),
        (runtime_verify_text, 'lcec.0.all-op-false-count', "BUS_RUNTIME_OP_STICKY_READBACK_MISSING"),
        (runtime_verify_text, 'setp lcec.0.runtime-phase-step-max 0', "BUS_RUNTIME_PHASE_WINDOW_RESET_MISSING"),
        (runtime_verify_text, 'setp lcec.0.runtime-period-error-max 0', "BUS_RUNTIME_PERIOD_WINDOW_RESET_MISSING"),
        (runtime_verify_text, '| halcmd -s -f', "BUS_RUNTIME_SINGLE_HAL_ATTACH_MISSING"),
        (runtime_verify_text, 'test "$period" -ge 800000', "BUS_RUNTIME_PERIOD_LOW_BOUND_MISSING"),
        (runtime_verify_text, 'test "$period" -le 1200000', "BUS_RUNTIME_PERIOD_HIGH_BOUND_MISSING"),
        (runtime_verify_text, 'test "$phase" -le 200000', "BUS_RUNTIME_PHASE_HARD_GATE_MISSING"),
        (runtime_verify_text, 'test "$error" -le 200000', "BUS_RUNTIME_PERIOD_HARD_GATE_MISSING"),
        (runtime_verify_text, 'test "$combined" -lt 1000000', "BUS_RUNTIME_COMBINED_HARD_GATE_MISSING"),
        (runtime_verify_text, 'test "$fault_end" -eq "$fault_start"', "BUS_RUNTIME_PERIOD_FAULT_WINDOW_GATE_MISSING"),
        (runtime_verify_text, 'test "$wc_end" -eq "$wc_start"', "BUS_RUNTIME_WKC_FAULT_WINDOW_GATE_MISSING"),
        (runtime_verify_text, 'test "$op_end" -eq "$op_start"', "BUS_RUNTIME_OP_FAULT_WINDOW_GATE_MISSING"),
        (cold_boot_measure_text, '"runtime_period_ns"', "BUS_COLD_BOOT_PERIOD_READBACK_MISSING"),
        (cold_boot_measure_text, '"runtime_period_error_max_ns"', "BUS_COLD_BOOT_PERIOD_ERROR_READBACK_MISSING"),
        (cold_boot_measure_text, '"runtime_period_warning_count"', "BUS_COLD_BOOT_PERIOD_WARNING_READBACK_MISSING"),
        (cold_boot_measure_text, '"runtime_period_fault_count"', "BUS_COLD_BOOT_PERIOD_STICKY_READBACK_MISSING"),
        (cold_boot_measure_text, '800_000 <= int(startup.get("runtime_period_ns")', "BUS_COLD_BOOT_PERIOD_RANGE_GATE_MISSING"),
        (cold_boot_measure_text, 'combined_budget < 1_000_000', "BUS_COLD_BOOT_COMBINED_GATE_MISSING"),
        (cold_boot_measure_text, 'int(startup.get("runtime_period_fault_count") or 0) == 0', "BUS_COLD_BOOT_PERIOD_FAULT_GATE_MISSING"),
        (lcec_recipe_text, 'file://0004-v5-runtime-cycle-health-sticky-pins.patch', "BUS_LCEC_HEALTH_PATCH_RECIPE_MISSING"),
        (lcec_health_patch_text, 'startup-phase-span', "BUS_LCEC_STARTUP_PHASE_RENAME_MISSING"),
        (lcec_health_patch_text, 'runtime-phase-step-max', "BUS_LCEC_RUNTIME_PHASE_STICKY_MISSING"),
        (lcec_health_patch_text, 'runtime-period-ns', "BUS_LCEC_RUNTIME_PERIOD_MISSING"),
        (lcec_health_patch_text, 'runtime-period-error-max', "BUS_LCEC_RUNTIME_PERIOD_ERROR_STICKY_MISSING"),
        (lcec_health_patch_text, 'runtime-period-warning-count', "BUS_LCEC_RUNTIME_PERIOD_WARNING_STICKY_MISSING"),
        (lcec_health_patch_text, 'runtime-period-fault-count', "BUS_LCEC_RUNTIME_PERIOD_FAULT_STICKY_MISSING"),
        (lcec_health_patch_text, 'master->app_time_period / 5', "BUS_LCEC_RUNTIME_PERIOD_20_PERCENT_HARD_LIMIT_MISSING"),
        (lcec_health_patch_text, 'domain-wc-incomplete-count', "BUS_LCEC_WKC_STICKY_MISSING"),
        (lcec_health_patch_text, 'all-op-false-count', "BUS_LCEC_OP_STICKY_MISSING"),
    ):
        if token not in text:
            print(code, file=sys.stderr)
            return 1
    return 0


def check_rotary_native_target_policy() -> int:
    rc = 0
    bus_hal = ROOT / "linuxcnc" / "hal" / "v5_bus_1ms.hal"
    bus_ini = ROOT / "linuxcnc" / "ini" / "v5_bus.ini"
    if not bus_hal.exists() or not bus_ini.exists():
        print("ROTARY_WRAPPED_MASK_BUS_SOURCE_MISSING", file=sys.stderr)
        rc = 1
    else:
        hal_text = bus_hal.read_text(encoding="utf-8", errors="ignore")
        registry_path = ROOT / "services" / "command_gate" / "v5_motion_model_registry.h"
        registry_text = registry_path.read_text(encoding="utf-8", errors="strict") if registry_path.exists() else ""
        registry = {
            match.group("canonical"): {
                "display": match.group("display"),
                "module": match.group("module"),
                "coordinates": match.group("coordinates"),
                "traj": match.group("traj"),
                "first_axis": match.group("first_axis"),
                "second_axis": match.group("second_axis"),
                "first_slot": int(match.group("first_slot")),
                "second_slot": int(match.group("second_slot")),
            }
            for match in re.finditer(
                r'\{\s*(?:\d+U|V5_MOTION_MODEL_ID_[A-Z0-9_]+),\s*'
                r'"(?P<canonical>[^"]+)",\s*"(?P<display>[^"]+)",\s*'
                r'\{[^}]*\},\s*"(?P<module>[^"]+)",\s*"(?P<coordinates>[^"]+)",\s*'
                r'"(?P<traj>[^"]+)",\s*\d+U,\s*'
                r"'(?P<first_axis>[ABC])',\s*'(?P<second_axis>[ABC])',\s*"
                r'(?P<first_slot>\d+)U,\s*(?P<second_slot>\d+)U,',
                registry_text,
                re.DOTALL,
            )
        }
        linuxcnc_source = ROOT.parent / "linuxcnc"
        command_source = linuxcnc_source / "src" / "emc" / "motion" / "command.c"
        control_source = linuxcnc_source / "src" / "emc" / "motion" / "control.c"
        private_source = linuxcnc_source / "src" / "emc" / "motion" / "mot_priv.h"
        task_source = linuxcnc_source / "src" / "emc" / "task" / "taskintf.cc"
        required_sources = (command_source, control_source, private_source, task_source)
        if not all(path.is_file() for path in required_sources):
            print(f"ROTARY_NATIVE_SOURCE_MISSING: {linuxcnc_source}", file=sys.stderr)
            rc = 1
        else:
            command_text = command_source.read_text(encoding="utf-8", errors="strict")
            private_text = private_source.read_text(encoding="utf-8", errors="strict")
            for token in (
                "v5_prepare_wrapped_rotary_target",
                "v5_wrapped_rotary_turn_offset_deg",
                "v5_commit_wrapped_rotary_turn_offset",
                "v5_reset_wrapped_rotary_turn_offsets",
                "GM_FLAG_DISTANCE_MODE",
                "GM_FIELD_G_MODE_0",
            ):
                if token not in command_text:
                    print(
                        f"ROTARY_NATIVE_SOURCE_CONTRACT_MISSING: {command_source} lacks {token}",
                        file=sys.stderr,
                    )
                    rc = 1
            if "extern void v5_reset_wrapped_rotary_turn_offsets(void);" not in private_text:
                print(f"ROTARY_NATIVE_SOURCE_CONTRACT_MISSING: {private_source}", file=sys.stderr)
                rc = 1
            forbidden_sources = {
                control_source: (
                    "z20_normalize_wrapped_rotaries_if_idle",
                    "z20_apply_wrapped_rotary_traverse_target",
                    "z20_wrapped_rotary_mask",
                    "z20_wrapped_rotary_planner_idle",
                ),
                task_source: (
                    "z20_wrap_public_degrees",
                    "z20_wrap_public_pose",
                    "z20_wrap_public_joint_position",
                    "WRAPPED_ROTARY",
                ),
            }
            for source, forbidden_tokens in forbidden_sources.items():
                source_text = source.read_text(encoding="utf-8", errors="strict")
                for forbidden in forbidden_tokens:
                    if forbidden not in source_text:
                        continue
                    print(
                        f"ROTARY_NATIVE_SOURCE_FORBIDDEN_PATH: {source} contains {forbidden}",
                        file=sys.stderr,
                    )
                    rc = 1
            if not re.search(
                r"case EMCMOT_ABORT:\s+v5_reset_wrapped_rotary_turn_offsets\(\);",
                command_text,
            ):
                print(
                    f"ROTARY_NATIVE_ABORT_SYNC_RESET_MISSING: {command_source}",
                    file=sys.stderr,
                )
                rc = 1
        required_hal = (
            "loadrt [RTCP]KINS_MODULE coordinates=[RTCP]KINS_COORDINATES sparm=identityfirst",
            "motion.tooloffset.z => [RTCP]KINS_TOOL_OFFSET_PIN",
            "loadusr -Wn v5_native_hal_owner /usr/bin/v5_native_hal_owner",
            "--model=[RTCP]MODEL",
            "--kins-module=[RTCP]KINS_MODULE",
            "--g53-a-y=[RTCP]G53_A_Y",
            "--g53-a-z=[RTCP]G53_A_Z",
            "--g53-b-x=[RTCP]G53_B_X",
            "--g53-b-z=[RTCP]G53_B_Z",
            "--g53-c-x=[RTCP]G53_C_X",
            "--g53-c-y=[RTCP]G53_C_Y",
            "v5-native-hal-owner.kins-x-rot-point => [RTCP]KINS_X_ROT_POINT_PIN",
            "v5-native-hal-owner.kins-y-rot-point => [RTCP]KINS_Y_ROT_POINT_PIN",
            "v5-native-hal-owner.kins-z-rot-point => [RTCP]KINS_Z_ROT_POINT_PIN",
            "v5-native-hal-owner.kins-x-offset => [RTCP]KINS_X_OFFSET_PIN",
            "v5-native-hal-owner.kins-y-offset => [RTCP]KINS_Y_OFFSET_PIN",
            "v5-native-hal-owner.kins-z-offset => [RTCP]KINS_Z_OFFSET_PIN",
        )
        for token in required_hal:
            if token not in hal_text:
                print(f"ACTIVE_MODEL_KINS_HAL_CONTRACT_MISSING: {bus_hal.relative_to(ROOT)} lacks {token}", file=sys.stderr)
                rc = 1
        for descriptor in registry.values():
            for token in (
                f"loadrt {descriptor['module']}",
                f"{descriptor['module']}.tool-offset",
                f"setp {descriptor['module']}.",
            ):
                if token in hal_text:
                    print(f"ACTIVE_MODEL_KINS_HAL_HARDCODED_MODEL: {bus_hal.relative_to(ROOT)} contains {token}", file=sys.stderr)
                    rc = 1
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str
        parser.read(bus_ini, encoding="utf-8")
        model = parser.get("RTCP", "MODEL", fallback="").strip()
        descriptor = registry.get(model)
        if not descriptor:
            print(f"ACTIVE_MODEL_KINS_UNREGISTERED: {bus_ini.relative_to(ROOT)} MODEL={model!r}", file=sys.stderr)
            rc = 1
        else:
            expected = {
                ("RTCP", "MOTION_MODEL"): descriptor["display"],
                ("RTCP", "KINS_MODULE"): descriptor["module"],
                ("RTCP", "KINS_COORDINATES"): descriptor["coordinates"],
                ("RTCP", "KINS_PREFIX"): descriptor["module"],
                ("RTCP", "KINS_TOOL_OFFSET_PIN"): f"{descriptor['module']}.tool-offset",
                ("RTCP", "KINS_X_ROT_POINT_PIN"): f"{descriptor['module']}.x-rot-point",
                ("RTCP", "KINS_Y_ROT_POINT_PIN"): f"{descriptor['module']}.y-rot-point",
                ("RTCP", "KINS_Z_ROT_POINT_PIN"): f"{descriptor['module']}.z-rot-point",
                ("RTCP", "KINS_X_OFFSET_PIN"): f"{descriptor['module']}.x-offset",
                ("RTCP", "KINS_Y_OFFSET_PIN"): f"{descriptor['module']}.y-offset",
                ("RTCP", "KINS_Z_OFFSET_PIN"): f"{descriptor['module']}.z-offset",
                ("KINS", "KINEMATICS"): f"{descriptor['module']} coordinates={descriptor['coordinates']} sparm=identityfirst",
                ("TRAJ", "COORDINATES"): descriptor["traj"],
            }
            for (section, key), wanted in expected.items():
                actual = parser.get(section, key, fallback="").strip()
                if actual != wanted:
                    print(
                        f"ACTIVE_MODEL_KINS_INI_MISMATCH: {bus_ini.relative_to(ROOT)} "
                        f"[{section}] {key}={actual!r} expected={wanted!r} model={model}",
                        file=sys.stderr,
                    )
                    rc = 1
    return rc


def check_linuxcnc_source_rebuild_policy() -> int:
    project_root = ROOT.parent
    source_root = project_root / "linuxcnc"
    identity_path = source_root / "v5_linuxcnc_source_identity.json"
    recipe_path = ROOT / "linuxcnc" / "yocto" / "linuxcnc-prebuilt.bb"
    allowlist_path = (
        ROOT / "linuxcnc" / "yocto" / "files" / "v5_linuxcnc_runtime_allowlist.tsv"
    )
    verifier_path = ROOT / "tools" / "linuxcnc" / "verify_v5_linuxcnc_source.py"
    minimal_verifier_path = (
        ROOT / "tools" / "linuxcnc" / "verify_v5_linuxcnc_minimal_runtime.py"
    )
    build_path = ROOT / "tools" / "linuxcnc" / "build_v5_linuxcnc_petalinux.sh"
    installer_path = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    rootfs_config_path = ROOT / "petalinux" / "project-spec" / "configs" / "rootfs_config"
    custom_rootfs_allowlist_path = (
        ROOT
        / "petalinux"
        / "project-spec"
        / "meta-user"
        / "conf"
        / "user-rootfsconfig"
    )
    sync_path = ROOT / "linuxcnc" / "tools" / "sync_v5_linuxcnc_recipe_to_petalinux.sh"
    retired_paths = (
        ROOT / "linuxcnc" / "v5_linuxcnc_source.lock.json",
        ROOT / "linuxcnc" / "patches" / "0001-v5-native-rotary-nearest-target.patch",
    )
    required_paths = (
        identity_path,
        recipe_path,
        allowlist_path,
        verifier_path,
        minimal_verifier_path,
        build_path,
        installer_path,
        rootfs_config_path,
        custom_rootfs_allowlist_path,
    )
    for path in required_paths:
        if not path.is_file():
            print(f"LINUXCNC_REBUILD_OWNER_MISSING: {path}", file=sys.stderr)
            return 1
    for path in (*retired_paths, sync_path):
        if path.exists():
            print(f"LINUXCNC_REBUILD_RETIRED_PATH_PRESENT: {path}", file=sys.stderr)
            return 1
    try:
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"LINUXCNC_REBUILD_IDENTITY_INVALID: {exc}", file=sys.stderr)
        return 1
    if identity.get("schema") != "v5-linuxcnc-vendored-source-v1":
        print("LINUXCNC_REBUILD_IDENTITY_SCHEMA_INVALID", file=sys.stderr)
        return 1
    commit = identity.get("upstream", {}).get("commit", "")
    recipe = recipe_path.read_text(encoding="utf-8", errors="strict")
    if len(commit) != 40 or "inherit pkgconfig externalsrc" not in recipe:
        print("LINUXCNC_REBUILD_EXTERNAL_SOURCE_NOT_PINNED", file=sys.stderr)
        return 1
    for required in (
        'V5_LINUXCNC_EXTERNAL_SOURCE ?= ""',
        'V5_LINUXCNC_EXTERNAL_BUILD ?= ""',
        'EXTERNALSRC = "${V5_LINUXCNC_EXTERNAL_SOURCE}"',
        'EXTERNALSRC_BUILD = "${V5_LINUXCNC_EXTERNAL_BUILD}"',
        'B = "${EXTERNALSRC_BUILD}"',
        'do_configure[file-checksums] = "',
        'do_configure[nostamp] = "1"',
        '${S}/v5_linuxcnc_source_identity.json:True',
        '${WORKDIR}/v5_linuxcnc_runtime_allowlist.tsv:True',
        'SRC_URI += "file://v5_linuxcnc_runtime_allowlist.tsv"',
        'PARALLEL_MAKE = "-j 8"',
        'runtime_count=$(expr "$runtime_count" + 1)',
        "oe_runmake V5_HEADLESS_RUNTIME=1",
        "V5 headless Python native status binding owner missing",
        'PACKAGEFUNCS_prepend = "v5_refresh_runtime_hashes "',
        'package_root="${PKGDEST}/${PN}"',
        'package_rtapi_app="$package_root${bindir}/rtapi_app"',
        'final LinuxCNC package rtapi_app must remain root:root 4755',
        'chown root:root "$rtapi_app_path"',
        'chmod 4755 "$rtapi_app_path"',
        'stat -c \'%u:%g:%a\' "$rtapi_app_path"',
        'FILES_${PN}-dev += "',
        'RDEPENDS_${PN}-dev += "python3-core"',
        "linuxcnc-runtime-files.sha256",
        "v5_linuxcnc_source_identity.json",
    ):
        if required not in recipe:
            print(f"LINUXCNC_REBUILD_EXTERNAL_SOURCE_GATE_MISSING: {required}", file=sys.stderr)
            return 1
    build_text = build_path.read_text(encoding="utf-8", errors="strict")
    for required in (
        'if [ "$build_mode" = focused ]; then',
        "run_petalinux_overlay prepare-target-only",
        "run_petalinux_overlay prepare",
    ):
        if required not in build_text:
            print(f"LINUXCNC_FOCUSED_OVERLAY_GATE_MISSING: {required}", file=sys.stderr)
            return 1
    for forbidden in (
        "git://",
        "SRCREV",
        "0001-v5-native-rotary-nearest-target.patch",
        "/home/",
        'EXTERNALSRC_BUILD = "${V5_LINUXCNC_EXTERNAL_SOURCE}/src"',
        'B = "${S}/src"',
    ):
        if forbidden in recipe:
            print(f"LINUXCNC_REBUILD_RETIRED_PATH: {forbidden}", file=sys.stderr)
            return 1
    installer = installer_path.read_text(encoding="utf-8", errors="strict")
    for required in (
        'linuxcnc_rtapi_app="$linuxcnc_package_root/usr/bin/rtapi_app"',
        'stat -c \'%a\' "$linuxcnc_rtapi_app"',
        'LinuxCNC deploy bundle rtapi_app must retain setuid mode 4755',
        'source_mode=$(stat -c \'%a\' "$source_path")',
        'chmod "$source_mode" "$temporary"',
        'linuxcnc_rtapi_app=/usr/bin/rtapi_app',
        'stat -c \'%u:%g:%a\' "$linuxcnc_rtapi_app"',
    ):
        if required not in installer:
            print(f"LINUXCNC_RTAPI_APP_DEPLOY_MODE_GATE_MISSING: {required}", file=sys.stderr)
            return 1
    install_order = (
        'cp -p "$source_path" "$temporary"',
        'chown root:root "$temporary"',
        'chmod "$source_mode" "$temporary"',
        'mv -f "$temporary" "$destination"',
    )
    install_order_positions = tuple(installer.find(token) for token in install_order)
    if any(position < 0 for position in install_order_positions) or tuple(
        sorted(install_order_positions)
    ) != install_order_positions:
        print("LINUXCNC_DEPLOY_MODE_RESTORE_ORDER_INVALID", file=sys.stderr)
        return 1
    allowlist_rows = {}
    for line_number, raw in enumerate(
        allowlist_path.read_text(encoding="utf-8", errors="strict").splitlines(), 1
    ):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 2 or not fields[0].startswith("/") or not fields[1].strip():
            print(f"LINUXCNC_MINIMAL_ALLOWLIST_INVALID: line={line_number}", file=sys.stderr)
            return 1
        if fields[0] in allowlist_rows:
            print(f"LINUXCNC_MINIMAL_ALLOWLIST_DUPLICATE: {fields[0]}", file=sys.stderr)
            return 1
        allowlist_rows[fields[0]] = fields[1]
    if not allowlist_rows:
        print("LINUXCNC_MINIMAL_ALLOWLIST_EMPTY", file=sys.stderr)
        return 1
    for required_runtime in (
        "/usr/lib/linuxcnc/modules/v5_bus_axis_router.so",
        "/usr/lib/linuxcnc/modules/v5_bus_homecomp.so",
        "/usr/lib/python3/dist-packages/_hal.so",
        "/usr/lib/python3/dist-packages/hal.py",
    ):
        if required_runtime not in allowlist_rows:
            print(
                f"LINUXCNC_MINIMAL_ALLOWLIST_REQUIRED_RUNTIME_MISSING: {required_runtime}",
                file=sys.stderr,
            )
            return 1
    files_match = re.search(r'FILES_\$\{PN\}\s*=\s*"(.*?)"', recipe, re.DOTALL)
    rdepends_match = re.search(r'RDEPENDS_\$\{PN\}\s*\+=\s*"(.*?)"', recipe, re.DOTALL)
    depends_match = re.search(r'DEPENDS\s*\+=\s*"(.*?)"', recipe, re.DOTALL)
    if files_match is None or rdepends_match is None or depends_match is None:
        print("LINUXCNC_MINIMAL_PACKAGE_BLOCK_MISSING", file=sys.stderr)
        return 1
    if re.search(r"(^|\s)libepoxy($|\s)", depends_match.group(1)):
        print("LINUXCNC_MINIMAL_HEADLESS_BUILD_DEPENDENCY_PRESENT: libepoxy", file=sys.stderr)
        return 1
    runtime_entries = set()
    substitutions = {
        "${sysconfdir}": "/etc",
        "${bindir}": "/usr/bin",
        "${libdir}": "/usr/lib",
        "${datadir}": "/usr/share",
    }
    for raw in files_match.group(1).splitlines():
        entry = raw.strip()
        if entry.endswith("\\"):
            entry = entry[:-1].strip()
        for token, value in substitutions.items():
            entry = entry.replace(token, value)
        if entry:
            runtime_entries.add(entry)
    missing_entries = sorted(set(allowlist_rows) - runtime_entries)
    if missing_entries:
        print(f"LINUXCNC_MINIMAL_FILES_BLOCK_INCOMPLETE: {missing_entries}", file=sys.stderr)
        return 1
    for broad_entry in ("/usr/bin", "/usr/lib", "/usr/share", "/usr/include/linuxcnc"):
        if broad_entry in runtime_entries:
            print(f"LINUXCNC_MINIMAL_BROAD_PACKAGE_ENTRY: {broad_entry}", file=sys.stderr)
            return 1
    for forbidden_runtime_dependency in (
        "python3-modules",
        "python3-pyqt5",
        "libepoxy",
        "gtk",
        "qt",
        "tcl",
        "tk",
    ):
        if re.search(
            rf"(^|\s){re.escape(forbidden_runtime_dependency)}($|\s)",
            rdepends_match.group(1),
        ):
            print(
                "LINUXCNC_MINIMAL_FORBIDDEN_RDEPEND: "
                f"{forbidden_runtime_dependency}",
                file=sys.stderr,
            )
            return 1
    compile(
        minimal_verifier_path.read_text(encoding="utf-8", errors="strict"),
        str(minimal_verifier_path),
        "exec",
    )
    rootfs_config = rootfs_config_path.read_text(encoding="utf-8", errors="strict")
    forbidden_enabled_rootfs = re.findall(
        r"^CONFIG_(?:packagegroup-petalinux-(?:qt|qt-extended|matchbox)|"
        r"packagegroup-core-x11|gtk|gtkPLUS|qt|tk|libx11|xserver|wayland|weston)"
        r"[^=]*=[ym]$",
        rootfs_config,
        re.MULTILINE | re.IGNORECASE,
    )
    if forbidden_enabled_rootfs:
        print(
            f"LINUXCNC_MINIMAL_ROOTFS_GUI_SELECTED: {forbidden_enabled_rootfs}",
            file=sys.stderr,
        )
        return 1
    forbidden_python_rootfs = re.findall(
        r"^CONFIG_python3(?:-(?:modules|tkinter))?=[ym]$",
        rootfs_config,
        re.MULTILINE,
    )
    if forbidden_python_rootfs:
        print(
            "LINUXCNC_MINIMAL_ROOTFS_BROAD_PYTHON_SELECTED: "
            f"{forbidden_python_rootfs}",
            file=sys.stderr,
        )
        return 1
    if "# CONFIG_python3 is not set" not in rootfs_config:
        print("LINUXCNC_MINIMAL_ROOTFS_BROAD_PYTHON_NOT_DISABLED", file=sys.stderr)
        return 1
    expected_custom_rootfs_packages = {
        "CONFIG_v5-base-overlay",
        "CONFIG_v5-stepgen-module",
        "CONFIG_linuxcnc-prebuilt",
        "CONFIG_ethercat-master",
        "CONFIG_linuxcnc-ethercat",
        "CONFIG_hal-cia402",
        "CONFIG_wpa-supplicant",
        "CONFIG_wpa-supplicant-cli",
        "CONFIG_wpa-supplicant-passphrase",
    }
    actual_custom_rootfs_packages = {
        line.strip()
        for line in custom_rootfs_allowlist_path.read_text(
            encoding="utf-8", errors="strict"
        ).splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    if actual_custom_rootfs_packages != expected_custom_rootfs_packages:
        print(
            "LINUXCNC_MINIMAL_ROOTFS_CUSTOM_WHITELIST_MISMATCH: "
            f"actual={sorted(actual_custom_rootfs_packages)}",
            file=sys.stderr,
        )
        return 1
    build_script = build_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "findmnt -n -o FSTYPE,OPTIONS -T",
        "mount -t overlay overlay",
        'lowerdir=$linuxcnc_projection,upperdir=$overlay_upper,workdir=$overlay_work',
        'ln -s "$integration_root/yocto/linuxcnc-prebuilt.bb"',
        'ln -s "$runtime_allowlist"',
        'BB_ENV_EXTRAWHITE="${BB_ENV_EXTRAWHITE:-} V5_LINUXCNC_EXTERNAL_SOURCE V5_LINUXCNC_EXTERNAL_BUILD"',
        'V5_LINUXCNC_EXTERNAL_SOURCE="$linuxcnc_projection"',
        'V5_LINUXCNC_EXTERNAL_BUILD="$linuxcnc_external_build"',
        "build_mode=focused",
        'run_petalinux_build "-c linuxcnc-prebuilt -x listtasks"',
        'run_bitbake_direct "linuxcnc-prebuilt -c package"',
        'run_bitbake_direct "petalinux-image-minimal -c rootfs"',
        'missing_source_report="$build_root/petalinux/v5-missing-source-inputs.json"',
        'V5_WINDOWS_SOURCE_IMPORT_REQUIRED',
        '--missing-report "$missing_source_report"',
        '--resume-scope "linuxcnc-$build_mode"',
        '-u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY',
        '-u http_proxy -u https_proxy -u all_proxy',
        "verify_rootfs_package_selection",
        "V5_ROOTFS_PACKAGE_WHITELIST_OK",
        "prepare_linuxcnc_install_workdir",
        "linuxcnc_install_reset=1",
        'V5_LINUXCNC_STALE_IMAGE_REMOVED owner=$image_owner path=$image_dir',
        "V5_LINUXCNC_INSTALL_WORKDIR_OK",
        'run_bitbake_direct "linuxcnc-prebuilt -c install -f"',
        "audit_minimal_runtime",
        "V5_LINUXCNC_FOCUSED_OK",
    ):
        if token not in build_script:
            print(f"LINUXCNC_REBUILD_READONLY_OWNER_GATE_MISSING: {token}", file=sys.stderr)
            return 1

    package_only_marker = (
        '\nif [ "$package_only" -eq 1 ]; then\n'
        '    if [ ! -x "$petalinux_root/components/yocto/layers/core/bitbake/bin/bitbake" ]'
    )
    package_only_start = build_script.find(package_only_marker)
    package_only_end = build_script.find(
        '\nfi\n\nverify_rootfs_package_selection', package_only_start
    )
    if package_only_start < 0 or package_only_end < 0:
        print("LINUXCNC_PACKAGE_ONLY_BRANCH_MISSING", file=sys.stderr)
        return 1
    package_only_body = build_script[package_only_start:package_only_end]
    package_task = 'run_bitbake_direct "linuxcnc-prebuilt -c package -f"'
    if package_only_body.count(package_task) != 1:
        print("LINUXCNC_PACKAGE_ONLY_SINGLE_DAG_MISSING", file=sys.stderr)
        return 1
    for forbidden in (
        'linuxcnc-prebuilt -c compile -f',
        'petalinux-image-minimal',
        'verify_windows_source_packages',
        'verify_rootfs_package_selection',
        'audit_minimal_runtime',
    ):
        if forbidden in package_only_body:
            print(
                f"LINUXCNC_PACKAGE_ONLY_EXPANDED_PATH_PRESENT: {forbidden}",
                file=sys.stderr,
            )
            return 1
    for required in (
        '--package-only) build_mode=package-only; package_only=1',
        '--package-target)',
        '--sync-registered-delta "$linuxcnc_projection"',
        'V5_LINUXCNC_PACKAGE_ONLY_OK target=$package_target package_root=$package_root',
        'run_bitbake_direct "linuxcnc-prebuilt -c populate_sysroot"',
        'run_bitbake_direct "linux-xlnx -c v5_linux_projection -f"',
        'run_bitbake_direct "linuxcnc-ethercat -c compile -f"',
        'run_bitbake_direct "linuxcnc-ethercat -c install -f"',
        'run_bitbake_direct "linuxcnc-ethercat -c package -f"',
        'V5_LINUX_KERNEL_PROJECTION_REPAIRED task=do_v5_linux_projection',
        'V5_LINUXCNC_ETHERCAT_PACKAGE_ONLY_OK',
    ):
        if required not in build_script:
            print(f"LINUXCNC_PACKAGE_ONLY_FAST_PATH_MISSING: {required}", file=sys.stderr)
            return 1
    if "bitbake -b" in package_only_body:
        print("LINUXCNC_PACKAGE_ONLY_RECIPE_BYPASS_PRESENT", file=sys.stderr)
        return 1
    if "sync_v5_linuxcnc_recipe_to_petalinux" in build_script:
        print("LINUXCNC_REBUILD_RETIRED_SYNC_CALL_PRESENT", file=sys.stderr)
        return 1
    if 'V5_LINUXCNC_EXTERNAL_SOURCE="$overlay_merged"' in build_script:
        print("LINUXCNC_REBUILD_WRITABLE_VIEW_USED_AS_SOURCE", file=sys.stderr)
        return 1
    if '-c linuxcnc-prebuilt -x clean' in build_script:
        print("LINUXCNC_MINIMAL_FOCUSED_OUTPUT_CLEAN_PRESENT", file=sys.stderr)
        return 1
    if "rootfs/usr/bin/linuxcnc" in build_script:
        print("LINUXCNC_MINIMAL_EPHEMERAL_ROOTFS_AUDIT_PRESENT", file=sys.stderr)
        return 1
    if 'run_petalinux_build "-c linuxcnc-prebuilt"' in build_script:
        print("LINUXCNC_MINIMAL_DUPLICATE_FOCUSED_PACKAGE_BUILD", file=sys.stderr)
        return 1
    kernel_clean_gate = re.search(
        r'if \[ "\$clean_kernel" -eq 1 \].*?'
        r'run_petalinux_build "-c kernel -x clean".*?fi',
        build_script,
        re.DOTALL,
    )
    if kernel_clean_gate is None or build_script.count('-c kernel -x clean') != 1:
        print("LINUXCNC_MINIMAL_KERNEL_CLEAN_NOT_EXPLICIT_FINAL_ONLY", file=sys.stderr)
        return 1
    external_runtime_recipes = (
        ROOT
        / "petalinux"
        / "project-spec"
        / "meta-user"
        / "recipes-apps"
        / "hal-cia402"
        / "hal-cia402.bb",
        ROOT
        / "petalinux"
        / "project-spec"
        / "meta-user"
        / "recipes-apps"
        / "v5-stepgen-module"
        / "v5-stepgen-module.bb",
        ROOT
        / "petalinux"
        / "project-spec"
        / "meta-user"
        / "recipes-apps"
        / "linuxcnc-ethercat"
        / "linuxcnc-ethercat_git.bb",
    )
    for external_recipe in external_runtime_recipes:
        external_text = external_recipe.read_text(encoding="utf-8", errors="strict")
        for forbidden in ("/usr/src/", "lcec_configgen"):
            if forbidden in external_text:
                print(
                    f"LINUXCNC_MINIMAL_EXTERNAL_DEV_PATH: {external_recipe}:{forbidden}",
                    file=sys.stderr,
                )
                return 1
    result = subprocess.run(
        [sys.executable, str(verifier_path), "--project-root", str(project_root)],
        cwd=project_root,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        print(f"LINUXCNC_REBUILD_VERIFY_FAILED: {result.stderr.strip() or result.stdout.strip()}", file=sys.stderr)
        return 1
    return 0


def _strip_gcode_comment(line: str) -> str:
    out = []
    in_paren = False
    for char in line:
        if char == "(":
            in_paren = True
            continue
        if char == ")":
            in_paren = False
            continue
        if not in_paren:
            out.append(char)
    return "".join(out)


def _gcode_numeric_words(line: str) -> dict[str, float]:
    words: dict[str, float] = {}
    for match in re.finditer(r"(?<![A-Z])([A-Z])\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+))", line):
        words[match.group(1)] = float(match.group(2))
    return words


def _check_cc_golden_arc_chain(
    program: Path,
    expected_axis: str,
    start_c: float,
    spring_arcs: list[tuple[int, dict[str, float]]],
) -> int:
    if len(spring_arcs) != 20:
        print(
            f"CC_GOLDEN_HELIX_ARC_COUNT: {program.relative_to(ROOT)}: expected=20 actual={len(spring_arcs)}",
            file=sys.stderr,
        )
        return 1
    start_x = 0.0
    start_y = 10.0
    quarter_xy = ((-10.0, 0.0), (0.0, -10.0), (10.0, 0.0), (0.0, 10.0))
    quarter_axis = (55.0, 45.0, 35.0, 45.0)
    tolerance = 1e-6
    for index, (line_no, words) in enumerate(spring_arcs):
        required_words = ("X", "Y", "Z", expected_axis, "C", "I", "J")
        missing = [word for word in required_words if word not in words]
        if missing:
            print(
                f"CC_GOLDEN_HELIX_WORD_MISSING: {program.relative_to(ROOT)}:{line_no}: "
                f"missing={','.join(missing)}",
                file=sys.stderr,
            )
            return 1
        expected_x, expected_y = quarter_xy[index % 4]
        expected_z = -2.5 * (index + 1)
        expected_rotary = quarter_axis[index % 4]
        expected_c = start_c - 90.0 * (index + 1)
        expected_values = {
            "X": expected_x,
            "Y": expected_y,
            "Z": expected_z,
            expected_axis: expected_rotary,
            "C": expected_c,
        }
        for word, expected in expected_values.items():
            if abs(words[word] - expected) > tolerance:
                print(
                    f"CC_GOLDEN_HELIX_VALUE_MISMATCH: {program.relative_to(ROOT)}:{line_no}: "
                    f"word={word} expected={expected:.3f} actual={words[word]:.3f}",
                    file=sys.stderr,
                )
                return 1
        center_x = start_x + words["I"]
        center_y = start_y + words["J"]
        start_radius_sq = (start_x - center_x) ** 2 + (start_y - center_y) ** 2
        end_radius_sq = (words["X"] - center_x) ** 2 + (words["Y"] - center_y) ** 2
        if (
            abs(center_x) > tolerance
            or abs(center_y) > tolerance
            or abs(start_radius_sq - 100.0) > tolerance
            or abs(end_radius_sq - 100.0) > tolerance
        ):
            print(
                f"CC_GOLDEN_HELIX_GEOMETRY_INVALID: {program.relative_to(ROOT)}:{line_no}: "
                f"center=({center_x:.3f},{center_y:.3f}) "
                f"radius_sq=({start_radius_sq:.3f},{end_radius_sq:.3f})",
                file=sys.stderr,
            )
            return 1
        start_x = words["X"]
        start_y = words["Y"]
    return 0


def _check_cc_golden_program(
    program: Path,
    expected_axis: str,
    forbidden_axis: str,
    start_c: float,
) -> int:
    pending_spring_anchor = False
    seen_spring_anchor = False
    seen_cutting_trajectory = False
    seen_spring_feed = False
    seen_program_rtcp_on = False
    seen_program_rtcp_off = False
    seen_machine_return = False
    seen_expected_axis = False
    spring_arcs: list[tuple[int, dict[str, float]]] = []
    for line_no, raw in enumerate(program.read_text(encoding="ascii", errors="ignore").splitlines(), 1):
        raw_upper = raw.upper()
        if "SPRING ANCHOR" in raw_upper:
            pending_spring_anchor = True
        if "CUTTING TRAJECTORY" in raw_upper:
            seen_cutting_trajectory = True
        if raw_upper.strip().startswith("(MACHINE-COORDINATE RETURN"):
            if not seen_program_rtcp_off:
                print(
                    f"CC_GOLDEN_PROGRAM_SPRING_END_RTCP_OFF_MISSING: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            seen_machine_return = True
        line = _strip_gcode_comment(raw).strip().upper()
        if not line:
            continue
        if re.search(rf"(?<![A-Z]){forbidden_axis}\s*[-+]?(?:\d|\.)", line):
            print(
                f"CC_GOLDEN_WRONG_MODEL_AXIS: {program.relative_to(ROOT)}:{line_no}: "
                f"contains {forbidden_axis} axis: {raw.strip()}",
                file=sys.stderr,
            )
            return 1
        if re.search(rf"(?<![A-Z]){expected_axis}\s*[-+]?(?:\d|\.)", line):
            seen_expected_axis = True
        if re.search(r"\bG43\.4\b", line):
            print(f"CC_GOLDEN_UNSUPPORTED_G43_4: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}", file=sys.stderr)
            return 1
        has_motion = re.search(r"\bG(?:0|00|1|01|2|02|3|03)\b", line) is not None
        has_linear_feed = re.search(r"\bG(?:1|01)\b", line) is not None
        has_spring_feed = re.search(r"\bG(?:3|03)\b", line) is not None
        has_g53 = re.search(r"\bG53\b", line) is not None
        if pending_spring_anchor and has_motion:
            anchor_words = _gcode_numeric_words(line)
            expected_anchor = {
                "X": 0.0,
                "Y": 10.0,
                "Z": 0.0,
                expected_axis: 45.0,
                "C": start_c,
            }
            for word, expected in expected_anchor.items():
                if word not in anchor_words or abs(anchor_words[word] - expected) > 1e-6:
                    actual = anchor_words.get(word)
                    print(
                        f"CC_GOLDEN_SPRING_ANCHOR_MISMATCH: {program.relative_to(ROOT)}:{line_no}: "
                        f"word={word} expected={expected:.3f} actual={actual}",
                        file=sys.stderr,
                    )
                    return 1
            seen_spring_anchor = True
            pending_spring_anchor = False
        if re.search(r"\bM64\b", line) and re.search(r"\bP0\b", line):
            if seen_program_rtcp_on:
                print(f"CC_GOLDEN_PROGRAM_RTCP_ON_DUPLICATE: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}", file=sys.stderr)
                return 1
            if seen_program_rtcp_off:
                print(f"CC_GOLDEN_PROGRAM_RTCP_ON_AFTER_RTCP_OFF: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}", file=sys.stderr)
                return 1
            if not seen_spring_anchor:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_ON_BEFORE_SPRING_START: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            if seen_spring_feed:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_ON_AFTER_SPRING_FEED: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            seen_program_rtcp_on = True
            continue
        if re.search(r"\bM65\b", line) and re.search(r"\bP0\b", line):
            if seen_program_rtcp_off:
                print(f"CC_GOLDEN_PROGRAM_RTCP_OFF_DUPLICATE: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}", file=sys.stderr)
                return 1
            if seen_machine_return:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_OFF_AFTER_MACHINE_RETURN: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            if not seen_spring_feed:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_OFF_BEFORE_SPRING_FEED: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            seen_program_rtcp_off = True
            continue
        has_z_zero = re.search(r"(?<![A-Z])Z\s*[-+]?0(?:\.0*)?(?![0-9.])", line) is not None
        if seen_cutting_trajectory and has_linear_feed and not seen_program_rtcp_off:
            print(
                f"CC_GOLDEN_G1_POLYLINE_FORBIDDEN: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                file=sys.stderr,
            )
            return 1
        if seen_cutting_trajectory and has_spring_feed:
            if not seen_program_rtcp_on:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_ON_AFTER_SPRING_START: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
            spring_arcs.append((line_no, _gcode_numeric_words(line)))
            seen_spring_feed = True
        if seen_machine_return and has_motion and has_z_zero and has_g53 and not seen_program_rtcp_off:
            print(
                f"CC_GOLDEN_RTCP_OFF_AFTER_FINAL_Z_RETURN: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                file=sys.stderr,
            )
            return 1
        if seen_program_rtcp_off and has_motion and not has_g53:
            print(
                f"CC_GOLDEN_RTCP_OFF_NON_G53_MOTION: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                file=sys.stderr,
            )
            return 1
    required = (
        (seen_spring_anchor, "SPRING_ANCHOR_MISSING"),
        (seen_spring_feed, "SPRING_FEED_MISSING"),
        (seen_program_rtcp_on, "RTCP_ON_MISSING"),
        (seen_program_rtcp_off, "RTCP_OFF_MISSING"),
        (seen_expected_axis, f"{expected_axis}_AXIS_MOTION_MISSING"),
    )
    for present, code in required:
        if not present:
            print(f"CC_GOLDEN_PROGRAM_{code}: {program.relative_to(ROOT)}", file=sys.stderr)
            return 1
    return _check_cc_golden_arc_chain(program, expected_axis, start_c, spring_arcs)


def check_cc_golden_model_specific_motion() -> int:
    programs = (
        (ROOT / "gcode" / "golden" / "cc-ac.ngc", "A", "B", 0.0),
        (ROOT / "gcode" / "golden" / "cc-bc.ngc", "B", "A", 90.0),
    )
    legacy_program = ROOT / "gcode" / "golden" / "cc.ngc"
    runner = ROOT / "services" / "command_gate" / "v5_linuxcncrsh_golden_run.c"
    acceptance = ROOT / "tools" / "deploy" / "run_v5_board_acceptance.sh"
    manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    hal = ROOT / "linuxcnc" / "hal" / "v5_bus_1ms.hal"
    native_hal_owner = ROOT.parent / "linuxcnc" / "src" / "hal" / "user_comps" / "v5_native_hal_owner.comp"
    rc = 0
    if legacy_program.exists():
        print("CC_GOLDEN_LEGACY_PROGRAM_SURVIVOR: gcode/golden/cc.ngc", file=sys.stderr)
        rc = 1
    for program, expected_axis, forbidden_axis, start_c in programs:
        if not program.exists():
            print(f"CC_GOLDEN_PROGRAM_MISSING: {program.relative_to(ROOT)}", file=sys.stderr)
            rc = 1
            continue
        rc |= _check_cc_golden_program(program, expected_axis, forbidden_axis, start_c)
    if not runner.exists():
        print("CC_GOLDEN_RUNNER_MISSING: services/command_gate/v5_linuxcncrsh_golden_run.c", file=sys.stderr)
        return 1
    if not acceptance.exists():
        print("CC_GOLDEN_ACCEPTANCE_MISSING: tools/deploy/run_v5_board_acceptance.sh", file=sys.stderr)
        return 1
    if not manifest.exists():
        print("CC_GOLDEN_DEPLOY_MANIFEST_MISSING: config/deploy/v5_runtime_deploy_manifest.tsv", file=sys.stderr)
        return 1
    if not hal.exists():
        print("CC_GOLDEN_RTCP_HAL_MISSING: linuxcnc/hal/v5_bus_1ms.hal", file=sys.stderr)
        return 1
    if not native_hal_owner.exists():
        print("CC_GOLDEN_NATIVE_HAL_OWNER_MISSING: linuxcnc/src/hal/user_comps/v5_native_hal_owner.comp", file=sys.stderr)
        return 1
    runner_text = runner.read_text(encoding="utf-8", errors="ignore")
    forbidden_runner = (
        "v5_native_rtcp_control_set(1",
        "ensure_rtcp_on_before_golden_motion",
        "rtcp on confirmed before golden motion",
        "--start",
        "V5_ALLOW_MOTION",
        "V5_COMMAND_PROGRAM_OPEN",
        "V5_COMMAND_START",
        "v5_linuxcncrsh_send_line",
        "v5_native_home_send",
    )
    for token in forbidden_runner:
        if token in runner_text:
            print(f"CC_GOLDEN_RUNNER_CONTROL_BYPASS_SURVIVOR: {runner.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    required_runner = (
        "--print-active-model",
        "cc-ac.ngc",
        "cc-bc.ngc",
        "XYZAC_TRT",
        "XYZBC_TRT",
        "v5_native_g53_geometry_status_read",
        "golden_program_matches_active_model",
    )
    for token in required_runner:
        if token not in runner_text:
            print(f"CC_GOLDEN_RUNNER_MODEL_GATE_MISSING: {runner.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1

    acceptance_text = acceptance.read_text(encoding="utf-8", errors="ignore")
    for token in (
        "--motion",
        "V5_ALLOW_MOTION",
        "v5_linuxcncrsh_golden_run --program",
        "v5_linuxcncrsh_golden_run --start",
        "V5_GOLDEN_PROGRAM",
        "V5_REMOTE_GOLDEN_PROGRAM",
    ):
        if token in acceptance_text:
            print(f"CC_GOLDEN_ACCEPTANCE_CONTROL_BYPASS: {acceptance.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1

    manifest_text = manifest.read_text(encoding="utf-8", errors="ignore")
    for token in ("gcode/golden/cc-ac.ngc", "gcode/golden/cc-bc.ngc"):
        if token not in manifest_text:
            print(f"CC_GOLDEN_DEPLOY_ROW_MISSING: {manifest.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    if "gcode/golden/cc.ngc" in manifest_text:
        print(f"CC_GOLDEN_DEPLOY_LEGACY_SURVIVOR: {manifest.relative_to(ROOT)}", file=sys.stderr)
        rc = 1

    hal_text = hal.read_text(encoding="utf-8", errors="ignore")
    required_hal = (
        "loadrt or2 count=2",
        "loadrt and2 count=6",
        "loadrt v5_safety_latch",
        "loadusr -Wn v5_native_hal_owner /usr/bin/v5_native_hal_owner",
        "addf or2.0 servo-thread",
        "addf or2.1 servo-thread",
        "addf v5-safety-latch.0 servo-thread",
        "addf and2.1 servo-thread",
        "addf and2.2 servo-thread",
        "addf and2.3 servo-thread",
        "addf and2.4 servo-thread",
        "addf and2.5 servo-thread",
        "v5-safety-force v5-native-hal-owner.safety-force => v5-safety-latch.0.force",
        "v5-safety-latch.0.estop-ok => iocontrol.0.emc-enable-in",
        "joint.0.amp-enable-out => and2.1.in0",
        "joint.1.amp-enable-out => and2.2.in0",
        "joint.2.amp-enable-out => and2.3.in0",
        "joint.3.amp-enable-out => and2.4.in0",
        "joint.4.amp-enable-out => and2.5.in0",
        "and2.1.out => cia402.0.enable",
        "and2.2.out => cia402.1.enable",
        "and2.3.out => cia402.2.enable",
        "and2.4.out => cia402.3.enable",
        "and2.5.out => cia402.4.enable",
        "v5-rtcp-ui-request",
        "v5-rtcp-gcode-request motion.digital-out-00 => v5-native-hal-owner.rtcp-gcode-request or2.0.in1",
        "v5-rtcp-owner-force-off v5-native-hal-owner.rtcp-force-off => motion.v5-bus-home-rtcp-force-latched or2.1.in0",
        "v5-home-rtcp-force-off motion.v5-bus-home-rtcp-force-off => v5-native-hal-owner.home-rtcp-force-off or2.1.in1",
        "v5-rtcp-force-off or2.1.out => not.0.in",
        "v5-rtcp-selected and2.0.out => mux2.0.sel",
        "re-switchkins-select mux2.0.out => motion.switchkins-type",
        "v5-kins-x-rot-point v5-native-hal-owner.kins-x-rot-point",
        "v5-kins-x-offset v5-native-hal-owner.kins-x-offset",
    )
    for token in required_hal:
        if token not in hal_text:
            print(f"CC_GOLDEN_RTCP_HAL_CONTRACT_MISSING: {hal.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    for retired in (
        "joint-max-velocity-",
        "joint-max-acceleration-",
        "joint-scale-",
        "joint-drive-enable-",
    ):
        if retired in hal_text:
            print(
                f"CC_GOLDEN_RETIRED_PDO_LIMITER_SURVIVOR: {hal.relative_to(ROOT)} contains {retired}",
                file=sys.stderr,
            )
            rc = 1

    owner_text = native_hal_owner.read_text(encoding="utf-8", errors="ignore")
    required_owner = (
        "pin out bit safety_force;",
        "pin out bit safety_reset;",
        "pin out bit heartbeat;",
        "pin out bit rtcp_ui_request;",
        "pin in float rtcp_actual;",
        "V5_NATIVE_HAL_OWNER_SOCKET_PATH",
        "V5_NATIVE_HAL_OWNER_OP_ESTOP_FORCE",
        "V5_NATIVE_HAL_OWNER_OP_ESTOP_RESET",
        "V5_NATIVE_HAL_OWNER_OP_RTCP_SET",
    )
    for token in required_owner:
        if token not in owner_text:
            print(
                f"CC_GOLDEN_NATIVE_HAL_OWNER_CONTRACT_MISSING: {native_hal_owner.relative_to(ROOT.parent)} lacks {token}",
                file=sys.stderr,
            )
            rc = 1
    for retired in (
        ROOT / "services" / "state_publisher" / "v5_rtcp_status_publisher.py",
        ROOT / "services" / "microkernel" / "v5_g53_geometry_memory_owner.py",
        ROOT / "services" / "microkernel" / "v5_native_safety_latch_owner.py",
    ):
        if retired.exists():
            print(f"CC_GOLDEN_RETIRED_PYTHON_HAL_OWNER_SURVIVOR: {retired.relative_to(ROOT)}", file=sys.stderr)
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
            source not in MERGED_PARAMETER_TABLE_DEPLOY_ROWS or
            MERGED_PARAMETER_TABLE_DEPLOY_ROWS.get(source) != destination or
            mode != "0644"
        ):
            print(
                "SETTINGS_TABLE_DEPLOY_MERGE_NOT_REGISTERED: "
                f"{manifest.relative_to(ROOT)}:{line_no} {source} -> {destination} mode={mode}",
                file=sys.stderr,
            )
            rc = 1
        rows[source] = (kind, destination, mode, line_no)
    for source, destination in MERGED_PARAMETER_TABLE_DEPLOY_ROWS.items():
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


def check_settings_parameter_table_transaction_merge() -> int:
    script = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    if not script.exists():
        print("SETTINGS_TABLE_INSTALL_SCRIPT_MISSING: tools/deploy/install_v5_runtime.sh", file=sys.stderr)
        return 1
    text = script.read_text(encoding="utf-8", errors="ignore")
    forbidden = (
        "config/settings/microkernel_parameter_table.tsv:/opt/8ax/v5/config/settings/microkernel_parameter_table.tsv:0644",
        "parameter_table_backup_dir",
        "V5_PARAMETER_TABLE_BACKUP_DIR",
        "$project_root/bak",
        ".bak.",
        'read_rows(dst, False)',
    )
    for token in forbidden:
        if token in text:
            print(f"SETTINGS_TABLE_MERGE_FORBIDDEN_TOKEN: {script.relative_to(ROOT)} has {token}", file=sys.stderr)
            return 1
    required = (
        "config/settings/self_parameter_table.tsv:/opt/8ax/v5/config/settings/self_parameter_table.tsv:0644",
        "config/settings/drive_parameter_table.tsv:/opt/8ax/v5/config/settings/drive_parameter_table.tsv:0644",
        'vm_build_root="${VM_BUILD_ROOT:-/root/v5-build}"',
        'parameter_table_snapshot_root="$vm_build_root/temp_parameter_snapshot"',
        "parameter_table_transaction_snapshot()",
        "parameter_table_transaction_restore()",
        "parameter_table_transaction_cleanup()",
        "parameter_table_transaction_on_exit()",
        "parameter_table_transaction_complete()",
        'trap \'parameter_table_transaction_on_exit "$?"\' 0',
        'merged_artifact="$parameter_table_transaction_entry/merged.tsv"',
        'read_rows(dst, "board")',
        "if (axis, field) in template_keys",
        "malformed %s parameter table row",
        "duplicate %s parameter key",
        "expected_text",
        "artifact.write_text",
        "os.replace(temporary, dst)",
        "actual_text = dst.read_text",
        "actual_keys",
        "expected_keys",
        'parameter_table_transaction_snapshot "$destination"',
        "parameter_table_transaction_complete",
        "format=ok",
    )
    for token in required:
        if token not in text:
            print(f"SETTINGS_TABLE_TRANSACTION_MERGE_MISSING: {script.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            return 1
    if text.index('parameter_table_transaction_snapshot "$destination"') > text.index("artifact.write_text"):
        print("SETTINGS_TABLE_SNAPSHOT_AFTER_ARTIFACT: snapshot must happen before artifact write", file=sys.stderr)
        return 1
    if text.index('parameter_table_transaction_snapshot "$destination"') > text.index("os.replace(temporary, dst)"):
        print("SETTINGS_TABLE_SNAPSHOT_AFTER_REPLACE: snapshot must happen before replace", file=sys.stderr)
        return 1
    if text.rindex("parameter_table_transaction_complete") < text.index("done < \"$manifest\""):
        print("SETTINGS_TABLE_TRANSACTION_COMPLETES_BEFORE_INSTALL_LOOP", file=sys.stderr)
        return 1
    return 0


def check_unique_windows_source_delivery() -> int:
    script = ROOT / "tools" / "deploy" / "push_v5_runtime_to_board.sh"
    if not script.exists():
        print("UNIQUE_SOURCE_DEPLOY_SCRIPT_MISSING: tools/deploy/push_v5_runtime_to_board.sh", file=sys.stderr)
        return 1
    text = script.read_text(encoding="utf-8", errors="ignore")
    forbidden = (
        "refresh_board_owner_files",
        "merge_board_self_parameter_table",
        "pull_board_owner_file",
        "local_backup_dir",
        "--refresh-board-owner-files",
        "V5_LOCAL_OWNER_BACKUP_DIR",
        "shutil.copy2(local, backup)",
        "os.replace(tmp_local, local)",
    )
    for token in forbidden:
        if token in text:
            print(f"UNIQUE_SOURCE_RETIRED_BOARD_PULL_PRESENT: {script.relative_to(ROOT)} contains {token}", file=sys.stderr)
            return 1
    sync_script = ROOT / "tools" / "sync_win_source_to_vm.py"
    if sync_script.exists():
        print(f"UNIQUE_SOURCE_RETIRED_VM_SYNC_PRESENT: {sync_script.relative_to(ROOT)}", file=sys.stderr)
        return 1
    deploy_scripts = tuple((ROOT / "tools" / "deploy").glob("*.sh"))
    for deploy_script in deploy_scripts:
        deploy_text = deploy_script.read_text(encoding="utf-8", errors="ignore")
        if "/root/Desktop/v5" in deploy_text:
            print(f"UNIQUE_SOURCE_RETIRED_VM_MIRROR_PATH: {deploy_script.relative_to(ROOT)}", file=sys.stderr)
            return 1
    return 0


def check_product_runtime_closure_policy() -> int:
    manifest_path = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    cmake_path = ROOT / "app" / "CMakeLists.txt"
    closure_path = ROOT / "tools" / "deploy" / "verify_v5_product_source_closure.py"
    file_manifest_path = ROOT / "tools" / "deploy" / "v5_product_file_manifest.py"
    write_sd_path = ROOT / "tools" / "petalinux" / "write_v5_sd_card.sh"
    update_sd_path = ROOT / "tools" / "petalinux" / "update_v5_sd_from_qspi_recovery.sh"
    acceptance_path = ROOT / "tools" / "deploy" / "run_v5_board_acceptance.sh"
    installer_path = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    runtime_startup_path = (
        ROOT / "services" / "runtime_startup" / "init.d" / "v5-runtime-startup"
    )
    runtime_startup_smoke_path = (
        ROOT / "services" / "runtime_startup" / "v5_runtime_startup_smoke.py"
    )
    ui_init_path = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    ui_ready_path = ROOT / "services" / "ui" / "v5_ui_boot_ready.py"
    actiond_init_path = (
        ROOT / "services" / "drive_profile" / "init.d" / "v5-settings-actiond"
    )
    required_paths = (
        manifest_path,
        cmake_path,
        closure_path,
        file_manifest_path,
        write_sd_path,
        update_sd_path,
        acceptance_path,
        installer_path,
        runtime_startup_path,
        runtime_startup_smoke_path,
        ui_init_path,
        ui_ready_path,
        actiond_init_path,
    )
    for path in required_paths:
        if not path.is_file():
            print(f"PRODUCT_RUNTIME_CLOSURE_OWNER_MISSING: {path}", file=sys.stderr)
            return 1

    manifest_text = manifest_path.read_text(encoding="utf-8", errors="strict")
    for row in (
        "binary\tbuild/board/app/v5_command_gate_drive_window\t/usr/libexec/8ax/v5_command_gate_drive_window\t0755",
        "module\tservices/drive_profile/v5_drive_enable_window.py\t/usr/libexec/8ax/drive_profile/v5_drive_enable_window.py\t0644",
        "module\tservices/drive_profile/v5_drive_bus_apply_action.py\t/usr/libexec/8ax/drive_profile/v5_drive_bus_apply_action.py\t0644",
        "module\tservices/drive_profile/v5_drive_bus_reset_action.py\t/usr/libexec/8ax/drive_profile/v5_drive_bus_reset_action.py\t0644",
        "module\tservices/drive_profile/v5_drive_axis_zero.py\t/usr/libexec/8ax/drive_profile/v5_drive_axis_zero.py\t0644",
        "module\tservices/drive_profile/v5_drive_runtime_schema.py\t/usr/libexec/8ax/drive_profile/v5_drive_runtime_schema.py\t0644",
        "module\tservices/ui/v5_remote_ui_relay_access.py\t/usr/libexec/8ax/v5_remote_ui_relay_access.py\t0644",
        "module\tservices/ui/v5_remote_ui_relay_stream.py\t/usr/libexec/8ax/v5_remote_ui_relay_stream.py\t0644",
        "module\tservices/ui/v5_ui_boot_inputs.py\t/usr/libexec/8ax/v5_ui_boot_inputs.py\t0644",
        "init\tservices/runtime_startup/init.d/v5-runtime-startup\t/etc/init.d/v5-runtime-startup\t0755",
    ):
        if row not in manifest_text.splitlines():
            print(f"DRIVE_ENABLE_WINDOW_DEPLOY_ROW_MISSING: {row}", file=sys.stderr)
            return 1

    binary_targets = []
    destinations = set()
    for line_number, raw in enumerate(manifest_text.splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 4:
            print(f"PRODUCT_RUNTIME_MANIFEST_INVALID: line={line_number}", file=sys.stderr)
            return 1
        kind, source, destination, _mode = fields
        if destination in destinations:
            print(f"PRODUCT_RUNTIME_DESTINATION_DUPLICATE: {destination}", file=sys.stderr)
            return 1
        destinations.add(destination)
        if kind == "binary" and source.startswith("build/board/app/"):
            binary_targets.append(Path(source).name)
        elif kind == "binary" and source != "build/ethercat/lcec.so":
            print(
                f"PRODUCT_RUNTIME_BINARY_OWNER_UNREGISTERED: {source}",
                file=sys.stderr,
            )
            return 1
    if not binary_targets or len(binary_targets) != len(set(binary_targets)):
        print("PRODUCT_RUNTIME_BINARY_TARGETS_INVALID", file=sys.stderr)
        return 1

    cmake_text = cmake_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "V5_RUNTIME_DEPLOY_MANIFEST",
        "list(APPEND V5_PRODUCT_RUNTIME_TARGETS",
        "add_custom_target(v5_product_runtime DEPENDS ${V5_PRODUCT_RUNTIME_TARGETS})",
    ):
        if token not in cmake_text:
            print(f"PRODUCT_RUNTIME_CMAKE_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    for target in binary_targets:
        executable_owner = re.search(
            rf"add_executable\(\s*{re.escape(target)}(?:\s|\))", cmake_text)
        shared_library_owner = re.search(
            rf"add_library\(\s*{re.escape(target)}\s+SHARED(?:\s|\))", cmake_text)
        if executable_owner is None and shared_library_owner is None:
            print(f"PRODUCT_RUNTIME_CMAKE_TARGET_MISSING: {target}", file=sys.stderr)
            return 1

    write_sd = write_sd_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "verify_v5_product_source_closure.py",
        "v5_product_file_manifest.py",
        "--prepare-cmake-query",
        "--validate-shell",
        "--target v5_product_runtime",
        "v5-rootfs-file-manifest.tsv",
        '"$product_file_manifest_tool" create',
        '"$product_file_manifest_tool" verify',
        "disable_service()",
        "enable_service v5-runtime-startup 05 14",
        'rm -f "$rootfs_stage/etc/rc${level}.d"/S??ethercat',
        "schema=v5-sd-card-build-v2",
        "--boot-script-only",
        "V5_SD_BOOT_SCRIPT_STAGE_OK",
        "build_and_verify_boot_script",
    ):
        if token not in write_sd:
            print(f"PRODUCT_RUNTIME_SD_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    for retired in (
        "enable_service v5-linuxcnc-command-gate",
        "enable_service v5-position-status-publisher",
        "enable_service v5-wcs-status-publisher",
        "enable_service v5-state-publisher",
        "enable_service v5-settings-actiond",
        "enable_service v5-ui-relay",
    ):
        if retired in write_sd:
            print(f"PRODUCT_RUNTIME_SD_SHADOW_BOOT_LINK: {retired}", file=sys.stderr)
            return 1
    update_sd = update_sd_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "--boot-script-sha256",
        "V5_QSPI_BOOT_SCRIPT_INPUT_OK",
        "V5_QSPI_BOOT_SCRIPT_UPDATE_OK",
        'rootfs=untouched',
        'mount -t vfat -o rw "$boot_partition" "$boot_mount"',
        'mv -f "$boot_mount/boot.scr.new" "$boot_mount/boot.scr"',
        "if product_isolcpus:",
        "product bootargs must not isolate the ARM boot CPU",
        "QSPI recovery bootargs must not isolate a CPU",
    ):
        if token not in update_sd:
            print(f"PRODUCT_FOCUSED_BOOT_UPDATE_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    acceptance = acceptance_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        'V5_BOARD_BUILD_TARGETS:-v5_lvgl_shell v5_state_publisher v5_position_status_publisher v5_touch_diagnostics v5_linuxcncrsh_probe v5_command_gate_server v5_command_gate_drive_window v5_linuxcncrsh_golden_run',
        "CMAKE_C_COMPILER:FILEPATH=",
        "arm-xilinx-linux-gnueabi-gcc",
        "verify_v5_product_source_closure.py",
        "--prepare-cmake-query",
        "--validate-shell",
    ):
        if token not in acceptance:
            print(f"PRODUCT_RUNTIME_ACCEPTANCE_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    installer = installer_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "/usr/libexec/8ax/v5_command_gate_drive_window|\\",
        "manifest_command_gate_only=1",
        "restart_scope=command_gate",
        "Command-gate restart scope requires a non-empty command-gate-only manifest and no LinuxCNC bundle",
        "restart_runtime_event_dag()",
        "/etc/init.d/v5-runtime-startup restart",
        "disable_unconditional_ethercat_autostart",
        "enable_runtime_startup_boot_graph",
        "enable_auxiliary_boot_service v5-runtime-startup 05 14",
        'disable_boot_service "$service"',
    ):
        if token not in installer:
            print(f"DRIVE_ENABLE_WINDOW_COMMAND_GATE_SCOPE_MISSING: {token}", file=sys.stderr)
            return 1
    for retired in (
        "enable_boot_service()",
        "enable_boot_service_raw()",
        "enable_boot_service v5-linuxcnc-command-gate",
        "enable_boot_service v5-position-status-publisher",
        "enable_boot_service v5-wcs-status-publisher",
        "enable_boot_service v5-state-publisher",
        "enable_boot_service v5-ui-relay",
        "enable_boot_service v5-settings-actiond",
    ):
        if retired in installer:
            print(f"RUNTIME_STARTUP_RETIRED_BOOT_ENTRY_SURVIVOR: {retired}", file=sys.stderr)
            return 1
    runtime_startup = runtime_startup_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "pin_userspace_housekeeping_to_cpu1() {",
        'rtapi_app:T#*)',
        '/usr/bin/taskset -pc 1 "$tid"',
        "Cpus_allowed_list:",
        "  if ! pin_userspace_housekeeping_to_cpu1; then",
        '"$COMMAND_GATE" start >>"$LOGFILE" 2>&1 &',
        '"$ACTIOND" start >>"$LOGFILE" 2>&1 &',
        'start_publishers_after_data_ready >>"$LOGFILE" 2>&1 &',
        '"$BACKEND_READINESS_PROBE" --wait data --timeout-ms 120000',
        '"$POSITION" start &',
        '"$WCS" start &',
        '"$STATE" start &',
        '"$UI" start >>"$LOGFILE" 2>&1 &',
        "rollback_start",
        '"$BACKEND_READINESS_PROBE" --require motion',
    ):
        if token not in runtime_startup:
            print(f"RUNTIME_STARTUP_EVENT_DAG_MISSING: {token}", file=sys.stderr)
            return 1
    ui_init = ui_init_path.read_text(encoding="utf-8", errors="strict")
    wait_start = ui_init.find("wait_boot_inputs_ready() {")
    wait_end = ui_init.find("\nstale_service_pids() {", wait_start)
    wait_inputs = (
        ui_init[wait_start:wait_end]
        if wait_start >= 0 and wait_end > wait_start
        else ""
    )
    if (
        '"$BACKEND_READINESS_PROBE" --wait data --timeout-ms 120000'
        not in wait_inputs
        or "/proc/[0-9]*/cmdline" in wait_inputs
        or "PROFILE_SNAPSHOT" in ui_init
        or "build_profile_snapshot" in ui_init
        or '--backend-readiness-probe "$BACKEND_READINESS_PROBE"' not in ui_init
    ):
        print("UI_EVENT_DAG_INPUT_OR_FINAL_BARRIER_INVALID", file=sys.stderr)
        return 1
    ui_ready = ui_ready_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        "def require_backend_motion_ready",
        'payload["backend_ready"] = backend_ready',
        "atomic_write_json(args.ready_path, payload)",
        'parser.add_argument("--backend-readiness-probe", type=Path)',
    ):
        if token not in ui_ready:
            print(f"UI_FINAL_BACKEND_READY_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    actiond_init = actiond_init_path.read_text(encoding="utf-8", errors="strict")
    actiond_start = actiond_init.find("start_service() {")
    actiond_stop = actiond_init.find("\nstop_service() {", actiond_start)
    actiond_start_block = (
        actiond_init[actiond_start:actiond_stop]
        if actiond_start >= 0 and actiond_stop > actiond_start
        else ""
    )
    if (
        actiond_init.count("v5_drive_profile_resident_snapshot.py") != 1
        or 'rm -f "$PROFILE_SNAPSHOT"' not in actiond_start_block
        or (
            '"$PROFILE_SNAPSHOT_BUILDER" --profile-root /opt/8ax/drive-profiles \\\n'
            '    --out "$PROFILE_SNAPSHOT" >/run/8ax/v5_drive_profile_resident_snapshot.log 2>&1\n'
        ) not in actiond_start_block
    ):
        print("PROFILE_SNAPSHOT_STARTUP_OWNER_COUNT_INVALID", file=sys.stderr)
        return 1
    command_gate_scope_start = installer.find('elif [ "$restart_scope" = "command_gate" ]')
    command_gate_scope_end = installer.find(
        'elif [ "$restart_scope" = "backend" ]', command_gate_scope_start
    )
    command_gate_scope = (
        installer[command_gate_scope_start:command_gate_scope_end]
        if command_gate_scope_start >= 0 and command_gate_scope_end > command_gate_scope_start
        else ""
    )
    if (
        not command_gate_scope
        or command_gate_scope.count("restart_runtime_event_dag") != 1
        or "restart-native" in command_gate_scope
        or "/etc/init.d/v5-linuxcnc-command-gate restart\n" in command_gate_scope
        or "/etc/init.d/v5-settings-actiond restart\n" in command_gate_scope
    ):
        print("DRIVE_GENERATION_COMMAND_GATE_SCOPE_BACKEND_RESTART_INVALID", file=sys.stderr)
        return 1
    cpu_policy_scope_start = installer.find('elif [ "$restart_scope" = "cpu_policy" ]')
    cpu_policy_scope_end = installer.find(
        'elif [ "$restart_scope" = "settings" ]', cpu_policy_scope_start
    )
    cpu_policy_scope = (
        installer[cpu_policy_scope_start:cpu_policy_scope_end]
        if cpu_policy_scope_start >= 0 and cpu_policy_scope_end > cpu_policy_scope_start
        else ""
    )
    if (
        not cpu_policy_scope
        or cpu_policy_scope.count("restart_runtime_event_dag") != 1
        or "restart-native" in cpu_policy_scope
        or "/etc/init.d/v5-linuxcnc-command-gate restart\n" in cpu_policy_scope
        or "/etc/init.d/v5-settings-actiond restart\n" in cpu_policy_scope
        or cpu_policy_scope.find("apply_cpu_policy_after_install") >
           cpu_policy_scope.find("restart_runtime_event_dag")
    ):
        print("DRIVE_GENERATION_CPU_POLICY_SCOPE_BACKEND_RESTART_INVALID", file=sys.stderr)
        return 1
    for script in (closure_path, file_manifest_path):
        compile(
            script.read_text(encoding="utf-8", errors="strict"),
            str(script),
            "exec",
        )
    return 0


def main() -> int:
    return (
        check_shm_consumers() |
        check_shm_abi() |
        check_cpu_policy() |
        check_bus_1ms_cycle_policy() |
        check_linuxcnc_rtapi_affinity_owner() |
        check_linuxcnc_control_transport_policy() |
        check_uio_device_permission_policy() |
        check_ethercat_device_permission_policy() |
        check_tcf_retirement_policy() |
        check_settings_runtime_schema_guard() |
        check_retired_drive_tuning_chain() |
        check_settings_actiond_socket_policy() |
        check_remote_relay_access_control() |
        check_cc_golden_model_specific_motion() |
        check_rotary_native_target_policy() |
        check_linuxcnc_source_rebuild_policy() |
        check_settings_parameter_table_deploy_kind() |
        check_settings_parameter_table_transaction_merge() |
        check_unique_windows_source_delivery() |
        check_product_runtime_closure_policy()
    )


if __name__ == "__main__":
    raise SystemExit(main())
