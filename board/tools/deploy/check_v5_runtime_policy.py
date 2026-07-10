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
    relay = ROOT / "services" / "ui" / "v5_remote_ui_relay.py"
    init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
    if not source.exists() or not relay.exists() or not init.exists():
        print("REMOTE_RELAY_ACCESS_CONTROL_MISSING_SOURCE", file=sys.stderr)
        return 1
    source_text = source.read_text(encoding="utf-8", errors="ignore")
    relay_text = relay.read_text(encoding="utf-8", errors="ignore")
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
            print(f"REMOTE_RELAY_PYTHON_BOUNDARY_MISSING: {relay.relative_to(ROOT)} lacks {token}", file=sys.stderr)
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


def check_linuxcnc_rtapi_affinity_owner() -> int:
    init = ROOT / "services" / "command_gate" / "init.d" / "v5-linuxcnc-command-gate"
    ui_init = ROOT / "services" / "ui" / "init.d" / "v5-ui-relay"
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
    text = init.read_text(encoding="utf-8", errors="ignore")
    required = (
        "linuxcnc_realtime_pids",
        "linuxcnc_realtime_affinity_ok",
        "set_linuxcnc_realtime_affinity",
        "rtapi_app",
        "taskset -a -pc 0",
        "Cpus_allowed_list",
        "ethercat_irq_affinity_ok",
        "ethercat_kernel_affinity_ok",
        "set_ethercat_realtime_affinity",
        "RTAPI and EtherCAT realtime paths pinned to CPU0",
    )
    rc = 0
    for token in required:
        if token not in text:
            print(f"LINUXCNC_RTAPI_AFFINITY_OWNER_MISSING: {init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1
    for token in ("ensure_machine_on_at_boot", "--machine-on"):
        if token in text:
            print(f"LINUXCNC_MACHINE_ON_EARLY_INIT_SURVIVOR: {init.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1
    ui_text = ui_init.read_text(encoding="utf-8", errors="ignore")
    required_ui = (
        "wait_boot_inputs_ready",
        "drive_faults_clear",
        "wait_drive_faults_clear_for_machine_on",
        "joint.$joint.amp-fault-in",
        "ensure_machine_on_after_microkernel_ready",
        "--machine-on",
        "Machine On confirmed after microkernel ready",
    )
    for token in required_ui:
        if token not in ui_text:
            print(f"LINUXCNC_MACHINE_ON_UI_INIT_CONTRACT_MISSING: {ui_init.relative_to(ROOT)} lacks {token}", file=sys.stderr)
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


def check_rotary_wrapped_mask_policy() -> int:
    rc = 0
    bus_hal = ROOT / "linuxcnc" / "hal" / "v5_bus_2ms.hal"
    bus_ini = ROOT / "linuxcnc" / "ini" / "v5_bus.ini"
    if not bus_hal.exists() or not bus_ini.exists():
        print("ROTARY_WRAPPED_MASK_BUS_SOURCE_MISSING", file=sys.stderr)
        rc = 1
    else:
        hal_text = bus_hal.read_text(encoding="utf-8", errors="ignore")
        ini_text = bus_ini.read_text(encoding="utf-8", errors="ignore")
        required_hal = (
            "loadrt [RTCP]KINS_MODULE coordinates=[RTCP]KINS_COORDINATES sparm=identityfirst",
            "z20_wrapped_rotary_mask=[RTCP]WRAPPED_ROTARY_MASK",
            "motion.tooloffset.z => [RTCP]KINS_TOOL_OFFSET_PIN",
            "setp [RTCP]KINS_X_ROT_POINT_PIN [RTCP]X_ROT_POINT",
        )
        for token in required_hal:
            if token not in hal_text:
                print(f"ACTIVE_MODEL_KINS_HAL_CONTRACT_MISSING: {bus_hal.relative_to(ROOT)} lacks {token}", file=sys.stderr)
                rc = 1
        forbidden_hal = (
            "loadrt xyzac-trt-kins",
            "xyzac-trt-kins.tool-offset",
            "setp xyzac-trt-kins.",
        )
        for token in forbidden_hal:
            if token in hal_text:
                print(f"ACTIVE_MODEL_KINS_HAL_HARDCODED_AC: {bus_hal.relative_to(ROOT)} contains {token}", file=sys.stderr)
                rc = 1
        required_ini = (
            "MODEL = XYZAC_TRT",
            "KINS_MODULE = xyzac-trt-kins",
            "KINS_COORDINATES = XYZAC",
            "KINS_PREFIX = xyzac-trt-kins",
            "KINS_TOOL_OFFSET_PIN = xyzac-trt-kins.tool-offset",
            "KINS_X_ROT_POINT_PIN = xyzac-trt-kins.x-rot-point",
            "WRAPPED_ROTARY_MASK = 24",
        )
        for token in required_ini:
            if token not in ini_text:
                print(f"ACTIVE_MODEL_KINS_INI_DEFAULT_MISSING: {bus_ini.relative_to(ROOT)} lacks {token}", file=sys.stderr)
                rc = 1

    expected = (
        (
            ROOT / "linuxcnc" / "hal" / "v5_pulse.hal",
            "loadrt motmod",
            "z20_wrapped_rotary_mask=40",
            "Pulse xyzabc A/C wrapped rotary mask must be 0x28",
        ),
    )
    for path, loadrt_token, mask_token, reason in expected:
        if not path.exists():
            print(f"ROTARY_WRAPPED_MASK_HAL_MISSING: {path.relative_to(ROOT)}", file=sys.stderr)
            rc = 1
            continue
        matched_line = None
        for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("#"):
                continue
            if loadrt_token in line:
                matched_line = (line_no, line)
                break
        if matched_line is None:
            print(
                f"ROTARY_WRAPPED_MASK_LOADRT_MISSING: {path.relative_to(ROOT)} lacks {loadrt_token}",
                file=sys.stderr,
            )
            rc = 1
            continue
        line_no, line = matched_line
        if "z20_wrapped_rotary_mask=0" in line or mask_token not in line:
            print(
                f"ROTARY_WRAPPED_MASK_WRONG: {path.relative_to(ROOT)}:{line_no}: "
                f"{reason}; line must contain {mask_token}: {line.strip()}",
                file=sys.stderr,
            )
            rc = 1
    return rc


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


def check_cc_golden_rtcp_before_ac_motion() -> int:
    program = ROOT / "gcode" / "golden" / "cc.ngc"
    runner = ROOT / "services" / "command_gate" / "v5_linuxcncrsh_golden_run.c"
    hal = ROOT / "linuxcnc" / "hal" / "v5_bus_2ms.hal"
    rtcp_publisher = ROOT / "services" / "state_publisher" / "v5_rtcp_status_publisher.py"
    if not program.exists():
        print("CC_GOLDEN_PROGRAM_MISSING: gcode/golden/cc.ngc", file=sys.stderr)
        return 1
    if not runner.exists():
        print("CC_GOLDEN_RUNNER_MISSING: services/command_gate/v5_linuxcncrsh_golden_run.c", file=sys.stderr)
        return 1
    if not hal.exists():
        print("CC_GOLDEN_RTCP_HAL_MISSING: linuxcnc/hal/v5_bus_2ms.hal", file=sys.stderr)
        return 1
    if not rtcp_publisher.exists():
        print("CC_GOLDEN_RTCP_PUBLISHER_MISSING: services/state_publisher/v5_rtcp_status_publisher.py", file=sys.stderr)
        return 1
    pending_spring_anchor = False
    seen_spring_anchor = False
    seen_cutting_trajectory = False
    seen_spring_feed = False
    seen_program_rtcp_on = False
    seen_program_rtcp_off = False
    seen_machine_return = False
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
        if re.search(r"\bG43\.4\b", line):
            print(f"CC_GOLDEN_UNSUPPORTED_G43_4: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}", file=sys.stderr)
            return 1
        has_motion = re.search(r"\bG(?:0|00|1|01)\b", line) is not None
        has_spring_feed = re.search(r"\bG(?:1|01)\b", line) is not None
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
        if seen_cutting_trajectory and has_spring_feed:
            if not seen_program_rtcp_on:
                print(
                    f"CC_GOLDEN_PROGRAM_RTCP_ON_AFTER_SPRING_START: {program.relative_to(ROOT)}:{line_no}: {raw.strip()}",
                    file=sys.stderr,
                )
                return 1
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
    if not seen_spring_anchor:
        print("CC_GOLDEN_PROGRAM_SPRING_ANCHOR_MISSING: gcode/golden/cc.ngc lacks spring anchor motion", file=sys.stderr)
        return 1
    if not seen_spring_feed:
        print("CC_GOLDEN_PROGRAM_SPRING_FEED_MISSING: gcode/golden/cc.ngc lacks spring G1 feed", file=sys.stderr)
        return 1
    if not seen_program_rtcp_on:
        print("CC_GOLDEN_PROGRAM_RTCP_ON_MISSING: gcode/golden/cc.ngc lacks M64 P0", file=sys.stderr)
        return 1
    if not seen_program_rtcp_off:
        print("CC_GOLDEN_PROGRAM_RTCP_OFF_MISSING: gcode/golden/cc.ngc lacks M65 P0", file=sys.stderr)
        return 1

    rc = 0
    runner_text = runner.read_text(encoding="utf-8", errors="ignore")
    forbidden_runner = (
        "v5_native_rtcp_control_set(1",
        "ensure_rtcp_on_before_golden_motion",
        "rtcp on confirmed before golden motion",
    )
    for token in forbidden_runner:
        if token in runner_text:
            print(f"CC_GOLDEN_RUNNER_EXTERNAL_RTCP_SURVIVOR: {runner.relative_to(ROOT)} contains {token}", file=sys.stderr)
            rc = 1

    hal_text = hal.read_text(encoding="utf-8", errors="ignore")
    required_hal = (
        "loadrt or2 count=1",
        "addf or2.0 servo-thread",
        "v5-rtcp-ui-request",
        "v5-rtcp-gcode-request motion.digital-out-00 => or2.0.in1",
        "v5-rtcp-selected or2.0.out => mux2.0.sel",
        "re-switchkins-select mux2.0.out => motion.switchkins-type",
    )
    for token in required_hal:
        if token not in hal_text:
            print(f"CC_GOLDEN_RTCP_HAL_CONTRACT_MISSING: {hal.relative_to(ROOT)} lacks {token}", file=sys.stderr)
            rc = 1

    publisher_text = rtcp_publisher.read_text(encoding="utf-8", errors="ignore")
    if "v5-rtcp-ui-request" not in publisher_text:
        print(
            f"CC_GOLDEN_RTCP_UI_LATCH_SIGNAL_MISSING: {rtcp_publisher.relative_to(ROOT)} lacks v5-rtcp-ui-request",
            file=sys.stderr,
        )
        rc = 1
    if "set_p('mux2.0.sel'" in publisher_text or 'set_p("mux2.0.sel"' in publisher_text:
        print(
            f"CC_GOLDEN_RTCP_DIRECT_MUX_WRITE_SURVIVOR: {rtcp_publisher.relative_to(ROOT)} still writes mux2.0.sel",
            file=sys.stderr,
        )
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
        "scp_legacy_protocol_opt",
        "scp $scp_legacy_protocol_opt -q",
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
        check_linuxcnc_rtapi_affinity_owner() |
        check_settings_runtime_schema_guard() |
        check_remote_relay_access_control() |
        check_cc_golden_rtcp_before_ac_motion() |
        check_rotary_wrapped_mask_policy() |
        check_settings_parameter_table_deploy_kind() |
        check_settings_parameter_table_backup_before_merge() |
        check_board_owner_refresh_before_bundle()
    )


if __name__ == "__main__":
    raise SystemExit(main())
