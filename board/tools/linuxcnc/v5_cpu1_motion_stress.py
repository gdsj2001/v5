#!/usr/bin/env python3
"""CPU1-only motion stress/UI freeze with buffered 100 ms runtime evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
import signal
import subprocess
import sys
import time
import re
from pathlib import Path
from typing import Any


CPU1 = 1
MIN_DURATION_SECONDS = 30
MAX_DURATION_SECONDS = 60
MAX_OBSERVE_DURATION_SECONDS = 600
DEFAULT_STRESS_SECONDS = 60
DEFAULT_FREEZE_SECONDS = 30
SAMPLE_INTERVAL_NS = 100_000_000
EXPECTED_SERVO_PERIOD_NS = 1_000_000
TARGET_SERVO_TMAX_NS = 700_000
TARGET_RUNTIME_PHASE_STEP_NS = 100_000
TARGET_RUNTIME_PERIOD_ERROR_NS = 100_000
HARD_RUNTIME_PHASE_STEP_NS = 200_000
HARD_RUNTIME_PERIOD_ERROR_NS = 200_000
TARGET_COMBINED_BUDGET_NS = 800_000
HARD_COMBINED_BUDGET_NS = EXPECTED_SERVO_PERIOD_NS
MIN_RUNTIME_PERIOD_NS = EXPECTED_SERVO_PERIOD_NS - HARD_RUNTIME_PERIOD_ERROR_NS
MAX_RUNTIME_PERIOD_NS = EXPECTED_SERVO_PERIOD_NS + HARD_RUNTIME_PERIOD_ERROR_NS
MIN_MOTION_VELOCITY = 1e-6
MIN_ENCODER_PROGRESS = 0.00005
UINT32_MAX = (1 << 32) - 1
EXPECTED_SLAVES = 5
EXPECTED_WKC = 10
PROC_ROOT = Path("/proc")
EVIDENCE_ROOT = Path("/tmp/v5_test_tools")
BACKEND_PROBE = Path("/usr/libexec/8ax/v5_backend_readiness_probe")
LINUXCNC_LOG = Path("/run/8ax/v5_linuxcnc.log")
REALTIME_DELAY_TOKEN = b"Unexpected realtime delay"
KERNEL_LOG_COMMAND = ("dmesg", "--color=never")
KERNEL_FAULT_PATTERNS = {
    "al_sync_error": re.compile(r'AL status message 0x001A: "Synchronization error"'),
    "working_counter_zero": re.compile(r"Working counter changed to 0/"),
    "datagram_skipped": re.compile(r"Datagram .* was SKIPPED"),
    "datagram_timed_out": re.compile(r"datagrams? TIMED OUT"),
    "datagram_unmatched": re.compile(r"datagrams? UNMATCHED"),
    "reference_clock_error": re.compile(r"Failed to get reference clock time"),
}
RUNTIME_LOG_TOKENS = {
    "unexpected_realtime_delay": b"Unexpected realtime delay",
    "reference_clock_error": b"Failed to get reference clock time",
    "datagram_skipped": b"SKIPPED",
    "datagram_timed_out": b"TIMED OUT",
    "datagram_unmatched": b"UNMATCHED",
}
WINDOW_ACTIVE = False


class StressError(RuntimeError):
    pass


def parse_cpu_list(value: str) -> set[int]:
    cpus: set[int] = set()
    for raw_part in value.strip().split(","):
        part = raw_part.strip()
        if not part:
            continue
        if "-" in part:
            first_text, last_text = part.split("-", 1)
            first = int(first_text, 10)
            last = int(last_text, 10)
            if first < 0 or last < first:
                raise ValueError(f"invalid CPU range: {part}")
            cpus.update(range(first, last + 1))
        else:
            cpu = int(part, 10)
            if cpu < 0:
                raise ValueError(f"invalid CPU: {part}")
            cpus.add(cpu)
    if not cpus:
        raise ValueError("CPU list is empty")
    return cpus


def read_status_field(path: Path, field: str) -> str:
    prefix = field + ":"
    for line in path.read_text(encoding="ascii", errors="strict").splitlines():
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    raise StressError(f"missing {field} in {path}")


def bind_and_prove_cpu1(proc_root: Path = PROC_ROOT) -> tuple[set[int], set[int]]:
    if not hasattr(os, "sched_setaffinity") or not hasattr(os, "sched_getaffinity"):
        raise StressError("Linux sched affinity APIs are unavailable")
    available = set(os.sched_getaffinity(0))
    if CPU1 not in available:
        raise StressError(f"CPU1 unavailable in initial affinity: {sorted(available)}")
    os.sched_setaffinity(0, {CPU1})
    api_readback = set(os.sched_getaffinity(0))
    status_readback = parse_cpu_list(
        read_status_field(proc_root / "self" / "status", "Cpus_allowed_list")
    )
    if api_readback != {CPU1} or status_readback != {CPU1}:
        raise StressError(
            "CPU1 affinity readback mismatch: "
            f"api={sorted(api_readback)} proc={sorted(status_readback)}"
        )
    if 0 in api_readback or 0 in status_readback:
        raise StressError("CPU0 appeared in allowed mask")
    return api_readback, status_readback


def validate_duration(seconds: int, maximum: int = MAX_DURATION_SECONDS) -> int:
    if seconds < MIN_DURATION_SECONDS or seconds > maximum:
        raise StressError(
            f"duration must be {MIN_DURATION_SECONDS}..{maximum} seconds"
        )
    return seconds


def mode_duration(mode: str, requested: int | None) -> int:
    if requested is None:
        requested = DEFAULT_FREEZE_SECONDS if mode == "freeze-ui" else DEFAULT_STRESS_SECONDS
    maximum = MAX_OBSERVE_DURATION_SECONDS if mode == "observe" else MAX_DURATION_SECONDS
    return validate_duration(requested, maximum)


def process_start_ticks(stat_text: str) -> int:
    close = stat_text.rfind(")")
    if close < 0:
        raise StressError("malformed /proc PID stat")
    fields_after_comm = stat_text[close + 2:].split()
    if len(fields_after_comm) < 20:
        raise StressError("short /proc PID stat")
    return int(fields_after_comm[19], 10)


def process_identity(pid: int, proc_root: Path = PROC_ROOT) -> tuple[str, int]:
    pid_root = proc_root / str(pid)
    cmdline_parts = (pid_root / "cmdline").read_bytes().split(b"\0")
    if not cmdline_parts or not cmdline_parts[0]:
        raise StressError(f"PID {pid} has no executable cmdline")
    executable = cmdline_parts[0].decode("utf-8", errors="strict")
    start_ticks = process_start_ticks((pid_root / "stat").read_text(encoding="ascii"))
    return executable, start_ticks


def process_state(pid: int, proc_root: Path = PROC_ROOT) -> str:
    return read_status_field(proc_root / str(pid) / "status", "State").split()[0]


def wait_for_state(pid: int, stopped: bool, deadline: float) -> bool:
    while time.monotonic() < deadline:
        try:
            state = process_state(pid)
        except FileNotFoundError:
            return False
        if (state in {"T", "t"}) == stopped:
            return True
        time.sleep(0.01)
    return False


def install_stop_handlers(stop: dict[str, bool]) -> dict[int, object]:
    previous: dict[int, object] = {}

    def request_stop(_signum: int, _frame: object) -> None:
        stop["requested"] = True

    for signum in (signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, request_stop)
    return previous


def restore_handlers(previous: dict[int, object]) -> None:
    for signum, handler in previous.items():
        signal.signal(signum, handler)


def parse_backend_response(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in text.strip().split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    for required in (
        "generation", "owner_pid", "owner_start_ticks", "motion_ready", "revoked",
        "slaves", "wkc", "wkc_complete", "all_op", "dc_phased", "dc_valid",
        "dc_age", "dc_seq", "dc_errors",
    ):
        if required not in fields:
            raise StressError(f"backend response missing {required}: {text.strip()}")
    return fields


def backend_identity(fields: dict[str, str]) -> tuple[int, int, int]:
    return (
        int(fields["generation"], 10),
        int(fields["owner_pid"], 10),
        int(fields["owner_start_ticks"], 10),
    )


def query_backend_identity(
    probe: Path = BACKEND_PROBE,
    expected: tuple[int, int, int] | None = None,
) -> dict[str, str]:
    if WINDOW_ACTIVE:
        raise StressError("external command attempted inside measurement window")
    command = [str(probe), "--status", "--require", "motion"]
    if expected is not None:
        command.extend((
            "--expect-generation", str(expected[0]),
            "--expect-owner-pid", str(expected[1]),
            "--expect-owner-start-ticks", str(expected[2]),
        ))
    result = subprocess.run(
        command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=2.0,
    )
    if result.returncode != 0:
        raise StressError(
            f"backend identity probe failed rc={result.returncode}: "
            f"{result.stdout.strip()} {result.stderr.strip()}"
        )
    fields = parse_backend_response(result.stdout)
    if fields["motion_ready"] != "1" or fields["revoked"] != "0":
        raise StressError(f"backend is not motion-ready: {result.stdout.strip()}")
    if expected is not None and backend_identity(fields) != expected:
        raise StressError("backend identity changed across measurement window")
    return fields


def parse_cpu_ticks(text: str) -> dict[str, list[int]]:
    result: dict[str, list[int]] = {}
    for line in text.splitlines():
        parts = line.split()
        if parts and parts[0] in {"cpu0", "cpu1"}:
            result[parts[0]] = [int(value, 10) for value in parts[1:]]
    if set(result) != {"cpu0", "cpu1"}:
        raise StressError("/proc/stat does not expose both cpu0 and cpu1")
    return result


def read_cpu_ticks(proc_root: Path = PROC_ROOT) -> dict[str, list[int]]:
    return parse_cpu_ticks((proc_root / "stat").read_text(encoding="ascii"))


def count_realtime_delays(path: Path = LINUXCNC_LOG) -> int:
    if WINDOW_ACTIVE:
        raise StressError("realtime log read attempted inside measurement window")
    try:
        return path.read_bytes().count(REALTIME_DELAY_TOKEN)
    except OSError as exc:
        raise StressError(f"LinuxCNC realtime log read failed: {path}: {exc}") from exc


def count_kernel_faults(text: str) -> dict[str, int]:
    return {
        name: len(pattern.findall(text))
        for name, pattern in KERNEL_FAULT_PATTERNS.items()
    }


def capture_kernel_fault_counters() -> dict[str, int]:
    if WINDOW_ACTIVE:
        raise StressError("kernel log command attempted inside measurement window")
    result = subprocess.run(
        KERNEL_LOG_COMMAND, check=False, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, timeout=2.0,
    )
    if result.returncode != 0:
        raise StressError(
            f"kernel log read failed rc={result.returncode}: {result.stderr.strip()}"
        )
    return count_kernel_faults(result.stdout)


def count_runtime_log_markers(path: Path = LINUXCNC_LOG) -> dict[str, int]:
    if WINDOW_ACTIVE:
        raise StressError("runtime log read attempted inside measurement window")
    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise StressError(f"LinuxCNC runtime log read failed: {path}: {exc}") from exc
    return {name: payload.count(token) for name, token in RUNTIME_LOG_TOKENS.items()}


def load_runtime_modules() -> tuple[Any, Any]:
    try:
        hal_module = importlib.import_module("hal")
        linuxcnc_module = importlib.import_module("linuxcnc")
    except ImportError as exc:
        raise StressError(f"LinuxCNC Python runtime unavailable: {exc}") from exc
    if (
        not callable(getattr(hal_module, "get_value", None)) or
        not callable(getattr(hal_module, "component", None)) or
        not callable(getattr(hal_module, "set_p", None)) or
        not callable(getattr(hal_module, "get_info_params", None))
    ):
        raise StressError("LinuxCNC HAL Python API is unavailable")
    if not callable(getattr(linuxcnc_module, "stat", None)):
        raise StressError("LinuxCNC stat API is unavailable")
    return hal_module, linuxcnc_module


class MetricsSampler:
    PIN_NAMES = (
        "servo-thread.time", "servo-thread.tmax", "motion.servo.last-period",
        "motion.requested-vel", "motion.current-vel",
        "lcec.0.slaves-responding", "lcec.0.all-op",
        "lcec.0.domain-working-counter", "lcec.0.domain-wc-complete",
        "lcec.0.dc-phased", "lcec.0.dc-time-valid", "lcec.0.dc-time-ok-seq",
        "lcec.0.dc-time-age-cycles", "lcec.0.dc-time-error-count",
        "lcec.0.startup-phase-span", "lcec.0.runtime-phase-step",
        "lcec.0.runtime-phase-step-max",
        "lcec.0.runtime-period-ns", "lcec.0.runtime-period-error-max",
        "lcec.0.runtime-period-warning-count",
        "lcec.0.runtime-period-fault-count",
        "lcec.0.runtime-period-event-valid",
        "lcec.0.runtime-period-event-sequence",
        "lcec.0.runtime-period-event-period-ns",
        "lcec.0.runtime-period-event-error-ns",
        "lcec.0.runtime-period-event-phase-step-ns",
        "lcec.0.runtime-period-event-working-counter",
        "lcec.0.runtime-period-event-slaves-responding",
        "lcec.0.runtime-period-event-wc-complete",
        "lcec.0.runtime-period-event-all-op",
        "lcec.0.runtime-period-event-dc-phased",
        "lcec.0.runtime-period-event-dc-valid",
        "lcec.0.runtime-period-event-dc-age-cycles",
        "lcec.0.runtime-period-event-app-time-hi",
        "lcec.0.runtime-period-event-app-time-lo",
        "lcec.0.runtime-period-event-write-prelock-ns",
        "lcec.0.runtime-period-event-write-lock-wait-ns",
        "lcec.0.runtime-period-event-write-prepare-ns",
        "lcec.0.runtime-period-event-write-send-ns",
        "lcec.0.runtime-period-event-write-total-ns",
        "lcec.0.domain-wc-incomplete-count", "lcec.0.all-op-false-count",
    )
    EVENT_TMAX_NAMES = (
        "servo-thread.tmax",
        "lcec.read-all.tmax",
        "lcec.write-all.tmax",
    )

    def __init__(
        self,
        hal_module: Any,
        linuxcnc_module: Any,
        initial_dc_seq: int,
        initial_dc_errors: int,
        proc_root: Path = PROC_ROOT,
        require_motion: bool = False,
    ) -> None:
        self.hal = hal_module
        self.hal_component = hal_module.component(
            f"v5_cpu1_motion_stress_{os.getpid()}"
        )
        self.hal_component.ready()
        self.status = linuxcnc_module.stat()
        self.proc_root = proc_root
        self.require_motion = require_motion
        self.last_dc_seq = initial_dc_seq
        self.initial_dc_errors = initial_dc_errors
        self.initial_period_warnings = 0
        self.initial_period_faults = 0
        self.initial_wc_incomplete = 0
        self.initial_all_op_false = 0
        self.first_runtime_period_event: dict[str, Any] | None = None
        self.previous_encoder: list[float] | None = None
        self.tmax_names = sorted({
            str(item.get("NAME", ""))
            for item in hal_module.get_info_params()
            if str(item.get("NAME", "")).endswith(".tmax")
        })
        if "servo-thread.tmax" not in self.tmax_names:
            raise StressError("servo-thread.tmax is absent from HAL parameter inventory")
        missing_event_tmax = set(self.EVENT_TMAX_NAMES).difference(self.tmax_names)
        if missing_event_tmax:
            raise StressError(
                "hard-event tmax parameters are absent from HAL inventory: "
                + ",".join(sorted(missing_event_tmax))
            )
        for name in self.PIN_NAMES:
            self._get(name)
        for joint in range(EXPECTED_SLAVES):
            self._get(f"joint.{joint}.motor-pos-fb")
            self._get(f"joint.{joint}.f-error")
            self._get(f"joint.{joint}.f-error-lim")
            self._get(f"lcec.0.s{joint}.statusword")
        self.status.poll()
        if not hasattr(self.status, "queue"):
            raise StressError("LinuxCNC status.queue is unavailable")

    def arm_window(self) -> dict[str, Any]:
        previous_tmax = {
            name: int(self._get(name)) for name in self.tmax_names
        }
        try:
            for name in self.tmax_names:
                self.hal.set_p(name, "0")
            self.hal.set_p("lcec.0.runtime-phase-step-max", "0")
            self.hal.set_p("lcec.0.runtime-period-error-max", "0")
            self.hal.set_p("lcec.0.runtime-period-event-clear", "1")
        except Exception as exc:
            raise StressError(f"HAL tmax/sticky reset failed: {exc}") from exc
        event_clear_deadline = time.monotonic() + 0.25
        while bool(self._get("lcec.0.runtime-period-event-clear")):
            if time.monotonic() >= event_clear_deadline:
                raise StressError("lcec hard-event snapshot clear timed out")
            time.sleep(0.001)
        if bool(self._get("lcec.0.runtime-period-event-valid")):
            raise StressError("lcec hard-event snapshot remained valid after clear")
        self.first_runtime_period_event = None
        self.initial_period_warnings = int(
            self._get("lcec.0.runtime-period-warning-count")
        )
        self.initial_period_faults = int(
            self._get("lcec.0.runtime-period-fault-count")
        )
        self.initial_wc_incomplete = int(
            self._get("lcec.0.domain-wc-incomplete-count")
        )
        self.initial_all_op_false = int(self._get("lcec.0.all-op-false-count"))
        if (
            self.initial_period_warnings == UINT32_MAX
            or self.initial_period_faults == UINT32_MAX
            or self.initial_wc_incomplete == UINT32_MAX
            or self.initial_all_op_false == UINT32_MAX
        ):
            raise StressError(
                "HAL sticky counter saturated before measurement window"
            )
        self.status.poll()
        self.previous_encoder = [
            float(self._get(f"joint.{joint}.motor-pos-fb"))
            for joint in range(EXPECTED_SLAVES)
        ]
        if self.require_motion:
            requested = float(self._get("motion.requested-vel"))
            current = float(self._get("motion.current-vel"))
            if (
                int(self.status.queue) <= 0
                or not math.isfinite(requested)
                or not math.isfinite(current)
                or abs(requested) <= MIN_MOTION_VELOCITY
                or abs(current) <= MIN_MOTION_VELOCITY
            ):
                raise StressError(
                    "motion window is not active before measurement: "
                    f"queue={self.status.queue} requested={requested} current={current}"
                )
        return {
            "runtime_period_warning_count": self.initial_period_warnings,
            "runtime_period_fault_count": self.initial_period_faults,
            "domain_wc_incomplete_count": self.initial_wc_incomplete,
            "all_op_false_count": self.initial_all_op_false,
            "runtime_phase_step_max_ns": int(
                self._get("lcec.0.runtime-phase-step-max")
            ),
            "runtime_period_error_max_ns": int(
                self._get("lcec.0.runtime-period-error-max")
            ),
            "motion_required": self.require_motion,
            "initial_encoder": self.previous_encoder,
            "tmax_reset_count": len(self.tmax_names),
            "previous_tmax_ns": previous_tmax,
        }

    def finish_window(self) -> dict[str, Any]:
        return {
            "runtime_period_warning_count": int(
                self._get("lcec.0.runtime-period-warning-count")
            ),
            "runtime_period_fault_count": int(
                self._get("lcec.0.runtime-period-fault-count")
            ),
            "domain_wc_incomplete_count": int(
                self._get("lcec.0.domain-wc-incomplete-count")
            ),
            "all_op_false_count": int(self._get("lcec.0.all-op-false-count")),
            "runtime_phase_step_max_ns": int(
                self._get("lcec.0.runtime-phase-step-max")
            ),
            "runtime_period_error_max_ns": int(
                self._get("lcec.0.runtime-period-error-max")
            ),
            "first_runtime_period_event": self.first_runtime_period_event,
            "tmax_ns": {name: int(self._get(name)) for name in self.tmax_names},
        }

    def _get(self, name: str) -> Any:
        try:
            return self.hal.get_value(name)
        except Exception as exc:
            raise StressError(f"HAL read failed for {name}: {exc}") from exc

    @staticmethod
    def _runtime_period_event(values: dict[str, Any]) -> dict[str, Any] | None:
        if not bool(values["lcec.0.runtime-period-event-valid"]):
            return None
        app_time = (
            (int(values["lcec.0.runtime-period-event-app-time-hi"]) << 32)
            | int(values["lcec.0.runtime-period-event-app-time-lo"])
        )
        return {
            "sequence": int(values["lcec.0.runtime-period-event-sequence"]),
            "period_ns": int(values["lcec.0.runtime-period-event-period-ns"]),
            "period_error_ns": int(
                values["lcec.0.runtime-period-event-error-ns"]
            ),
            "phase_step_ns": int(
                values["lcec.0.runtime-period-event-phase-step-ns"]
            ),
            "working_counter": int(
                values["lcec.0.runtime-period-event-working-counter"]
            ),
            "slaves_responding": int(
                values["lcec.0.runtime-period-event-slaves-responding"]
            ),
            "wc_complete": bool(
                values["lcec.0.runtime-period-event-wc-complete"]
            ),
            "all_op": bool(values["lcec.0.runtime-period-event-all-op"]),
            "dc_phased": bool(
                values["lcec.0.runtime-period-event-dc-phased"]
            ),
            "dc_valid": bool(
                values["lcec.0.runtime-period-event-dc-valid"]
            ),
            "dc_age_cycles": int(
                values["lcec.0.runtime-period-event-dc-age-cycles"]
            ),
            "write_segments_ns": {
                "prelock": int(
                    values[
                        "lcec.0.runtime-period-event-write-prelock-ns"
                    ]
                ),
                "lock_wait": int(
                    values[
                        "lcec.0.runtime-period-event-write-lock-wait-ns"
                    ]
                ),
                "prepare": int(
                    values[
                        "lcec.0.runtime-period-event-write-prepare-ns"
                    ]
                ),
                "send": int(
                    values["lcec.0.runtime-period-event-write-send-ns"]
                ),
                "total": int(
                    values["lcec.0.runtime-period-event-write-total-ns"]
                ),
            },
            "app_time_ns": app_time,
        }

    def sample(self, index: int, deadline_ns: int) -> dict[str, Any]:
        started_ns = time.monotonic_ns()
        self.status.poll()
        values = {name: self._get(name) for name in self.PIN_NAMES}
        runtime_period_event = self._runtime_period_event(values)
        if (
            runtime_period_event is not None
            and self.first_runtime_period_event is None
        ):
            self.first_runtime_period_event = {
                **runtime_period_event,
                "tmax_ns": {
                    name: int(self._get(name))
                    for name in self.EVENT_TMAX_NAMES
                },
            }
        encoders = [self._get(f"joint.{j}.motor-pos-fb") for j in range(EXPECTED_SLAVES)]
        following = [self._get(f"joint.{j}.f-error") for j in range(EXPECTED_SLAVES)]
        following_limits = [
            self._get(f"joint.{j}.f-error-lim") for j in range(EXPECTED_SLAVES)
        ]
        statuswords = [int(self._get(f"lcec.0.s{j}.statusword")) for j in range(EXPECTED_SLAVES)]
        finished_ns = time.monotonic_ns()
        dc_seq = int(values["lcec.0.dc-time-ok-seq"])
        dc_errors = int(values["lcec.0.dc-time-error-count"])
        servo_time = int(values["servo-thread.time"])
        servo_tmax = int(values["servo-thread.tmax"])
        last_period = int(values["motion.servo.last-period"])
        startup_phase_span = int(values["lcec.0.startup-phase-span"])
        runtime_phase_step = int(values["lcec.0.runtime-phase-step"])
        runtime_phase_step_max = int(values["lcec.0.runtime-phase-step-max"])
        runtime_period = int(values["lcec.0.runtime-period-ns"])
        runtime_period_error_max = int(values["lcec.0.runtime-period-error-max"])
        runtime_period_warnings = int(values["lcec.0.runtime-period-warning-count"])
        runtime_period_faults = int(values["lcec.0.runtime-period-fault-count"])
        wc_incomplete = int(values["lcec.0.domain-wc-incomplete-count"])
        all_op_false = int(values["lcec.0.all-op-false-count"])
        encoder_values = [float(value) for value in encoders]
        following_values = [float(value) for value in following]
        following_limit_values = [float(value) for value in following_limits]
        encoder_delta = (
            [abs(current - previous) for current, previous in zip(
                encoder_values, self.previous_encoder
            )]
            if self.previous_encoder is not None else [0.0] * EXPECTED_SLAVES
        )
        operation_enabled = [
            (statusword & 0x006F) == 0x0027 for statusword in statuswords
        ]
        violations: list[str] = []
        warnings: list[str] = []
        if started_ns - deadline_ns >= SAMPLE_INTERVAL_NS:
            violations.append("sample_deadline_missed")
        if int(values["lcec.0.slaves-responding"]) != EXPECTED_SLAVES:
            violations.append("slave_count")
        if not bool(values["lcec.0.all-op"]):
            violations.append("all_op")
        if int(values["lcec.0.domain-working-counter"]) != EXPECTED_WKC:
            violations.append("wkc")
        if not bool(values["lcec.0.domain-wc-complete"]):
            violations.append("wkc_complete")
        if not bool(values["lcec.0.dc-phased"]):
            violations.append("dc_phased")
        if not bool(values["lcec.0.dc-time-valid"]):
            violations.append("dc_valid")
        if int(values["lcec.0.dc-time-age-cycles"]) != 0:
            violations.append("dc_age")
        if dc_seq <= self.last_dc_seq:
            violations.append("dc_seq_not_advanced")
        if dc_errors != self.initial_dc_errors:
            violations.append("dc_errors_changed")
        if runtime_period_warnings != self.initial_period_warnings:
            warnings.append("runtime_period_warning_sticky")
        if runtime_period_faults != self.initial_period_faults:
            violations.append("runtime_period_fault_sticky")
            if runtime_period_event is None:
                violations.append("runtime_period_event_missing")
        if wc_incomplete != self.initial_wc_incomplete:
            violations.append("wkc_incomplete_sticky")
        if all_op_false != self.initial_all_op_false:
            violations.append("all_op_false_sticky")
        if runtime_phase_step_max > TARGET_RUNTIME_PHASE_STEP_NS:
            warnings.append("runtime_phase_step_target")
        if runtime_period_error_max > TARGET_RUNTIME_PERIOD_ERROR_NS:
            warnings.append("runtime_period_error_target")
        if runtime_phase_step_max > HARD_RUNTIME_PHASE_STEP_NS:
            violations.append("runtime_phase_step_max")
        if runtime_period_error_max > HARD_RUNTIME_PERIOD_ERROR_NS:
            violations.append("runtime_period_error_max")
        if servo_tmax > TARGET_SERVO_TMAX_NS:
            warnings.append("servo_tmax_target")
        if servo_time >= EXPECTED_SERVO_PERIOD_NS or servo_tmax >= EXPECTED_SERVO_PERIOD_NS:
            violations.append("servo_execution_overrun")
        combined_budget = servo_tmax + max(
            runtime_phase_step_max, runtime_period_error_max
        )
        if combined_budget > TARGET_COMBINED_BUDGET_NS:
            warnings.append("combined_budget_target")
        if combined_budget >= HARD_COMBINED_BUDGET_NS:
            violations.append("combined_budget_hard")
        if not MIN_RUNTIME_PERIOD_NS <= last_period <= MAX_RUNTIME_PERIOD_NS:
            violations.append("servo_period_out_of_budget")
        if not MIN_RUNTIME_PERIOD_NS <= runtime_period <= MAX_RUNTIME_PERIOD_NS:
            violations.append("runtime_period_out_of_budget")
        if self.require_motion:
            requested = float(values["motion.requested-vel"])
            current = float(values["motion.current-vel"])
            if int(self.status.queue) <= 0:
                violations.append("motion_queue_empty")
            if not math.isfinite(requested) or abs(requested) <= MIN_MOTION_VELOCITY:
                violations.append("requested_velocity_zero")
            if not math.isfinite(current) or abs(current) <= MIN_MOTION_VELOCITY:
                violations.append("current_velocity_zero")
            if not all(math.isfinite(value) for value in encoder_values):
                violations.append("encoder_non_finite")
            elif max(encoder_delta, default=0.0) < MIN_ENCODER_PROGRESS:
                violations.append("encoder_not_progressing")
            if not all(operation_enabled):
                violations.append("cia402_not_operation_enabled")
            for joint, (error, limit) in enumerate(zip(
                following_values, following_limit_values
            )):
                if (
                    not math.isfinite(error)
                    or not math.isfinite(limit)
                    or limit <= 0.0
                    or abs(error) > limit
                ):
                    violations.append(f"following_error_limit_j{joint}")
        self.previous_encoder = encoder_values
        self.last_dc_seq = dc_seq
        return {
            "type": "sample", "index": index, "deadline_ns": deadline_ns,
            "monotonic_ns": started_ns, "sample_duration_ns": finished_ns - started_ns,
            "lateness_ns": max(0, started_ns - deadline_ns),
            "cpu_ticks": read_cpu_ticks(self.proc_root),
            "configured_servo_period_ns": EXPECTED_SERVO_PERIOD_NS,
            "servo_time_ns": servo_time, "servo_tmax_ns": servo_tmax,
            "combined_budget_ns": combined_budget,
            "motion_last_period_ns": last_period,
            "overrun": "servo_execution_overrun" in violations,
            "trajectory_queue": int(self.status.queue),
            "requested_vel": float(values["motion.requested-vel"]),
            "current_vel": float(values["motion.current-vel"]),
            "slaves_responding": int(values["lcec.0.slaves-responding"]),
            "all_op": bool(values["lcec.0.all-op"]),
            "wkc": int(values["lcec.0.domain-working-counter"]),
            "wkc_complete": bool(values["lcec.0.domain-wc-complete"]),
            "dc_phased": bool(values["lcec.0.dc-phased"]),
            "dc_valid": bool(values["lcec.0.dc-time-valid"]),
            "dc_age_cycles": int(values["lcec.0.dc-time-age-cycles"]),
            "dc_seq": dc_seq, "dc_errors": dc_errors,
            "startup_phase_span_ns": startup_phase_span,
            "runtime_phase_step_ns": runtime_phase_step,
            "runtime_phase_step_max_ns": runtime_phase_step_max,
            "runtime_period_ns": runtime_period,
            "runtime_period_error_max_ns": runtime_period_error_max,
            "runtime_period_warning_count": runtime_period_warnings,
            "runtime_period_fault_count": runtime_period_faults,
            "runtime_period_event": runtime_period_event,
            "first_runtime_period_event": self.first_runtime_period_event,
            "domain_wc_incomplete_count": wc_incomplete,
            "all_op_false_count": all_op_false, "encoder": encoder_values,
            "encoder_delta": encoder_delta,
            "following_error": following_values,
            "following_error_limit": following_limit_values,
            "statusword": statuswords,
            "cia402_operation_enabled": operation_enabled,
            "motion_required": self.require_motion,
            "warnings": warnings,
            "violations": violations,
        }


def run_stress_window(
    duration: int, sampler: MetricsSampler, stop: dict[str, bool]
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    samples: list[dict[str, Any]] = []
    deadline_ns = time.monotonic_ns()
    sample_count = duration * (1_000_000_000 // SAMPLE_INTERVAL_NS)
    iterations = 0
    value = 0x12345678
    for index in range(1, sample_count + 1):
        deadline_ns += SAMPLE_INTERVAL_NS
        while time.monotonic_ns() < deadline_ns and not stop["requested"]:
            for _ in range(1000):
                value = (value * 1664525 + 1013904223) & 0xFFFFFFFF
            iterations += 1000
        if stop["requested"]:
            raise StressError("CPU1 stress interrupted")
        samples.append(sampler.sample(index, deadline_ns))
    return samples, {"iterations": iterations, "checksum": value}


def run_observe_window(
    duration: int, sampler: MetricsSampler, stop: dict[str, bool]
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    samples: list[dict[str, Any]] = []
    deadline_ns = time.monotonic_ns()
    sample_count = duration * (1_000_000_000 // SAMPLE_INTERVAL_NS)
    for index in range(1, sample_count + 1):
        deadline_ns += SAMPLE_INTERVAL_NS
        while time.monotonic_ns() < deadline_ns and not stop["requested"]:
            remaining = deadline_ns - time.monotonic_ns()
            time.sleep(min(0.01, max(0.0, remaining / 1_000_000_000)))
        if stop["requested"]:
            raise StressError("CPU1 observation interrupted")
        samples.append(sampler.sample(index, deadline_ns))
    return samples, {"observed_samples": len(samples)}


def run_ui_freeze_window(
    duration: int,
    ui_pid: int,
    expected_executable: str,
    sampler: MetricsSampler,
    stop: dict[str, bool],
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    if ui_pid <= 1 or ui_pid == os.getpid():
        raise StressError(f"unsafe UI PID: {ui_pid}")
    executable, start_ticks = process_identity(ui_pid)
    if executable != expected_executable:
        raise StressError(
            f"UI executable mismatch: pid={ui_pid} actual={executable!r} "
            f"expected={expected_executable!r}"
        )
    samples: list[dict[str, Any]] = []
    deadline_ns = time.monotonic_ns()
    sample_count = duration * (1_000_000_000 // SAMPLE_INTERVAL_NS)
    frozen = False
    try:
        os.kill(ui_pid, signal.SIGSTOP)
        if not wait_for_state(ui_pid, True, time.monotonic() + 2.0):
            raise StressError(f"UI PID {ui_pid} did not enter stopped state")
        frozen = True
        for index in range(1, sample_count + 1):
            deadline_ns += SAMPLE_INTERVAL_NS
            while time.monotonic_ns() < deadline_ns and not stop["requested"]:
                remaining = deadline_ns - time.monotonic_ns()
                time.sleep(min(0.01, max(0.0, remaining / 1_000_000_000)))
            if stop["requested"]:
                raise StressError("UI freeze interrupted")
            current_identity = process_identity(ui_pid)
            if current_identity != (executable, start_ticks):
                raise StressError("UI PID identity changed while frozen")
            samples.append(sampler.sample(index, deadline_ns))
    finally:
        if frozen:
            try:
                if process_identity(ui_pid) == (executable, start_ticks):
                    os.kill(ui_pid, signal.SIGCONT)
                    if not wait_for_state(ui_pid, False, time.monotonic() + 2.0):
                        raise StressError(f"UI PID {ui_pid} did not resume")
            except FileNotFoundError:
                pass
    return samples, {"ui_pid": ui_pid, "ui_start_ticks": start_ticks}


def validate_output_path(path: Path, evidence_root: Path = EVIDENCE_ROOT) -> Path:
    if not path.is_absolute() or path.parent != evidence_root or path.suffix != ".jsonl":
        raise StressError(f"output must be a new .jsonl directly under {evidence_root}")
    if path.exists():
        raise StressError(f"output already exists: {path}")
    return path


def write_evidence(
    path: Path,
    header: dict[str, Any],
    samples: list[dict[str, Any]],
    terminal: dict[str, Any],
) -> str:
    if WINDOW_ACTIVE:
        raise StressError("evidence write attempted inside measurement window")
    payload = "".join(
        json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
        for item in (header, *samples, terminal)
    ).encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0)
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except Exception:
        try:
            path.unlink()
        except FileNotFoundError:
            pass
        raise
    return hashlib.sha256(payload).hexdigest()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("observe", "stress", "freeze-ui"), required=True)
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ui-pid", type=int)
    parser.add_argument("--expect-ui-executable")
    parser.add_argument(
        "--require-motion", action="store_true",
        help="require continuous native motion; implied by stress/freeze-ui",
    )
    parser.add_argument("--linuxcnc-log", type=Path, default=LINUXCNC_LOG)
    return parser


def main(argv: list[str] | None = None) -> int:
    global WINDOW_ACTIVE
    args = build_parser().parse_args(argv)
    output: Path | None = None
    samples: list[dict[str, Any]] = []
    header: dict[str, Any] = {"type": "header", "mode": args.mode}
    terminal: dict[str, Any] = {"type": "terminal", "status": "fail"}
    stop = {"requested": False}
    previous: dict[int, object] = {}
    try:
        duration = mode_duration(args.mode, args.duration_seconds)
        output = validate_output_path(args.output)
        api_affinity, proc_affinity = bind_and_prove_cpu1()
        first = query_backend_identity()
        identity = backend_identity(first)
        runtime_log_start = count_runtime_log_markers(args.linuxcnc_log)
        kernel_fault_start = capture_kernel_fault_counters()
        hal_module, linuxcnc_module = load_runtime_modules()
        require_motion = args.require_motion or args.mode in ("stress", "freeze-ui")
        sampler = MetricsSampler(
            hal_module, linuxcnc_module,
            initial_dc_seq=int(first["dc_seq"], 10),
            initial_dc_errors=int(first["dc_errors"], 10),
            require_motion=require_motion,
        )
        window_sticky_start = sampler.arm_window()
        header.update({
            "duration_s": duration, "sample_interval_ns": SAMPLE_INTERVAL_NS,
            "expected_samples": duration * 10,
            "api_affinity": sorted(api_affinity), "proc_affinity": sorted(proc_affinity),
            "backend_start": first, "started_monotonic_ns": time.monotonic_ns(),
            "linuxcnc_log": str(args.linuxcnc_log),
            "runtime_log_start": runtime_log_start,
            "kernel_fault_start": kernel_fault_start,
            "window_sticky_start": window_sticky_start,
            "motion_required": require_motion,
        })
        print(
            "V5_CPU1_MOTION_STRESS_READY "
            f"mode={args.mode} api={sorted(api_affinity)} proc={sorted(proc_affinity)} "
            f"generation={identity[0]} require_motion={int(require_motion)} "
            f"status=/proc/self/status output={output}"
        )
        previous = install_stop_handlers(stop)
        WINDOW_ACTIVE = True
        try:
            if args.mode == "observe":
                if args.ui_pid is not None or args.expect_ui_executable is not None:
                    raise StressError("UI identity arguments are invalid in observe mode")
                samples, detail = run_observe_window(duration, sampler, stop)
            elif args.mode == "stress":
                if args.ui_pid is not None or args.expect_ui_executable is not None:
                    raise StressError("UI identity arguments are invalid in stress mode")
                samples, detail = run_stress_window(duration, sampler, stop)
            else:
                if args.ui_pid is None or not args.expect_ui_executable:
                    raise StressError("freeze-ui requires --ui-pid and --expect-ui-executable")
                samples, detail = run_ui_freeze_window(
                    duration, args.ui_pid, args.expect_ui_executable, sampler, stop
                )
        finally:
            WINDOW_ACTIVE = False
        window_sticky_end = sampler.finish_window()
        runtime_log_end = count_runtime_log_markers(args.linuxcnc_log)
        kernel_fault_end = capture_kernel_fault_counters()
        second = query_backend_identity(expected=identity)
        violations = [
            f"sample[{sample['index']}]:{code}"
            for sample in samples for code in sample["violations"]
        ]
        warnings = sorted({
            code for sample in samples for code in sample["warnings"]
        })
        if len(samples) != duration * 10:
            violations.append(f"sample_count:{len(samples)}/{duration * 10}")
        for name, start in runtime_log_start.items():
            end = runtime_log_end[name]
            if end != start:
                violations.append(f"runtime_log_{name}:{start}/{end}")
        for name, start in kernel_fault_start.items():
            end = kernel_fault_end[name]
            if end != start:
                violations.append(f"kernel_fault_{name}:{start}/{end}")
        for name, value in window_sticky_end["tmax_ns"].items():
            if value >= EXPECTED_SERVO_PERIOD_NS:
                violations.append(f"function_tmax_overrun:{name}:{value}")
        for name in (
            "runtime_period_fault_count",
            "domain_wc_incomplete_count",
            "all_op_false_count",
        ):
            if window_sticky_end[name] != window_sticky_start[name]:
                violations.append(
                    f"window_sticky_{name}:"
                    f"{window_sticky_start[name]}/{window_sticky_end[name]}"
                )
        if (window_sticky_end["runtime_period_warning_count"] !=
                window_sticky_start["runtime_period_warning_count"]):
            warnings.append(
                "window_sticky_runtime_period_warning_count:"
                f"{window_sticky_start['runtime_period_warning_count']}/"
                f"{window_sticky_end['runtime_period_warning_count']}"
            )
        window_phase = window_sticky_end["runtime_phase_step_max_ns"]
        window_period_error = window_sticky_end["runtime_period_error_max_ns"]
        window_servo_tmax = window_sticky_end["tmax_ns"]["servo-thread.tmax"]
        window_combined = window_servo_tmax + max(window_phase, window_period_error)
        if window_phase > TARGET_RUNTIME_PHASE_STEP_NS:
            warnings.append("window_runtime_phase_step_target")
        if window_period_error > TARGET_RUNTIME_PERIOD_ERROR_NS:
            warnings.append("window_runtime_period_error_target")
        if window_servo_tmax > TARGET_SERVO_TMAX_NS:
            warnings.append("window_servo_tmax_target")
        if window_combined > TARGET_COMBINED_BUDGET_NS:
            warnings.append(f"window_combined_budget_target:{window_combined}")
        if window_phase > HARD_RUNTIME_PHASE_STEP_NS:
            violations.append("window_runtime_phase_step_max")
        if window_period_error > HARD_RUNTIME_PERIOD_ERROR_NS:
            violations.append("window_runtime_period_error_max")
        if window_combined >= HARD_COMBINED_BUDGET_NS:
            violations.append(f"window_combined_budget_hard:{window_combined}")
        if violations:
            raise StressError(";".join(violations[:20]))
        terminal.update({
            "status": "pass", "ended_monotonic_ns": time.monotonic_ns(),
            "backend_end": second, "samples": len(samples), "detail": detail,
            "runtime_log_end": runtime_log_end,
            "kernel_fault_end": kernel_fault_end,
            "window_sticky_end": window_sticky_end,
            "warnings": sorted(set(warnings)),
            "window_combined_budget_ns": window_combined,
        })
        digest = write_evidence(output, header, samples, terminal)
        print(
            "V5_CPU1_MOTION_STRESS_OK "
            f"mode={args.mode} duration_s={duration} samples={len(samples)} "
            f"warnings={len(set(warnings))} "
            f"sha256={digest} output={output}"
        )
        return 0
    except (OSError, StressError, ValueError) as exc:
        WINDOW_ACTIVE = False
        terminal.update({
            "status": "fail", "ended_monotonic_ns": time.monotonic_ns(),
            "error": str(exc), "samples": len(samples),
        })
        if output is not None and not output.exists():
            try:
                digest = write_evidence(output, header, samples, terminal)
                terminal_message = f" evidence={output} sha256={digest}"
            except Exception as write_exc:
                terminal_message = f" evidence_write_failed={write_exc}"
        else:
            terminal_message = ""
        print(f"V5_CPU1_MOTION_STRESS_FAIL:{exc}{terminal_message}", file=sys.stderr)
        return 1
    finally:
        WINDOW_ACTIVE = False
        if previous:
            restore_handlers(previous)


if __name__ == "__main__":
    raise SystemExit(main())
