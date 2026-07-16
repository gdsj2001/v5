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
SELF_PARAMETER_TABLE_DEPLOY_ROW = {
    "config/settings/self_parameter_table.tsv": "/opt/8ax/v5/config/settings/self_parameter_table.tsv",
}
BOARD_OWNER_DEPLOY_ROWS = {
    "config/settings/drive_parameter_table.tsv": ("runtime_seed", "/opt/8ax/v5/config/settings/drive_parameter_table.tsv", "0644"),
    "config/settings/settings_runtime.json": ("runtime_seed", "/opt/8ax/phase0_bus5/settings_runtime.json", "0644"),
    "linuxcnc/ini/v5_bus.ini": ("linuxcnc", "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini", "0644"),
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
    relay = ROOT / "services" / "ui" / "v5_remote_ui_relay.py"
    relay_modules = (
        relay,
        ROOT / "services" / "ui" / "v5_remote_ui_state.py",
        ROOT / "services" / "ui" / "v5_remote_ui_support.py",
        ROOT / "services" / "ui" / "v5_remote_ui_protocol.py",
        ROOT / "services" / "ui" / "v5_remote_ui_contract.py",
    )
    init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    if not source.exists() or not all(path.exists() for path in relay_modules) or not init.exists():
        print("REMOTE_RELAY_ACCESS_CONTROL_MISSING_SOURCE", file=sys.stderr)
        return 1
    source_text = source.read_text(encoding="utf-8", errors="ignore")
    relay_text = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in relay_modules)
    init_text = init.read_text(encoding="utf-8", errors="ignore")
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
        "stream_idle_pings",
        "stream_send_failures",
        "input_active_sessions",
    )
    for token in required_relay:
        if token not in relay_text:
            print(f"REMOTE_RELAY_PYTHON_BOUNDARY_MISSING: canonical relay modules lack {token}", file=sys.stderr)
            rc = 1
    required_init = (
        "V5_UI_REMOTE_BIND",
        "V5_UI_REMOTE_ALLOW_CIDRS",
        "REMOTE_ALLOW_CIDRS",
        "v5_remote_ui_relay.py",
        "v5_ui_shell.pid",
    )
    for token in required_init:
        if token not in init_text:
            print(f"REMOTE_RELAY_INIT_ALLOWLIST_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
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
    relay_producer = ROOT / "services" / "ui" / "v5_remote_ui_shared_payload.py"
    deploy_manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    runtime_installer = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    usb_wifi_apply = net_core.with_name("v5_usb_wifi_apply.sh")
    probe = ROOT / "services" / "command_gate" / "v5_linuxcncrsh_probe.c"
    if not init.exists():
        print("LINUXCNC_RTAPI_AFFINITY_INIT_MISSING: services/command_gate/init.d/v5-linuxcnc-command-gate", file=sys.stderr)
        return 1
    if not probe.exists():
        print("LINUXCNC_MACHINE_ON_PROBE_MISSING: services/command_gate/v5_linuxcncrsh_probe.c", file=sys.stderr)
        return 1
    if not ui_init.exists():
        print("LINUXCNC_MACHINE_ON_UI_INIT_MISSING: services/ui/init.d/v5-ui-relay", file=sys.stderr)
        return 1
    if (
        not backend_lifecycle.exists()
        or not board_runtime_policy.exists()
        or not net_core.exists()
        or not usb_wifi_apply.exists()
        or not relay_producer.exists()
        or not deploy_manifest.exists()
        or not runtime_installer.exists()
        or any(not path.exists() for path in publisher_inits)
    ):
        print("CPU_ISOLATION_OWNER_MISSING", file=sys.stderr)
        return 1
    text = init.read_text(encoding="utf-8", errors="ignore")
    required = (
        "linuxcnc_realtime_pids",
        "linuxcnc_realtime_affinity_ok",
        "linuxcnc_privileged_helpers_ok",
        "linuxcnc_realtime_scheduler_ok",
        "/usr/bin/linuxcnc_module_helper",
        "rtapi_app:T*",
        "policy=$(awk '{print $41; exit}' \"$task/stat\"",
        "1|2",
        "LinuxCNC privileged helpers must be root:root 4755; refusing backend start",
        "realtime-scheduler",
        "set_linuxcnc_realtime_affinity",
        "rtapi_app",
        "taskset -a -pc 0",
        "Cpus_allowed_list",
        "ethercat_irq_affinity_ok",
        "ethercat_kernel_affinity_ok",
        "set_ethercat_realtime_affinity",
        "network_cpu_isolation_ok",
        "network_iface_irq_affinity_ok",
        "network_iface_rps_ok",
        "Network CPU isolation readback invalid; refusing LinuxCNC start",
        "Network CPU isolation readback invalid; refusing native-gate-only restart",
        "linuxcnc_non_realtime_pids",
        "linuxcnc_non_realtime_affinity_ok",
        "set_linuxcnc_non_realtime_affinity",
        "MICROKERNEL_NON_RT_NICE=-5",
        "linuxcnc_non_realtime_priority_ok",
        "set_linuxcnc_non_realtime_priority",
        'renice -n "$MICROKERNEL_NON_RT_NICE"',
        "linuxcncsvr milltask io linuxcncrsh v5_native_hal_owner",
        "taskset -a -pc 1",
        "EtherCAT IRQ affinity is not CPU0; network affinity owner must establish it before LinuxCNC starts",
        "RTAPI and EtherCAT realtime paths pinned to CPU0; LinuxCNC non-realtime motion paths protected on CPU1",
    )
    rc = 0
    for token in required:
        if token not in text:
            print(f"LINUXCNC_RTAPI_AFFINITY_OWNER_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    if '"$task/sched"' in text:
        print("LINUXCNC_RTAPI_SCHED_READER_SURVIVOR", file=sys.stderr)
        rc = 1
    lifecycle_text = backend_lifecycle.read_text(encoding="utf-8", errors="ignore")
    for token in (
        "linuxcnc_realtime_scheduler_ok",
        "LinuxCNC RTAPI servo thread remained outside the realtime scheduler",
    ):
        if token not in lifecycle_text:
            print(f"LINUXCNC_RTAPI_SCHEDULER_READINESS_MISSING: {token}", file=sys.stderr)
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
    )
    for token in required_net:
        if token not in net_text:
            print(f"NETWORK_CPU_ISOLATION_OWNER_MISSING: {net_core.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    if "apply_network_cpu_isolation" not in usb_wifi_apply.read_text(encoding="utf-8", errors="ignore"):
        print("USB_WIFI_CPU_ISOLATION_OWNER_MISSING", file=sys.stderr)
        rc = 1
    for publisher_init in publisher_inits:
        publisher_text = publisher_init.read_text(encoding="utf-8", errors="ignore")
        for token in (
            "NON_MICROKERNEL_NICE=10",
            'taskset -c 1 nice -n "$NON_MICROKERNEL_NICE"',
            'renice -n "$NON_MICROKERNEL_NICE"',
        ):
            if token not in publisher_text:
                print(
                    f"DISPLAY_PUBLISHER_CPU_BUDGET_POLICY_MISSING: "
                    f"{publisher_init.relative_to(ROOT)} lacks {token}",
                    file=sys.stderr,
                )
                rc = 1
    relay_producer_text = relay_producer.read_text(encoding="utf-8", errors="ignore")
    for token in (
        "FRAME_PRODUCER_NICE = 10",
        "os.nice(FRAME_PRODUCER_NICE)",
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
        "services/ui/v5_remote_ui_shared_payload.py\t/usr/libexec/8ax/v5_remote_ui_shared_payload.py\t0644",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_net_core.sh\t/usr/local/sbin/v5_net_core.sh\t0644",
        "petalinux/project-spec/meta-user/recipes-apps/v5-base-overlay/files/network/v5_usb_wifi_apply.sh\t/usr/local/sbin/v5_usb_wifi_apply.sh\t0755",
    )
    for row in required_cpu_policy_rows:
        if row not in manifest_text:
            print(f"CPU_POLICY_DEPLOY_ROW_MISSING: {row}", file=sys.stderr)
            rc = 1
    installer_text = runtime_installer.read_text(encoding="utf-8", errors="ignore")
    required_cpu_policy_installer = (
        "manifest_cpu_policy_only=1",
        "restart_scope=cpu_policy",
        'LOG=/run/8ax/v5_cpu_policy.log',
        "apply_network_cpu_isolation",
        "/etc/init.d/v5-linuxcnc-command-gate restart-native",
        "/etc/init.d/v5-wcs-status-publisher restart",
        "/etc/init.d/v5-state-publisher restart",
        "/etc/init.d/v5-ui-relay restart",
    )
    for token in required_cpu_policy_installer:
        if token not in installer_text:
            print(f"CPU_POLICY_DEPLOY_SCOPE_MISSING: {token}", file=sys.stderr)
            rc = 1
    cpu_scope_start = installer_text.find('elif [ "$restart_scope" = "cpu_policy" ]')
    cpu_scope_end = installer_text.find('elif [ "$restart_scope" = "settings" ]', cpu_scope_start)
    cpu_scope = installer_text[cpu_scope_start:cpu_scope_end] if cpu_scope_start >= 0 and cpu_scope_end > cpu_scope_start else ""
    if not cpu_scope or "/etc/init.d/v5-linuxcnc-command-gate restart\n" in cpu_scope:
        print("CPU_POLICY_SCOPE_RESTARTS_LINUXCNC_BACKEND", file=sys.stderr)
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
        "--machine-on",
        "ensure_machine_on",
        "v5_linuxcncrsh_send_machine_on_sequence",
        "machine on confirmed",
    )
    for token in required_probe:
        if token not in probe_text:
            print(f"LINUXCNC_MACHINE_ON_PROBE_CONTRACT_MISSING: {probe.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    verify = ROOT / "tools" / "deploy" / "verify_v5_board_runtime.sh"
    if not verify.exists():
        print("LINUXCNC_MACHINE_ON_VERIFY_MISSING: tools/deploy/verify_v5_board_runtime.sh", file=sys.stderr)
        rc = 1
    else:
        verify_text = verify.read_text(encoding="utf-8", errors="ignore")
        for token in ("MACHINE ON", "machine auto-on confirmed"):
            if token not in verify_text:
                print(f"LINUXCNC_MACHINE_ON_VERIFY_CONTRACT_MISSING: {verify.relative_to(ROOT)} lacks {token}", file=sys.stderr)
                rc = 1
    return rc


def check_rotary_native_target_policy() -> int:
    rc = 0
    bus_hal = ROOT / "linuxcnc" / "hal" / "v5_bus_2ms.hal"
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
        entry = raw.strip().removesuffix("\\").strip()
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
        'linuxcnc-prebuilt -c install -f',
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
        '--sync-registered-delta "$linuxcnc_projection"',
        'V5_LINUXCNC_PACKAGE_ONLY_OK package_root=$package_root',
    ):
        if required not in build_script:
            print(f"LINUXCNC_PACKAGE_ONLY_FAST_PATH_MISSING: {required}", file=sys.stderr)
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
        expected_c = -90.0 * (index + 1)
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


def _check_cc_golden_program(program: Path, expected_axis: str, forbidden_axis: str) -> int:
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
    return _check_cc_golden_arc_chain(program, expected_axis, spring_arcs)


def check_cc_golden_model_specific_motion() -> int:
    programs = (
        (ROOT / "gcode" / "golden" / "cc-ac.ngc", "A", "B"),
        (ROOT / "gcode" / "golden" / "cc-bc.ngc", "B", "A"),
    )
    legacy_program = ROOT / "gcode" / "golden" / "cc.ngc"
    runner = ROOT / "services" / "command_gate" / "v5_linuxcncrsh_golden_run.c"
    acceptance = ROOT / "tools" / "deploy" / "run_v5_board_acceptance.sh"
    manifest = ROOT / "config" / "deploy" / "v5_runtime_deploy_manifest.tsv"
    hal = ROOT / "linuxcnc" / "hal" / "v5_bus_2ms.hal"
    native_hal_owner = ROOT.parent / "linuxcnc" / "src" / "hal" / "user_comps" / "v5_native_hal_owner.comp"
    rc = 0
    if legacy_program.exists():
        print("CC_GOLDEN_LEGACY_PROGRAM_SURVIVOR: gcode/golden/cc.ngc", file=sys.stderr)
        rc = 1
    for program, expected_axis, forbidden_axis in programs:
        if not program.exists():
            print(f"CC_GOLDEN_PROGRAM_MISSING: {program.relative_to(ROOT)}", file=sys.stderr)
            rc = 1
            continue
        rc |= _check_cc_golden_program(program, expected_axis, forbidden_axis)
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
        print("CC_GOLDEN_RTCP_HAL_MISSING: linuxcnc/hal/v5_bus_2ms.hal", file=sys.stderr)
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
        "loadrt v5_safety_latch",
        "loadusr -Wn v5_native_hal_owner /usr/bin/v5_native_hal_owner",
        "addf or2.0 servo-thread",
        "addf or2.1 servo-thread",
        "addf v5-safety-latch.0 servo-thread",
        "v5-safety-force v5-native-hal-owner.safety-force => v5-safety-latch.0.force",
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
    acceptance_path = ROOT / "tools" / "deploy" / "run_v5_board_acceptance.sh"
    installer_path = ROOT / "tools" / "deploy" / "install_v5_runtime.sh"
    required_paths = (
        manifest_path,
        cmake_path,
        closure_path,
        file_manifest_path,
        write_sd_path,
        acceptance_path,
        installer_path,
    )
    for path in required_paths:
        if not path.is_file():
            print(f"PRODUCT_RUNTIME_CLOSURE_OWNER_MISSING: {path}", file=sys.stderr)
            return 1

    manifest_text = manifest_path.read_text(encoding="utf-8", errors="strict")
    for row in (
        "binary\tbuild/board/app/v5_command_gate_drive_window\t/usr/libexec/8ax/v5_command_gate_drive_window\t0755",
        "module\tservices/drive_profile/v5_drive_enable_window.py\t/usr/libexec/8ax/drive_profile/v5_drive_enable_window.py\t0644",
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
        if kind == "binary":
            binary_targets.append(Path(source).name)
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
        if re.search(rf"add_executable\(\s*{re.escape(target)}(?:\s|\))", cmake_text) is None:
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
        "schema=v5-sd-card-build-v2",
    ):
        if token not in write_sd:
            print(f"PRODUCT_RUNTIME_SD_GATE_MISSING: {token}", file=sys.stderr)
            return 1
    acceptance = acceptance_path.read_text(encoding="utf-8", errors="strict")
    for token in (
        'V5_BOARD_BUILD_TARGETS:-v5_lvgl_shell v5_state_publisher v5_touch_diagnostics v5_linuxcncrsh_probe v5_command_gate_server v5_command_gate_drive_window v5_linuxcncrsh_golden_run',
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
        "/etc/init.d/v5-linuxcnc-command-gate restart-native",
    ):
        if token not in installer:
            print(f"DRIVE_ENABLE_WINDOW_COMMAND_GATE_SCOPE_MISSING: {token}", file=sys.stderr)
            return 1
    command_gate_scope_start = installer.find('elif [ "$restart_scope" = "command_gate" ]')
    command_gate_scope_end = installer.find(
        'elif [ "$restart_scope" = "wcs" ]', command_gate_scope_start
    )
    command_gate_scope = (
        installer[command_gate_scope_start:command_gate_scope_end]
        if command_gate_scope_start >= 0 and command_gate_scope_end > command_gate_scope_start
        else ""
    )
    if not command_gate_scope or "/etc/init.d/v5-linuxcnc-command-gate restart\n" in command_gate_scope:
        print("DRIVE_ENABLE_WINDOW_COMMAND_GATE_SCOPE_RESTARTS_BACKEND", file=sys.stderr)
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
        check_linuxcnc_rtapi_affinity_owner() |
        check_settings_runtime_schema_guard() |
        check_settings_actiond_socket_policy() |
        check_remote_relay_access_control() |
        check_cc_golden_model_specific_motion() |
        check_rotary_native_target_policy() |
        check_linuxcnc_source_rebuild_policy() |
        check_settings_parameter_table_deploy_kind() |
        check_settings_parameter_table_backup_before_merge() |
        check_unique_windows_source_delivery() |
        check_product_runtime_closure_policy()
    )


if __name__ == "__main__":
    raise SystemExit(main())
