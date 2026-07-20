#!/usr/bin/env python3
from __future__ import annotations

import errno
import socket
import subprocess
import tempfile
import time
from pathlib import Path

INIT = Path(__file__).with_name("init.d") / "v5-linuxcnc-command-gate"
LIFECYCLE = Path(__file__).with_name("v5_ethercat_backend_lifecycle.sh")
ASSIGNMENT = "RTAPI_FIFO_PATH=$RUN_DIR/v5_rtapi_fifo"
ENV_PREFIX = "RTAPI_FIFO_PATH='$RTAPI_FIFO_PATH' TCLLIBPATH=/usr/lib/tcltk /usr/bin/linuxcnc"


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def validate_init(text: str) -> None:
    cpu_bind = section(text, "bind_service_init_to_cpu1() {", "\nLD_BIND_NOW=1")
    assert 'done <"/proc/$$/status"' in cpu_bind
    assert '[ "$cpu_list" = "1" ] && return 0' in cpu_bind
    assert 'exec /usr/bin/taskset -c 1 "$0" "$@"' in cpu_bind
    assert text.index('bind_service_init_to_cpu1 "$@"') < text.index("LD_BIND_NOW=1")

    assert text.count("RUN_DIR=/run/8ax") == 1
    assert text.count(ASSIGNMENT) == 1
    assert text.count("RTAPI_FIFO_PATH=") == 3
    assert 'rm -f "$RTAPI_FIFO_PATH"' not in text
    assert ".rtapi_fifo" not in text
    assert "${V5_RTAPI_FIFO_PATH" not in text
    linuxcnc_commands = [
        line.strip() for line in text.splitlines() if "/usr/bin/linuxcnc " in line
    ]
    assert len(linuxcnc_commands) == 2
    assert all(ENV_PREFIX in line for line in linuxcnc_commands)

    for retired in (
        "backend_runtime_contract_ok",
        "ethercat_backend_ready",
        "wait_linuxcnc_backend_ready",
        "network_cpu_isolation_ok",
        "linuxcnc_realtime_scheduler_ok",
        "linuxcnc_non_realtime_affinity_ok",
        "linuxcnc_non_realtime_priority_ok",
        "linuxcnc_realtime_affinity_ok",
    ):
        assert retired not in text

    assert "BACKEND_READINESS_PROBE=/usr/libexec/8ax/v5_backend_readiness_probe" in text
    arm_wait = section(
        text, "arm_and_wait_backend_motion_ready() {", "\nset_command_gate_affinity() {"
    )
    assert '"$BACKEND_READINESS_PROBE" --arm --wait motion --timeout-ms 60000' in arm_wait
    assert "record_native_readiness_events" in arm_wait
    assert "ethercat" not in arm_wait
    assert "halcmd" not in arm_wait

    stop_backend = section(text, "stop_backend() {", "\nstart_backend() {")
    assert stop_backend.index("halcmd stop") < stop_backend.index(
        "quiesce_ethercat_slaves_before_release"
    )
    assert stop_backend.index("quiesce_ethercat_slaves_before_release") < stop_backend.index(
        "/usr/bin/linuxcnc -r -k"
    )
    assert stop_backend.index("/usr/bin/linuxcnc -r -k") < stop_backend.index(
        "halcmd unload all"
    )
    assert "refusing backend release without clean PREOP proof" in stop_backend
    assert 'rm -f "$ETHERCAT_ATTACH_FAULT_BASELINE"' in stop_backend

    start_backend = section(text, "start_backend() {", "\nstart_native_gate() {")
    assert "backend_readiness_require motion" in start_backend
    assert "wait_linuxcnc_process_set" in start_backend
    assert start_backend.index("wait_linuxcnc_process_set") < start_backend.index(
        "set_linuxcnc_realtime_affinity"
    )
    assert start_backend.index("set_linuxcnc_realtime_affinity") < start_backend.index(
        "set_linuxcnc_non_realtime_affinity"
    )
    assert start_backend.index("set_linuxcnc_non_realtime_affinity") < start_backend.index(
        "set_linuxcnc_non_realtime_priority"
    )
    assert "record_startup_event linuxcnc_spawned" in start_backend

    start_gate = section(text, "start_gate() {", "\nrollback_runtime_start() {")
    assert "v5_linuxcncrsh_probe" in start_gate
    assert "record_startup_event linuxcncrsh_probe_ready" in start_gate
    start_native = section(text, "start_native_gate() {", "\nstart_gate() {")
    assert "record_startup_event native_gate_socket_ready" in start_native

    transaction = section(text, "start_runtime_transaction() {", '\ncase "${1:-}" in')
    assert transaction.index("start_backend") < transaction.index("start_gate")
    assert transaction.index("start_gate") < transaction.index(
        "arm_and_wait_backend_motion_ready"
    )
    assert transaction.index("arm_and_wait_backend_motion_ready") < transaction.index(
        "start_native_gate"
    )
    assert "backend_readiness_require_transaction" in transaction
    identity_check = section(
        text,
        "backend_readiness_require_transaction() {",
        "\narm_and_wait_backend_motion_ready() {",
    )
    for token in (
        "generation",
        "owner_pid",
        "owner_start_ticks",
        "--expect-generation",
        "--expect-owner-pid",
        "--expect-owner-start-ticks",
    ):
        assert token in identity_check
    assert transaction.count("rollback_runtime_start") == 5
    assert text.count("    start_runtime_transaction") == 2

    restart_native = section(text, "  restart-native)", "\n  status)")
    assert "backend_readiness_require motion" in restart_native
    assert "set_linuxcnc_realtime_affinity" not in restart_native
    status = section(text, "  status)", "\n  requested-mode)")
    assert "backend_readiness_require motion" in status

    timing = section(text, "record_native_readiness_events() {", "\nbackend_readiness_require() {")
    for event in (
        "first_full_wkc",
        "dc_fresh_pair_ready",
        "cpu_contract_ready",
        "backend_ready_published",
    ):
        assert event in timing


