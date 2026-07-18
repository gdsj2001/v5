#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path
import measure_v5_cold_boot as measure

from measure_v5_cold_boot import (DeterministicProbeError, FatalMeasurementError,
                                  TransientProbeError, canonical_publisher_argv,
                                  parse_boot_stages, persist_cycle_evidence,
                                  prove_terminal, receive_declared_response,
                                  require_resource_lock, run, summarize_results,
                                  terminal_complete, write_segments)

lines = [(1.2, "noise"), (2.5, "V5_BOOT_STAGE stage=linuxcnc_ready uptime_ms=21220")]
assert parse_boot_stages(lines) == [{"stage": "linuxcnc_ready", "uptime_s": 21.22, "host_elapsed_s": 2.5}]
timeout = run(["python", "-c", "import time; time.sleep(1)"], timeout=0.01)
assert timeout.returncode == 124 and "TIMEOUT" in timeout.stdout
probe = {key: True for key in ("ethercat_health",
                                "command_gate_ready", "position_fresh", "wcs_fresh", "state_fresh",
                                "touch_registered", "estop_active")}
probe["machine_enabled"] = False
pages = []
for index, page in enumerate(
        ("main", "settings", "tool", "probe", "offset", "io", "network", "program", "mdi"), 1):
    pages.append({"page": page, "completed": index, "total": 9, "worker_id": 0,
                  "cache_valid": 1, "invalidation_clean": 1, "elapsed_us": 10,
                  "yield_us": 1, "create_us": 4, "prepare_us": 5,
                  "cpu_pct_x100": index, "peak_cpu_pct_x100": index,
                  "budget_bytes": 31_948_800})
info = {"ui_ready": True, "width": 1024, "height": 600,
        "ready_metadata": {"cache_queue": pages, "cache_page_count": 9,
                           "cache_budget_bytes": 31_948_800, "current_frame_id": 2,
                           "ui_pid": 123,
                           "first_frame": {"x": 0, "y": 0, "w": 1024, "h": 600,
                                           "frame_id": 2, "base_frame_id": 1}}}
assert terminal_complete(probe, info)
probe["state_fresh"] = False
assert not terminal_complete(probe, info)
measure_source = Path(__file__).with_name("measure_v5_cold_boot.py").read_text(encoding="utf-8")
power_source = Path(__file__).parents[1].joinpath("v5_board_power_cycle.py").read_text(encoding="utf-8")
assert 'POWER_TOOL = ROOT / "tools/v5_board_power_cycle.py"' in measure_source
assert "RELAY_COMMANDS" not in measure_source
assert 'result["power_on_monotonic_s"]' in power_source
assert power_source.index("command_monotonic_ns = time.monotonic_ns()") < power_source.index("time.sleep(0.12)")
assert "console_ready.wait" in measure_source and "terminal_complete" in measure_source
assert "parse_boot_stages(normalized_serial)" not in measure_source
for token in (
        "fields[3]==1 and fields[4]==0",
        'if not chunk: raise RuntimeError("command gate EOF',
        "declared<44 or declared>4096",
        "pb[6]>pa[6]", "wb[10]>wa[10]",
        "pa[7:21]+pb[7:21]", "wa[11:56]+wb[11:56]",
        "<=1000000000", "len(wfields)==2", "proc_start_ticks(wpid)==wstart",
        "proc_start_ticks(ppid)==pstart", "wmatches==[wpid]",
        "ver==2", "len(raw)==840", "payload==808", "valid_mask&3==3", "not(typed_flags&4)",
        "sseq2>sseq", "stat.S_ISCHR(os.stat(touch_real).st_mode)",
        'ui_argv==[b"/usr/libexec/8ax/v5_lvgl_shell",b"--serve"]',
        "taskset -c 1", "power_stdout.log"):
    assert token in measure_source, token
summary = summarize_results([{"cycle": 1, "ok": True, "segments": [
    {"stage": "power_on", "host_elapsed_s": 0},
    {"stage": "ssh_ready", "host_elapsed_s": 10},
    {"stage": "ui_ready", "host_elapsed_s": 25}]},
    {"cycle": 2, "ok": False, "segments": []}])
