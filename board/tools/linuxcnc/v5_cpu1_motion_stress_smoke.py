#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import time
import tempfile
from pathlib import Path


TOOL = Path(__file__).with_name("v5_cpu1_motion_stress.py")
SPEC = importlib.util.spec_from_file_location("v5_cpu1_motion_stress", TOOL)
assert SPEC and SPEC.loader
stress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stress)


class FakeHal:
    def __init__(self) -> None:
        self.values = {
            "servo-thread.time": 250000,
            "servo-thread.tmax": 600000,
            "lcec.read-all.tmax": 300000,
            "lcec.write-all.tmax": 250000,
            "motion.servo.last-period": 1000000,
            "motion.requested-vel": 12.0,
            "motion.current-vel": 11.5,
            "lcec.0.slaves-responding": 5,
            "lcec.0.all-op": True,
            "lcec.0.domain-working-counter": 10,
            "lcec.0.domain-wc-complete": True,
            "lcec.0.dc-phased": True,
            "lcec.0.dc-time-valid": True,
            "lcec.0.dc-time-ok-seq": 101,
            "lcec.0.dc-time-age-cycles": 0,
            "lcec.0.dc-time-error-count": 7,
            "lcec.0.startup-phase-span": 129457,
            "lcec.0.runtime-phase-step": 1200,
            "lcec.0.runtime-phase-step-max": 5000,
            "lcec.0.runtime-period-ns": 1_000_000,
            "lcec.0.runtime-period-error-max": 5_000,
            "lcec.0.runtime-period-warning-count": 5,
            "lcec.0.runtime-period-fault-count": 1,
            "lcec.0.runtime-period-event-clear": 0,
            "lcec.0.runtime-period-event-valid": False,
            "lcec.0.runtime-period-event-sequence": 0,
            "lcec.0.runtime-period-event-period-ns": 0,
            "lcec.0.runtime-period-event-error-ns": 0,
            "lcec.0.runtime-period-event-phase-step-ns": 0,
            "lcec.0.runtime-period-event-working-counter": 0,
            "lcec.0.runtime-period-event-slaves-responding": 0,
            "lcec.0.runtime-period-event-wc-complete": False,
            "lcec.0.runtime-period-event-all-op": False,
            "lcec.0.runtime-period-event-dc-phased": False,
            "lcec.0.runtime-period-event-dc-valid": False,
            "lcec.0.runtime-period-event-dc-age-cycles": 0,
            "lcec.0.runtime-period-event-app-time-hi": 0,
            "lcec.0.runtime-period-event-app-time-lo": 0,
            "lcec.0.runtime-period-event-write-prelock-ns": 0,
            "lcec.0.runtime-period-event-write-lock-wait-ns": 0,
            "lcec.0.runtime-period-event-write-prepare-ns": 0,
            "lcec.0.runtime-period-event-write-send-ns": 0,
            "lcec.0.runtime-period-event-write-total-ns": 0,
            "lcec.0.domain-wc-incomplete-count": 3,
            "lcec.0.all-op-false-count": 2,
        }
        for joint in range(5):
            self.values[f"joint.{joint}.motor-pos-fb"] = float(joint)
            self.values[f"joint.{joint}.f-error"] = 0.0001 * joint
            self.values[f"joint.{joint}.f-error-lim"] = 0.1
            self.values[f"lcec.0.s{joint}.statusword"] = 0x0027

    def get_value(self, name: str):
        return self.values[name]

    def set_p(self, name: str, value: str) -> None:
        self.values[name] = int(value, 10)
        if name == "lcec.0.runtime-period-event-clear" and int(value, 10):
            for field in (
                "valid", "sequence", "period-ns", "error-ns",
                "phase-step-ns", "working-counter", "slaves-responding",
                "wc-complete", "all-op", "dc-phased", "dc-valid",
                "dc-age-cycles", "app-time-hi", "app-time-lo",
                "write-prelock-ns", "write-lock-wait-ns",
                "write-prepare-ns", "write-send-ns", "write-total-ns",
            ):
                self.values[f"lcec.0.runtime-period-event-{field}"] = 0
            self.values[name] = 0

    def get_info_params(self) -> list[dict[str, object]]:
        return [
            {"NAME": name, "VALUE": value}
            for name, value in self.values.items()
            if name.endswith(".tmax")
        ]

    class Component:
        def __init__(self, name: str) -> None:
            self.name = name
            self.is_ready = False

        def ready(self) -> None:
            self.is_ready = True

    def component(self, name: str) -> "FakeHal.Component":
        return self.Component(name)


class FakeStatus:
    queue = 42

    def poll(self) -> None:
        return None


class FakeLinuxCnc:
    @staticmethod
    def stat() -> FakeStatus:
        return FakeStatus()