def validate_lifecycle(text: str) -> None:
    for retired in (
        "ethercat_no_post_attach_faults",
        "ethercat_backend_ready",
        "ethercat_domain_wkc_ready",
        "ethercat_resident_all_op",
        "ethercat_reference_clock_healthy",
        "halcmd getp",
        "ethercat domains",
    ):
        assert retired not in text

    inactive = section(text, "ethercat_master_inactive() {", "\nethercat_backend_stopped() {")
    assert '[ -r /proc/modules ] || return 1' in inactive
    assert "grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules" in inactive

    fault_count = section(
        text, "ethercat_kernel_fault_count() {", "\ncapture_ethercat_attach_fault_baseline() {"
    )
    assert "kernel_log=$(dmesg 2>/dev/null) || return 1" in fault_count
    assert "datagrams UNMATCHED" in fault_count

    quiesce = section(
        text, "quiesce_ethercat_slaves_before_release() {", "\nethercat_master_inactive() {"
    )
    assert quiesce.index("ethercat states PREOP") < quiesce.index("ethercat states INIT")
    assert quiesce.count("wait_ethercat_slaves_clean_state PREOP") == 2


def validate_stop_failure_does_not_advance(text: str) -> None:
    start_backend = section(text, "start_backend() {", "\nstart_native_gate() {")
    harness = """set -eu
REQUESTED_DRIVER_MODE=bus
select_requested_backend() { REQUESTED_DRIVER_MODE=bus; }
runtime_prepare() { return 0; }
linuxcnc_privileged_helpers_ok() { return 0; }
backend_running() { return 0; }
backend_readiness_require() { return 1; }
stop_gate() { return 0; }
stop_backend() { return 1; }
backend_residue_running() { return 1; }
prepare_selected_transport() { echo V5_UNSAFE_CONTINUED; return 1; }
""" + start_backend + """
if ! start_backend; then exit 0; fi
exit 91
"""
    completed = subprocess.run(
        ["sh"], input=harness, text=True, capture_output=True, check=False
    )
    assert completed.returncode == 0, completed.stderr
    assert "V5_UNSAFE_CONTINUED" not in completed.stdout


def bind_listener(path: Path) -> socket.socket:
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(str(path))
    listener.listen(1)
    return listener


source = INIT.read_text(encoding="utf-8")
life_source = LIFECYCLE.read_text(encoding="utf-8")
validate_init(source)
validate_lifecycle(life_source)
validate_stop_failure_does_not_advance(source)

if hasattr(socket, "AF_UNIX"):
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        persistent = root / ".rtapi_fifo"
        canonical = root / "v5_rtapi_fifo"
        stale = bind_listener(persistent)
        stale.close()
        started = time.monotonic()
        current = bind_listener(canonical)
        assert time.monotonic() - started < 0.25
        assert persistent.exists()
        current.close()
        canonical.unlink()

        live = bind_listener(canonical)
        try:
            try:
                bind_listener(canonical)
            except OSError as exc:
                assert exc.errno == errno.EADDRINUSE
            else:
                raise AssertionError("live RTAPI master accepted a second bind")
        finally:
            live.close()

# Anti-resurrection: the old full scan and dmesg-count ready gate must remain
# physically absent after consumers move to the canonical owner.
for retired in (
    "backend_runtime_contract_ok() {",
    "wait_linuxcnc_backend_ready() {",
    "ethercat_backend_ready() {",
    "ethercat_no_post_attach_faults() {",
):
    assert retired not in source + life_source

print("V5_RTAPI_FIFO_LIFECYCLE_SMOKE_OK")