assert summary["total_s_median"] == 25 and summary["total_s_max"] == 25
assert summary["failed_count"] == 1 and summary["cycles"][0]["longest_five"][0]["delta_s"] == 15
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "segments.tsv"
    write_segments(path, [{"stage": "ui_ready", "host_elapsed_s": 29.1, "board_uptime_s": 28.7}])
    rows = list(csv.DictReader(path.open(encoding="utf-8"), delimiter="\t"))
    assert rows[0]["stage"] == "ui_ready" and rows[0]["board_uptime_s"] == "28.7"
    lock = Path(temporary) / "vm_board.lock"
    measure.RESOURCE_LOCK = lock
    try:
        require_resource_lock("owner-1")
    except RuntimeError:
        pass
    else:
        raise AssertionError("missing resource lock was accepted")
    lock.write_text("lock_version=1\nthread_id=owner-1\nfile=resource:vm_board\n", encoding="utf-8")
    require_resource_lock("owner-1")

# Behavior: a transport/startup miss is retried once, exactly two seconds after
# the first probe returns. A deterministic owner/ABI failure is never retried.
attempts = []
sleeps = []
good_probe = dict(probe)
good_probe["uptime_s"] = 10.0
def transient_then_ready(_target, _pid):
    attempts.append(time.monotonic())
    if len(attempts) == 1:
        raise TransientProbeError("startup")
    return good_probe
proved, count = prove_terminal("board", info, transient_then_ready,
                               lambda value, _info: value is good_probe,
                               lambda seconds: sleeps.append(seconds))
assert proved is good_probe and count == 2 and sleeps == [2.0] and len(attempts) == 2
deterministic_calls = []
try:
    prove_terminal("board", info,
                   lambda _target, _pid: deterministic_calls.append(1) or {"bad": True},
                   lambda _probe, _info: False,
                   lambda _seconds: (_ for _ in ()).throw(AssertionError("unexpected retry")))
except DeterministicProbeError as exc:
    assert exc.probe == {"bad": True}
else:
    raise AssertionError("deterministic terminal failure was accepted")
assert len(deterministic_calls) == 1

def run_injected_failure(kind: str) -> dict:
    released = []
    old_capture = measure.capture_console
    old_run = measure.run
    old_http = measure.http_probe
    old_prove = measure.prove_terminal
    old_fetch = measure.fetch_boot_logs
    def fake_capture(_port, stop, ready, sink, errors, _handles):
        if kind == "serial":
            errors.append("serial injected")
        else:
            sink.extend([(time.monotonic(), "U-Boot injected"),
                         (time.monotonic(), "Starting kernel ...")])
        ready.set()
        stop.wait(2.0)
        released.append(True)
    def fake_run(args, timeout=15.0):
        if str(measure.POWER_TOOL) in args:
            output = Path(args[args.index("--json-out") + 1])
            output.write_text("not-json" if kind == "json" else json.dumps({
                "power_on_monotonic_s": time.monotonic()}), encoding="utf-8")
            return subprocess.CompletedProcess(args, 0, "power-ok")
        if kind == "ssh" and args and args[0] == "ssh":
            raise RuntimeError("ssh injected")
        return subprocess.CompletedProcess(args, 0, "")
    ready_info = dict(info)
    ready_info["ready_metadata"] = dict(info["ready_metadata"], ui_pid=123)
    def fake_http(_host):
        if kind == "http":
            raise ValueError("http injected")
        if kind == "keyboard":
            raise KeyboardInterrupt("keyboard injected")
        return ready_info if kind == "terminal" else None
    def fake_prove(_target, observed):
        raise DeterministicProbeError("terminal injected", {"partial": observed["ui_ready"]})
    measure.capture_console = fake_capture
    measure.run = fake_run
    measure.http_probe = fake_http
    measure.prove_terminal = fake_prove
    measure.fetch_boot_logs = lambda _target: {
        "ui": {"stdout": "V5_BOOT_STAGE stage=ui_ready uptime_ms=1\n",
               "stderr": "", "returncode": 0},
        "runtime": {"stdout": "runtime evidence\n", "stderr": "", "returncode": 0}}
    try:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            try:
                measure.measure_cycle(1, root, "COM3", "COM4", "board",
                                      "192.0.2.1", 0.05)
            except BaseException:
                pass
            else:
                raise AssertionError(kind + " exception was accepted")
            payload = json.loads((root / "cycle-01/result.json").read_text(encoding="utf-8"))
            assert payload["ok"] is False and payload["error"]
            assert (root / "cycle-01/serial.log").exists()
            assert (root / "cycle-01/power_stdout.log").exists()
            assert (root / "cycle-01/segments.tsv").exists()
            if kind == "terminal":
                assert payload["remote_info"]["ui_ready"] is True
                assert payload["probe"] == {"partial": True}
                assert any(row["stage"] == "ui_ready" for row in payload["segments"])
                assert (root / "cycle-01/v5_ui_relay.log").read_text(encoding="utf-8")
                assert (root / "cycle-01/boot_runtime.log").read_text(encoding="utf-8")
                assert payload["log_fetch"]["ui"]["returncode"] == 0
            if kind in ("json", "http", "serial", "keyboard"):
                assert payload["log_fetch"] is None
                assert payload["log_fetch_errors"] == [
                    "log fetch skipped: SSH never became ready"]
                assert not (root / "cycle-01/v5_ui_relay.log").read_text(encoding="utf-8")
                assert not (root / "cycle-01/boot_runtime.log").read_text(encoding="utf-8")
            assert released, kind + " did not release the console capture"
            return payload
    finally:
        measure.capture_console = old_capture
        measure.run = old_run
        measure.http_probe = old_http
        measure.prove_terminal = old_prove
        measure.fetch_boot_logs = old_fetch

