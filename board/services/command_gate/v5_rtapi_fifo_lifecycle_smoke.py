#!/usr/bin/env python3
from __future__ import annotations

import errno
import os
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
    assert text.count("bind_service_init_to_cpu1") == 2
    assert text.index('bind_service_init_to_cpu1 "$@"') < text.index("LD_BIND_NOW=1")

    assert text.count("RUN_DIR=/run/8ax") == 1
    assert text.count(ASSIGNMENT) == 1
    assert text.count("RTAPI_FIFO_PATH=") == 3
    assert 'chown petalinux:petalinux "$RUN_DIR"' in text
    assert 'rm -f "$RTAPI_FIFO_PATH"' not in text
    assert ".rtapi_fifo" not in text
    assert "${V5_RTAPI_FIFO_PATH" not in text
    linuxcnc_commands = [line.strip() for line in text.splitlines()
                         if "/usr/bin/linuxcnc " in line]
    assert len(linuxcnc_commands) == 2
    assert all(ENV_PREFIX in line for line in linuxcnc_commands)

    runtime_contract = section(
        text, "backend_runtime_contract_ok() {", "\ncommand_gate_pids() {")
    for token in (
        "network_cpu_isolation_ok",
        "linuxcnc_realtime_affinity_ok",
        "linuxcnc_realtime_scheduler_ok",
        "linuxcnc_non_realtime_affinity_ok",
        "linuxcnc_non_realtime_priority_ok",
        "ethercat_backend_ready",
    ):
        assert token in runtime_contract

    stop_backend = section(text, "stop_backend() {", "\nstart_backend() {")
    assert stop_backend.index("halcmd stop") < stop_backend.index(
        "quiesce_ethercat_slaves_before_release")
    assert stop_backend.index("quiesce_ethercat_slaves_before_release") < stop_backend.index(
        "/usr/bin/linuxcnc -r -k")
    assert stop_backend.index("/usr/bin/linuxcnc -r -k") < stop_backend.index(
        "halcmd unload all")
    assert "refusing backend release without clean PREOP proof" in stop_backend
    assert 'elif backend_residue_running; then' in stop_backend
    assert 'elif [ "${REQUESTED_DRIVER_MODE:-}" = bus ]' not in stop_backend
    assert 'rm -f "$ETHERCAT_ATTACH_FAULT_BASELINE"' in stop_backend

    start_backend = section(text, "start_backend() {", "\nstart_native_gate() {")
    assert "if backend_runtime_contract_ok; then" in start_backend
    assert "wait_linuxcnc_backend_ready && backend_runtime_contract_ok" in start_backend
    assert "runtime_prepare || return 1" in start_backend
    assert start_backend.count("stop_gate || return 1") == 2
    assert start_backend.count("stop_backend || return 1") == 2
    assert "linuxcnc_launcher_pid=$!" in start_backend
    assert 'if ! printf \'%s\\n\' "$linuxcnc_launcher_pid" >"$LINUXCNC_PID"; then' in start_backend
    assert start_backend.index("if backend_running; then") < start_backend.index(
        "prepare_selected_transport || return 1")

    start_native = section(text, "start_native_gate() {", "\nstart_gate() {")
    assert "stop_native_gate || return 1" in start_native
    assert 'if ! printf \'%s\\n\' "$command_gate_pid" >"$COMMAND_GATE_PID"; then' in start_native

    rollback = section(text, "rollback_runtime_start() {", "\nstart_runtime_transaction() {")
    assert rollback.index("stop_gate") < rollback.index("stop_backend")
    for token in ("native_gate_running", "gate_running", "backend_residue_running"):
        assert token in rollback

    transaction = section(text, "start_runtime_transaction() {", "\ncase \"${1:-}\" in")
    assert transaction.index("start_backend") < transaction.index("start_gate")
    assert transaction.index("start_gate") < transaction.index("start_native_gate")
    assert transaction.count("rollback_runtime_start") == 4
    assert "backend_runtime_contract_ok" in transaction
    assert text.count("    start_runtime_transaction") == 2

    restart_native = section(text, "  restart-native)", "\n  status)")
    assert "backend_runtime_contract_ok" in restart_native
    assert "set_linuxcnc_realtime_affinity" not in restart_native
    assert "set_linuxcnc_non_realtime_affinity" not in restart_native
    assert "set_linuxcnc_non_realtime_priority" not in restart_native
    status = section(text, "  status)", "\n  requested-mode)")
    assert "backend_runtime_contract_ok && gate_running && native_gate_running" in status

    assert "ETHERCAT_ATTACH_FAULT_BASELINE=/run/v5_ethercat_attach_fault_baseline" in text
    prepare = section(text, "prepare_selected_transport() {", "\nif [ \"${1:-}\" = requested-mode ]")
    assert prepare.index("wait_ethercat_transport_scanned") < prepare.index(
        "capture_ethercat_attach_fault_baseline")