def main() -> int:
    assert stress.parse_cpu_list("1") == {1}
    assert stress.parse_cpu_list("1,3-4") == {1, 3, 4}
    for invalid in ("", "2-1", "-1"):
        try:
            stress.parse_cpu_list(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(f"invalid CPU list accepted: {invalid!r}")

    assert stress.mode_duration("stress", None) == 60
    assert stress.mode_duration("observe", None) == 60
    assert stress.mode_duration("observe", 600) == 600
    assert stress.mode_duration("freeze-ui", None) == 30
    assert stress.validate_duration(30) == 30
    assert stress.validate_duration(60) == 60
    for invalid_duration in (0, 29, 61):
        try:
            stress.validate_duration(invalid_duration)
        except stress.StressError:
            pass
        else:
            raise AssertionError(f"invalid duration accepted: {invalid_duration}")
    try:
        stress.mode_duration("observe", 601)
    except stress.StressError:
        pass
    else:
        raise AssertionError("observe duration above 600 seconds accepted")

    stat = "99 (v5 ui worker) S " + " ".join(str(value) for value in range(1, 25))
    assert stress.process_start_ticks(stat) == 19

    with tempfile.TemporaryDirectory() as raw_root:
        root = Path(raw_root)
        (root / "self").mkdir()
        (root / "self" / "status").write_text(
            "Name:\ttest\nCpus_allowed_list:\t1\n", encoding="ascii"
        )
        assert stress.read_status_field(root / "self" / "status", "Cpus_allowed_list") == "1"

        pid_root = root / "42"
        pid_root.mkdir()
        (pid_root / "cmdline").write_bytes(b"/usr/libexec/8ax/v5_lvgl_shell\0--serve\0")
        (pid_root / "stat").write_text(stat, encoding="ascii")
        executable, start_ticks = stress.process_identity(42, root)
        assert executable == "/usr/libexec/8ax/v5_lvgl_shell"
        assert start_ticks == 19

        (root / "stat").write_text(
            "cpu 1 2 3 4\ncpu0 1 2 3 4 5 6 7 8\n"
            "cpu1 8 7 6 5 4 3 2 1\n", encoding="ascii"
        )
        assert stress.read_cpu_ticks(root)["cpu0"] == [1, 2, 3, 4, 5, 6, 7, 8]
        realtime_log = root / "v5_linuxcnc.log"
        realtime_log.write_bytes(b"ok\nUnexpected realtime delay\nok\n")
        assert stress.count_realtime_delays(realtime_log) == 1
        assert stress.count_runtime_log_markers(realtime_log)[
            "unexpected_realtime_delay"
        ] == 1
        kernel_counts = stress.count_kernel_faults(
            "EtherCAT WARNING 0: 1 datagram TIMED OUT!\n"
            "EtherCAT ERROR 0: Failed to get reference clock time: -5\n"
        )
        assert kernel_counts["datagram_timed_out"] == 1
        assert kernel_counts["reference_clock_error"] == 1
        fake_hal = FakeHal()
        sampler = stress.MetricsSampler(
            fake_hal, FakeLinuxCnc(), initial_dc_seq=100,
            initial_dc_errors=7, proc_root=root,
        )
        assert sampler.hal_component.is_ready
        assert sampler.arm_window() == {
            "runtime_period_warning_count": 5,
            "runtime_period_fault_count": 1,
            "domain_wc_incomplete_count": 3,
            "all_op_false_count": 2,
            "runtime_phase_step_max_ns": 0,
            "runtime_period_error_max_ns": 0,
            "motion_required": False,
            "initial_encoder": [0.0, 1.0, 2.0, 3.0, 4.0],
            "tmax_reset_count": 3,
            "previous_tmax_ns": {
                "lcec.read-all.tmax": 300000,
                "lcec.write-all.tmax": 250000,
                "servo-thread.tmax": 600000,
            },
        }
        assert sampler.finish_window()["tmax_ns"] == {
            "lcec.read-all.tmax": 0,
            "lcec.write-all.tmax": 0,
            "servo-thread.tmax": 0,
        }
        saturated_hal = FakeHal()
        saturated_hal.values["lcec.0.domain-wc-incomplete-count"] = stress.UINT32_MAX
        saturated_sampler = stress.MetricsSampler(
            saturated_hal, FakeLinuxCnc(), initial_dc_seq=100,
            initial_dc_errors=7, proc_root=root,
        )
        try:
            saturated_sampler.arm_window()
        except stress.StressError as exc:
            assert "sticky counter saturated" in str(exc)
        else:
            raise AssertionError("saturated HAL sticky counter accepted")
        sample = sampler.sample(1, time.monotonic_ns())
        assert sample["violations"] == [], sample
        assert sample["configured_servo_period_ns"] == 1_000_000
        assert sample["trajectory_queue"] == 42
        assert sample["cia402_operation_enabled"] == [True] * 5
        fake_hal.values["lcec.0.domain-wc-incomplete-count"] = 4
        sticky_failure = sampler.sample(2, time.monotonic_ns())
        assert "wkc_incomplete_sticky" in sticky_failure["violations"]
        fake_hal.values["lcec.0.domain-wc-incomplete-count"] = 3
        fake_hal.values["lcec.0.runtime-phase-step-max"] = 100001
        phase_warning = sampler.sample(3, time.monotonic_ns())
        assert "runtime_phase_step_target" in phase_warning["warnings"]
        assert "runtime_phase_step_max" not in phase_warning["violations"]
        fake_hal.values["lcec.0.runtime-phase-step-max"] = 200001
        phase_failure = sampler.sample(4, time.monotonic_ns())
        assert "runtime_phase_step_max" in phase_failure["violations"]
        fake_hal.values["lcec.0.runtime-phase-step-max"] = 5000
        fake_hal.values["lcec.0.runtime-period-warning-count"] = 6
        warning_sample = sampler.sample(5, time.monotonic_ns())
        assert "runtime_period_warning_sticky" in warning_sample["warnings"]
        assert "runtime_period_fault_sticky" not in warning_sample["violations"]
        fake_hal.values.update({
            "lcec.0.runtime-period-fault-count": 2,
            "lcec.0.runtime-period-error-max": 233060,
            "lcec.0.runtime-period-event-valid": True,
            "lcec.0.runtime-period-event-sequence": 2,
            "lcec.0.runtime-period-event-period-ns": 1_233_060,
            "lcec.0.runtime-period-event-error-ns": 233_060,
            "lcec.0.runtime-period-event-phase-step-ns": 233_060,
            "lcec.0.runtime-period-event-working-counter": 10,
            "lcec.0.runtime-period-event-slaves-responding": 5,
            "lcec.0.runtime-period-event-wc-complete": True,
            "lcec.0.runtime-period-event-all-op": True,
            "lcec.0.runtime-period-event-dc-phased": True,
            "lcec.0.runtime-period-event-dc-valid": True,
            "lcec.0.runtime-period-event-dc-age-cycles": 0,
            "lcec.0.runtime-period-event-app-time-hi": 1,
            "lcec.0.runtime-period-event-app-time-lo": 2,
            "lcec.0.runtime-period-event-write-prelock-ns": 41_000,
            "lcec.0.runtime-period-event-write-lock-wait-ns": 7_000,
            "lcec.0.runtime-period-event-write-prepare-ns": 19_000,
            "lcec.0.runtime-period-event-write-send-ns": 83_000,
            "lcec.0.runtime-period-event-write-total-ns": 150_000,
            "servo-thread.tmax": 563917,
            "lcec.read-all.tmax": 100495,
            "lcec.write-all.tmax": 208601,
        })
        event_sample = sampler.sample(6, time.monotonic_ns())
        assert event_sample["first_runtime_period_event"] == {
            "sequence": 2,
            "period_ns": 1_233_060,
            "period_error_ns": 233_060,
            "phase_step_ns": 233_060,
            "working_counter": 10,
            "slaves_responding": 5,
            "wc_complete": True,
            "all_op": True,
            "dc_phased": True,
            "dc_valid": True,
            "dc_age_cycles": 0,
            "write_segments_ns": {
                "prelock": 41_000,
                "lock_wait": 7_000,
                "prepare": 19_000,
                "send": 83_000,
                "total": 150_000,
            },
            "app_time_ns": (1 << 32) | 2,
            "tmax_ns": {
                "servo-thread.tmax": 563917,
                "lcec.read-all.tmax": 100495,
                "lcec.write-all.tmax": 208601,
            },
        }
        assert "runtime_period_event_missing" not in event_sample["violations"]
        fake_hal.values["lcec.0.runtime-period-fault-count"] = 1
        fake_hal.values["lcec.0.runtime-period-error-max"] = 5_000
        fake_hal.values["lcec.0.runtime-period-event-valid"] = False
        fake_hal.values["motion.servo.last-period"] = 1_900_000
        period_failure = sampler.sample(7, time.monotonic_ns())
        assert "servo_period_out_of_budget" in period_failure["violations"]
        fake_hal.values["motion.servo.last-period"] = 1_000_000

        combined_hal = FakeHal()
        combined_sampler = stress.MetricsSampler(
            combined_hal, FakeLinuxCnc(), initial_dc_seq=100,
            initial_dc_errors=7, proc_root=root,
        )
        combined_sampler.arm_window()
        combined_hal.values["servo-thread.tmax"] = 850_000
        combined_hal.values["lcec.0.runtime-phase-step-max"] = 150_000
        combined_sample = combined_sampler.sample(1, time.monotonic_ns())
        assert "combined_budget_hard" in combined_sample["violations"]
        assert "combined_budget_target" in combined_sample["warnings"]

        inactive_hal = FakeHal()
        inactive_hal.values["motion.requested-vel"] = 0.0
        inactive_hal.values["motion.current-vel"] = 0.0
        inactive_sampler = stress.MetricsSampler(
            inactive_hal, FakeLinuxCnc(), initial_dc_seq=100,
            initial_dc_errors=7, proc_root=root, require_motion=True,
        )
        inactive_sampler.status.queue = 0
        try:
            inactive_sampler.arm_window()
        except stress.StressError as exc:
            assert "motion window is not active" in str(exc)
        else:
            raise AssertionError("inactive motion window accepted")

        motion_hal = FakeHal()
        motion_sampler = stress.MetricsSampler(
            motion_hal, FakeLinuxCnc(), initial_dc_seq=100,
            initial_dc_errors=7, proc_root=root, require_motion=True,
        )
        motion_sampler.arm_window()
        motion_hal.values["joint.0.motor-pos-fb"] = 0.01
        moving_sample = motion_sampler.sample(1, time.monotonic_ns())
        assert moving_sample["violations"] == [], moving_sample
        motion_hal.values["lcec.0.dc-time-ok-seq"] = 102
        stopped_sample = motion_sampler.sample(2, time.monotonic_ns())
        assert "encoder_not_progressing" in stopped_sample["violations"]
        motion_hal.values["joint.0.motor-pos-fb"] = 0.02
        motion_hal.values["joint.0.f-error"] = 0.2
        ferror_sample = motion_sampler.sample(3, time.monotonic_ns())
        assert "following_error_limit_j0" in ferror_sample["violations"]
        observe_samples, observe_detail = stress.run_observe_window(
            0, sampler, {"requested": False}
        )
        assert observe_samples == [] and observe_detail == {"observed_samples": 0}

        evidence = root / "window.jsonl"
        assert stress.validate_output_path(evidence, root) == evidence
        digest = stress.write_evidence(
            evidence, {"type": "header"}, [sample],
            {"type": "terminal", "status": "pass"},
        )
        assert len(digest) == 64
        records = [json.loads(line) for line in evidence.read_text(encoding="utf-8").splitlines()]
        assert [record["type"] for record in records] == ["header", "sample", "terminal"]

    response = stress.parse_backend_response(
        "code=OK generation=4 owner_pid=9 owner_start_ticks=12 armed=1 "
        "data_ready=1 motion_ready=1 revoked=0 slaves=5/5 wkc=10/10 "
        "wkc_complete=1 all_op=1 dc_phased=1 dc_valid=1 dc_age=0 "
        "dc_seq=55 dc_errors=0"
    )
    assert stress.backend_identity(response) == (4, 9, 12)
    stress.WINDOW_ACTIVE = True
    try:
        stress.query_backend_identity(Path("missing"))
    except stress.StressError as exc:
        assert "inside measurement window" in str(exc)
    else:
        raise AssertionError("external command accepted inside measurement window")
    finally:
        stress.WINDOW_ACTIVE = False
    stress.WINDOW_ACTIVE = True
    try:
        stress.count_realtime_delays(Path("missing"))
    except stress.StressError as exc:
        assert "inside measurement window" in str(exc)
    else:
        raise AssertionError("realtime log read accepted inside measurement window")
    finally:
        stress.WINDOW_ACTIVE = False

    source = TOOL.read_text(encoding="utf-8")
    for token in (
        "os.sched_setaffinity",
        "os.sched_getaffinity",
        'Path("/proc")',
        "status=/proc/self/status",
        "signal.SIGSTOP",
        "signal.SIGCONT",
        "finally:",
        "CPU0 appeared in allowed mask",
        "hal.get_value",
        "hal.set_p",
        "hal_module.get_info_params",
        "linuxcnc_module.stat",
        "SAMPLE_INTERVAL_NS = 100_000_000",
        "read_cpu_ticks",
        "backend identity changed across measurement window",
        "external command attempted inside measurement window",
        "realtime log read attempted inside measurement window",
        "runtime_log_start",
        "runtime_log_end",
        "kernel_fault_start",
        "kernel_fault_end",
        "previous_tmax_ns",
        "evidence write attempted inside measurement window",
        "domain-wc-incomplete-count",
        "all-op-false-count",
        "runtime-phase-step-max",
        "runtime-period-warning-count",
        "HARD_RUNTIME_PERIOD_ERROR_NS = 200_000",
        "TARGET_COMBINED_BUDGET_NS = 800_000",
        "MAX_OBSERVE_DURATION_SECONDS = 600",
        "sticky counter saturated",
    ):
        assert token in source, token
    print("V5_CPU1_MOTION_STRESS_SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