for injected in ("json", "http", "ssh", "terminal", "serial", "keyboard"):
    run_injected_failure(injected)

# Complete canonical publisher argv is accepted. Missing, repeated, additional
# flags and a second matching process all fail the uniqueness predicate.
position_argv = ["python3"] + measure.POSITION_PUBLISHER_ARGV
assert canonical_publisher_argv("/usr/bin/python3.7", position_argv, "position")
for invalid in (position_argv[:-2], position_argv + ["--extra"],
                position_argv + ["--path", "/tmp/duplicate"]):
    assert not canonical_publisher_argv("/usr/bin/python3.7", invalid, "position")
assert not canonical_publisher_argv("/usr/bin/python3", position_argv, "position")
assert sum(canonical_publisher_argv("/usr/bin/python3.7", argv, "position")
           for argv in (position_argv, list(position_argv))) == 2
for expected in measure.WCS_PUBLISHER_ARGV:
    assert canonical_publisher_argv("/usr/bin/python3.7", ["python3"] + expected, "wcs")

# Executable response parser: complete frame succeeds, EOF and oversized
# declared length fail without looping.
response = struct.pack("<III8i", 0x56354347, 4, 44, *([0] * 8))
chunks = iter((response[:7], response[7:]))
assert receive_declared_response(lambda _size: next(chunks, b"")) == response
for broken in (struct.pack("<III", 1, 1, 44),
               struct.pack("<III", 1, 1, 5000)):
    chunks = iter((broken, b""))
    try:
        receive_declared_response(lambda _size: next(chunks, b""))
    except RuntimeError:
        pass
    else:
        raise AssertionError("invalid response framing was accepted")

# Non-JSON output and a real remote nonzero are deterministic and must not
# consume the transient retry.
original_run = measure.run
for completed in (subprocess.CompletedProcess(["ssh"], 1, "remote failed"),
                  subprocess.CompletedProcess(["ssh"], 0, "not-json")):
    calls = []
    measure.run = lambda _args, timeout=5.0, value=completed: calls.append(1) or value
    try:
        prove_terminal("board", info, measure.ssh_probe, lambda _p, _i: True,
                       lambda _s: (_ for _ in ()).throw(AssertionError("retried deterministic output")))
    except DeterministicProbeError:
        pass
    else:
        raise AssertionError("deterministic SSH probe corruption was accepted")
    assert len(calls) == 1
measure.run = original_run

# ui_ready is frozen at the HTTP observation before a deliberately slow SSH
# light probe. The later proof and log fetch do not move that main boundary.
old_capture, old_run = measure.capture_console, measure.run
old_http, old_prove = measure.http_probe, measure.prove_terminal
old_fetch, old_append = measure.fetch_boot_logs, measure.append_ui_stage_evidence
released = []
def ready_capture(_port, stop, ready, sink, _errors, _handles):
    sink.extend([(time.monotonic(), "U-Boot fixture"),
                 (time.monotonic(), "Starting kernel fixture")])
    ready.set(); stop.wait(2.0); released.append(True)