def validate_lifecycle(text: str) -> None:
    inactive = section(text, "ethercat_master_inactive() {", "\nethercat_domain_wkc_ready() {")
    assert '[ -r /proc/modules ] || return 1' in inactive
    assert "grep -Eq '^(ec_master|ec_generic)[[:space:]]' /proc/modules" in inactive
    assert inactive.index("/proc/modules") < inactive.index("ethercat master")

    all_op = section(text, "ethercat_resident_all_op() {", "\nethercat_reference_clock_healthy() {")
    assert "lcec.0.slaves-responding" in all_op
    assert "lcec.0.all-op" in all_op
    assert "ethercat_slaves_clean_state OP" in all_op

    fault_count = section(text, "ethercat_kernel_fault_count() {", "\ncapture_ethercat_attach_fault_baseline() {")
    assert "kernel_log=$(dmesg 2>/dev/null) || return 1" in fault_count
    for token in (
        "AL status message 0x001A",
        "Working counter changed to 0",
        "was SKIPPED",
        "datagrams TIMED OUT",
        "datagrams UNMATCHED",
    ):
        assert token in fault_count

    quiesce = section(text, "quiesce_ethercat_slaves_before_release() {", "\nethercat_master_active() {")
    assert quiesce.index("ethercat states PREOP") < quiesce.index("ethercat states INIT")
    assert quiesce.count("wait_ethercat_slaves_clean_state PREOP") == 2
    assert "wait_ethercat_slaves_clean_state INIT" in quiesce

    ready = section(text, "ethercat_backend_ready() {", "\nethercat_backend_stopped() {")
    for token in (
        "ethercat_master_active",
        "ethercat_resident_all_op",
        "ethercat_domain_wkc_ready",
        "ethercat_reference_clock_healthy",
        "ethercat_no_post_attach_faults",
    ):
        assert token in ready


def validate_stop_failure_does_not_advance(text: str) -> None:
    start_backend = section(text, "start_backend() {", "\nstart_native_gate() {")
    harness = """set -eu
REQUESTED_DRIVER_MODE=bus
select_requested_backend() { REQUESTED_DRIVER_MODE=bus; }
runtime_prepare() { return 0; }
linuxcnc_privileged_helpers_ok() { return 0; }
network_cpu_isolation_ok() { return 0; }
backend_running() { return 0; }
backend_runtime_contract_ok() { return 1; }
stop_gate() { return 0; }
stop_backend() { return 1; }
backend_residue_running() { return 1; }
prepare_selected_transport() { echo V5_UNSAFE_CONTINUED; return 1; }
""" + start_backend + """
if ! start_backend; then
  exit 0
fi
exit 91
"""
    completed = subprocess.run(
        ["sh"], input=harness, text=True, capture_output=True, check=False)
    assert completed.returncode == 0, completed.stderr
    assert "V5_UNSAFE_CONTINUED" not in completed.stdout


_fake_live: set[Path] = set()


class FakeListener:
    def __init__(self, path: Path) -> None:
        self.path = path

    def close(self) -> None:
        _fake_live.discard(self.path)


def bind_listener(path: Path):
    if hasattr(socket, "AF_UNIX"):
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(path))
        listener.listen(1)
        return listener
    if path in _fake_live:
        raise OSError(errno.EADDRINUSE, "address in use")
    path.touch(exist_ok=True)
    _fake_live.add(path)
    return FakeListener(path)


source = INIT.read_text(encoding="utf-8")
life_source = LIFECYCLE.read_text(encoding="utf-8")
validate_init(source)
validate_lifecycle(life_source)
validate_stop_failure_does_not_advance(source)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    persistent = root / ".rtapi_fifo"
    canonical = root / "v5_rtapi_fifo"

    # A closed persistent socket is deliberately left stale. The product's
    # explicit volatile path binds immediately and never reads or removes it.
    stale = bind_listener(persistent)
    stale.close()
    started = time.monotonic()
    current = bind_listener(canonical)
    assert time.monotonic() - started < 0.25
    assert persistent.exists()
    current.close()
    canonical.unlink()

    # A live owner on the canonical path remains authoritative: a second bind
    # fails EADDRINUSE, connection succeeds, and the path is not unlinked.
    live = bind_listener(canonical)
    try:
        try:
            contender = bind_listener(canonical)
        except OSError as exc:
            assert exc.errno == errno.EADDRINUSE
        else:
            contender.close()
            raise AssertionError("live RTAPI master accepted a second bind")
        if hasattr(socket, "AF_UNIX"):
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect(str(canonical))
            accepted, _ = live.accept()
            accepted.close()
            client.close()
        else:
            assert canonical in _fake_live
        assert canonical.exists()
    finally:
        live.close()

# Anti-resurrection: dropping either lifecycle environment, restoring HOME,
# adding an override/fallback, or changing the unique path must fail.
mutations = (
    source.replace(ENV_PREFIX, "TCLLIBPATH=/usr/lib/tcltk /usr/bin/linuxcnc", 1),
    source.replace(ASSIGNMENT, "RTAPI_FIFO_PATH=${V5_RTAPI_FIFO_PATH:-$HOME/.rtapi_fifo}"),
    source + "\n# $HOME/.rtapi_fifo\n",
    source.replace(ASSIGNMENT, "RTAPI_FIFO_PATH=$RUN_DIR/another_fifo"),
    source.replace(ASSIGNMENT, ASSIGNMENT + '\nrm -f "$RTAPI_FIFO_PATH"'),
    source.replace('exec /usr/bin/taskset -c 1 "$0" "$@"', "return 0", 1),
    source.replace("if backend_runtime_contract_ok; then", "if ethercat_backend_ready; then", 1),
    source.replace("    start_runtime_transaction", "    start_backend", 1),
    source.replace("quiesce_ethercat_slaves_before_release", "true", 1),
    source.replace("stop_backend || return 1", "stop_backend", 1),
)
for mutation in mutations:
    try:
        validate_init(mutation)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("retired RTAPI FIFO path/lifecycle was reintroduced")

life_mutations = (
    life_source.replace("lcec.0.all-op", "lcec.0.slaves-responding", 1),
    life_source.replace("ethercat_slaves_clean_state OP", "true", 1),
    life_source.replace("kernel_log=$(dmesg 2>/dev/null) || return 1", "kernel_log=$(dmesg 2>/dev/null || true)", 1),
    life_source.replace("ethercat_no_post_attach_faults\n}", "true\n}", 1),
    life_source.replace("ethercat states INIT", "true", 1),
)
for mutation in life_mutations:
    try:
        validate_lifecycle(mutation)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("EtherCAT startup/rollback contract was weakened")

print("V5_RTAPI_FIFO_LIFECYCLE_SMOKE_OK")