def slow_ssh_run(args, timeout=15.0):
    if str(measure.POWER_TOOL) in args:
        Path(args[args.index("--json-out") + 1]).write_text(
            json.dumps({"power_on_monotonic_s": time.monotonic()}), encoding="utf-8")
        return subprocess.CompletedProcess(args, 0, "power")
    if args and args[0] == "ssh":
        time.sleep(0.05)
    return subprocess.CompletedProcess(args, 0, "")
measure.capture_console = ready_capture
measure.run = slow_ssh_run
measure.http_probe = lambda _host: info
measure.prove_terminal = lambda _target, _info: (good_probe, 1)
measure.fetch_boot_logs = lambda _target: {
    "ui": {"stdout": "ui log\n", "stderr": "", "returncode": 0},
    "runtime": {"stdout": "runtime log\n", "stderr": "", "returncode": 0}}
measure.append_ui_stage_evidence = lambda _state, require_complete: None
try:
    with tempfile.TemporaryDirectory() as temporary:
        payload = measure.measure_cycle(1, Path(temporary), "COM3", "COM4",
                                        "board", "192.0.2.1", 0.5)
        times = {row["stage"]: row["host_elapsed_s"] for row in payload["segments"]}
        assert times["ui_ready"] + 0.04 <= times["ssh_ready"]
        assert payload["final"]["ui_ready_host_elapsed_s"] == times["ui_ready"]
        assert released
finally:
    measure.capture_console, measure.run = old_capture, old_run
    measure.http_probe, measure.prove_terminal = old_http, old_prove
    measure.fetch_boot_logs, measure.append_ui_stage_evidence = old_fetch, old_append

# A console thread that ignores stop and has no closeable handle is fatal, so a
# multi-cycle run cannot continue and contend for COM4.
old_capture, old_run, old_http = measure.capture_console, measure.run, measure.http_probe
never = threading.Event()
def stuck_capture(_port, _stop, ready, _sink, _errors, _handles):
    ready.set(); never.wait(60.0)
def power_then_http_error(args, timeout=15.0):
    if str(measure.POWER_TOOL) in args:
        Path(args[args.index("--json-out") + 1]).write_text(
            json.dumps({"power_on_monotonic_s": time.monotonic()}), encoding="utf-8")
    return subprocess.CompletedProcess(args, 0, "")
measure.capture_console = stuck_capture
measure.run = power_then_http_error
measure.http_probe = lambda _host: (_ for _ in ()).throw(ValueError("stop fixture"))
try:
    with tempfile.TemporaryDirectory() as temporary:
        try:
            measure.measure_cycle(1, Path(temporary), "COM3", "COM4",
                                  "board", "192.0.2.1", 0.05)
        except FatalMeasurementError:
            pass
        else:
            raise AssertionError("stuck console thread was not fatal")
finally:
    measure.capture_console, measure.run, measure.http_probe = old_capture, old_run, old_http
    never.set()

# A transient file-write failure is retried while preserving the rest of the
# evidence bundle.
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    original_write_segments = measure.write_segments
    calls = []
    def flaky_segments(path, rows):
        calls.append(1)
        if len(calls) == 1:
            raise OSError("write injected")
        original_write_segments(path, rows)
    measure.write_segments = flaky_segments
    try:
        errors = persist_cycle_evidence(root, {
            "cycle": 1, "ok": False, "error": "injected", "capture_started": 1.0,
            "t0": 2.0, "serial_lines": [(2.1, "serial")], "rows": [],
            "remote_info": {"saved": True}, "probe": {"saved": True},
            "final": {"saved": True}})
    finally:
        measure.write_segments = original_write_segments
    assert not errors and len(calls) == 2
    assert (root / "segments.tsv").exists() and (root / "result.json").exists()

# Persistent directory/text failures are reported separately and never replace
# the original measurement error.
with tempfile.TemporaryDirectory() as temporary:
    state = {"cycle": 1, "ok": False, "error": "ORIGINAL_ERROR",
             "capture_started": 1.0, "t0": 2.0, "serial_lines": [], "rows": []}
    original_write_text = Path.write_text
    Path.write_text = lambda *_args, **_kwargs: (_ for _ in ()).throw(OSError("persistent write"))
    try:
        errors = persist_cycle_evidence(Path(temporary), state)
    finally:
        Path.write_text = original_write_text
    assert errors and state["error"] == "ORIGINAL_ERROR"
print("V5_COLD_BOOT_MEASURE_SMOKE_OK")
