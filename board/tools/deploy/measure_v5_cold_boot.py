#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import shlex
import sys
import threading
import time
import urllib.request
import socket
from pathlib import Path
from typing import Any

import serial

ROOT = Path(__file__).resolve().parents[2]
POWER_TOOL = ROOT / "tools/v5_board_power_cycle.py"
RESOURCE_LOCK = ROOT.parent / "repo_ignored/locks/resources/vm_board.lock"
POSITION_PUBLISHER_ARGV = [
    "/usr/libexec/8ax/v5_position_status_publisher.py",
    "--path", "/dev/shm/v5_native_position_status.bin",
    "--bus-path", "/dev/shm/v5_native_bus_status.bin",
    "--interval-ms", "33",
]
WCS_PUBLISHER_ARGV = [[
    "/usr/libexec/8ax/v5_wcs_status_publisher.py",
    "--path", "/dev/shm/v5_native_wcs_status.bin",
    "--modal-tool-path", "/dev/shm/v5_native_modal_tool_status.bin",
    "--operator-error-path", "/dev/shm/v5_native_operator_error_status.bin",
    "--operator-error-map", "/opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv",
    "--ini", ini,
    "--interval-ms", "200",
] for ini in ("/opt/8ax/v5/linuxcnc/ini/v5_bus.ini",
              "/opt/8ax/v5/linuxcnc/ini/v5_pulse.ini")]


def canonical_publisher_argv(executable: str, argv: list[str], kind: str) -> bool:
    if executable != "/usr/bin/python3.7":
        return False
    expected = [POSITION_PUBLISHER_ARGV] if kind == "position" else WCS_PUBLISHER_ARGV
    return argv[1:] in expected


def require_resource_lock(owner: str) -> None:
    text = RESOURCE_LOCK.read_text(encoding="utf-8") if RESOURCE_LOCK.exists() else ""
    required = ("lock_version=1", "thread_id=" + owner,
                "file=resource:vm_board")
    if not owner or any(token not in text for token in required):
        raise RuntimeError("vm_board.lock is missing or owned by another thread")


def run(args: list[str], timeout: float = 15.0) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(args, text=True, encoding="utf-8", errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(args, 124, (exc.stdout or "") + "\nTIMEOUT")


def receive_declared_response(recv_fn) -> bytes:
    response = b""
    while len(response) < 12:
        chunk = recv_fn(4096)
        if not chunk:
            raise RuntimeError("EOF before response header")
        response += chunk
    declared = int.from_bytes(response[8:12], "little")
    if declared < 44 or declared > 4096:
        raise RuntimeError("response size out of range")
    while len(response) < declared:
        chunk = recv_fn(min(4096, declared - len(response)))
        if not chunk:
            raise RuntimeError("EOF before declared response size")
        response += chunk
    if len(response) != declared:
        raise RuntimeError("response exceeded declared size")
    return response


def parse_boot_stages(lines: list[tuple[float, str]]) -> list[dict[str, Any]]:
    stages = []
    for host_s, line in lines:
        if "V5_BOOT_STAGE " not in line:
            continue
        fields = {}
        for token in line.split("V5_BOOT_STAGE ", 1)[1].split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
        if "stage" in fields and "uptime_ms" in fields:
            stages.append({"stage": fields["stage"],
                           "uptime_s": float(fields["uptime_ms"]) / 1000.0,
                           "host_elapsed_s": round(host_s, 3)})
    return stages


def post_ready_ethercat_errors(
        serial_lines: list[tuple[float, str]]) -> list[dict[str, Any]]:
    ready_seen = False
    failures: list[dict[str, Any]] = []
    markers = ("Working counter changed to 0/", "datagrams TIMED OUT!",
               "datagrams UNMATCHED!")
    for stamp_s, line in serial_lines:
        if "LinuxCNC backend transport ready;" in line:
            ready_seen = True
            continue
        if ready_seen and any(marker in line for marker in markers):
            failures.append({"stamp_s": stamp_s, "line": line})
    return failures


def ssh_probe(target: str, expected_ui_pid: int) -> dict[str, Any] | None:
    script = ("V5_EXPECT_UI_PID=%d taskset -c 1 " % expected_ui_pid) + r'''python3 - <<'PY'
import ctypes,json,os,socket,stat,struct,time
out={"uptime_s":float(open("/proc/uptime").read().split()[0]),"boot_id":open("/proc/sys/kernel/random/boot_id").read().strip()}
gate="/run/8ax_v5_product_ui/v5_command_gate.sock"
try:
 pid=int(open("/run/8ax/v5_command_gate.pid").read().strip())
 cmdline=open("/proc/%d/cmdline"%pid,"rb").read().replace(b"\0",b" ")
 class Request(ctypes.Structure):
  _fields_=[("magic",ctypes.c_uint32),("version",ctypes.c_uint32),("size",ctypes.c_uint32),("op",ctypes.c_uint32),("kind",ctypes.c_int32),("index",ctypes.c_int32),("enabled",ctypes.c_int32),("mask",ctypes.c_uint32),("run",ctypes.c_uint64),("generation",ctypes.c_uint32),("clean_generation",ctypes.c_uint32),("axis",ctypes.c_double),("increment",ctypes.c_double),("points",ctypes.c_double*5),("text",ctypes.c_char*512),("secondary",ctypes.c_char*128),("mode",ctypes.c_char*64),("settings_index",ctypes.c_uint32),("owner_generation",ctypes.c_uint32),("readback_token",ctypes.c_uint32),("project_root",ctypes.c_char*256),("settings_axis",ctypes.c_char*16),("field_key",ctypes.c_char*64),("field_name",ctypes.c_char*128),("value",ctypes.c_char*128)]
 req=Request(); req.magic=0x56354347; req.version=5; req.size=ctypes.sizeof(Request); req.op=2
 s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.settimeout(1); s.connect(gate); s.sendall(bytes(req)); response=b""
 while len(response)<12:
  chunk=s.recv(4096)
  if not chunk: raise RuntimeError("command gate EOF before response header")
  response+=chunk
 declared=struct.unpack_from("<I",response,8)[0]
 if declared<44 or declared>4096: raise RuntimeError("command gate response size out of range")
 while len(response)<declared:
  chunk=s.recv(min(4096,declared-len(response)))
  if not chunk: raise RuntimeError("command gate EOF before declared response size")
  response+=chunk
 if len(response)!=declared: raise RuntimeError("command gate response exceeded declared size")
 s.close(); fields=struct.unpack_from("<III8i",response)
 protocol_ok=(fields[0:3]==(0x56354347,5,len(response)) and fields[3]==1 and
              fields[4]==0 and fields[7:11]==(1,1,1,0))
 out["command_gate_ready"]=stat.S_ISSOCK(os.stat(gate).st_mode) and b"/usr/libexec/8ax/v5_command_gate_server" in cmdline and protocol_ok
 out["estop_active"]=(fields[7:9]==(1,1))
 out["machine_enabled"]=(bool(fields[10]) if fields[9]==1 else None)
except Exception: out["command_gate_ready"]=False
try:
 ui_log=open("/run/8ax/v5_ui_boot.log",encoding="utf-8",errors="replace").read()
 ui_pid=int(os.environ["V5_EXPECT_UI_PID"]); ui_argv=[arg for arg in open("/proc/%d/cmdline"%ui_pid,"rb").read().split(b"\0") if arg]; touch_real=os.path.realpath("/dev/input/by-path/z20-touchscreen")
 held=any(os.path.realpath("/proc/%d/fd/%s"%(ui_pid,fd))==touch_real for fd in os.listdir("/proc/%d/fd"%ui_pid))
 out["touch_registered"]=("v5 touch input enabled device=/dev/input/by-path/z20-touchscreen" in ui_log and ui_argv==[b"/usr/libexec/8ax/v5_lvgl_shell",b"--serve"] and stat.S_ISCHR(os.stat(touch_real).st_mode) and held)
except OSError: out["touch_registered"]=False
def fnv(raw):
 h=2166136261
 for b in raw: h=((h^b)*16777619)&0xffffffff
 return h
def block(path,fmt):
 raw=open(path,"rb").read(fmt.size); return raw,fmt.unpack(raw)
try:
 bf=struct.Struct("<12IQ"+("IIIII"*5)+"II")
 fields=open("/run/8ax/v5_position_status_publisher.pid").read().split(); pwriter=int(fields[2])
 bra,ba=block("/dev/shm/v5_native_bus_status.bin",bf); time.sleep(.25); brb,bb=block("/dev/shm/v5_native_bus_status.bin",bf); now=time.monotonic_ns()
 out["ethercat_health"]=(len(fields)==3 and ba[0:3]==(0x56425553,1,bf.size) and bb[0:3]==(0x56425553,1,bf.size) and ba[3]==bb[3]==1 and ba[4]>0 and bb[4]>ba[4] and not(ba[4]&1) and not(bb[4]&1) and ba[5]==bb[5]==pwriter and ba[6]>0 and bb[6]>=ba[6] and ba[7]>0 and bb[7]>0 and ba[8]&7==7 and bb[8]&7==7 and ba[9:11]==bb[9:11]==(5,5) and ba[11]>0 and bb[11]>=ba[11] and ba[-2]==fnv(bra[:-8]) and bb[-2]==fnv(brb[:-8]) and 0<=now-ba[12]<=1000000000 and 0<=now-bb[12]<=1000000000 and bb[12]>ba[12])
except Exception as e: out["bus_error"]=type(e).__name__+":"+str(e); out["ethercat_health"]=False
print(json.dumps(out,separators=(",",":")))
PY'''
    try:
        result = subprocess.run(
            ["ssh", "-o", "ConnectTimeout=2", target, "sh -s"],
            input=script + "\n",
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            # Keep the final proof fixed-size: two bus frames, one native
            # safety response and one UI process.  Broad /proc scans and
            # repeated full State-frame checks can perturb the two-core board.
            timeout=5.0,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise TransientProbeError(
            "terminal SSH transport timed out output=%s" % (exc.stdout or "")[-300:]
        ) from exc
    if result.returncode in (255, 4294967295):
        raise TransientProbeError("terminal SSH transport failed rc=%d" % result.returncode)
    if result.returncode != 0:
        raise DeterministicProbeError(
            "terminal remote probe failed rc=%d output=%s" %
            (result.returncode, result.stdout[-300:]))
    lines = result.stdout.strip().splitlines()
    try:
        payload = json.loads(lines[0])
    except Exception as exc:
        raise DeterministicProbeError(
            "terminal probe returned invalid JSON: %s" % exc) from exc
    if not isinstance(payload, dict):
        raise DeterministicProbeError("terminal probe JSON is not an object")
    return payload


def http_probe(host: str) -> dict[str, Any] | None:
    try:
        with urllib.request.urlopen("http://%s:18080/remote/info" % host, timeout=0.5) as response:
            return json.loads(response.read().decode("utf-8"))
    except Exception:
        return None


def terminal_complete(probe: dict[str, Any], info: dict[str, Any]) -> bool:
    ready = info.get("ready_metadata") or {}
    queue = ready.get("cache_queue") or []
    required_pages = ["main", "settings", "tool", "probe", "offset", "io", "network", "program", "mdi"]
    cache_ok = len(queue) == len(required_pages)
    observed_peak = 0
    for index, item in enumerate(queue, 1):
        observed_peak = max(observed_peak, int(item.get("cpu_pct_x100") or 0))
        cache_ok = cache_ok and (
            item.get("page") == required_pages[index - 1]
            and int(item.get("completed") or 0) == index
            and int(item.get("total") or 0) == len(required_pages)
            and int(item.get("worker_id", -1)) == 0
            and int(item.get("cache_valid") or 0) == 1
            and int(item.get("invalidation_clean") or 0) == 1
            and int(item.get("elapsed_us") or 0) > 0
            and int(item.get("yield_us") or 0) > 0
            and int(item.get("create_us") or 0) + int(item.get("prepare_us") or 0)
                + int(item.get("yield_us") or 0) <= int(item.get("elapsed_us") or 0)
            and int(item.get("peak_cpu_pct_x100") or -1) == observed_peak
            and int(item.get("budget_bytes") or 0) == 31_948_800)
    first = ready.get("first_frame") or {}
    rects = first.get("rects") or []
    full_blit = (first.get("x") == 0 and first.get("y") == 0
                 and first.get("w") == info.get("width")
                 and first.get("h") == info.get("height")
                 and int(first.get("frame_id") or 0) > 1
                 and int(first.get("base_frame_id") or 0) == 1
                 and (not rects or rects == [{"x": 0, "y": 0,
                                              "w": info.get("width"), "h": info.get("height")}])
                 and int(ready.get("current_frame_id") or 0) >= int(first.get("frame_id") or 0))
    return (info.get("ui_ready") is True and full_blit
            and cache_ok
            and int(ready.get("cache_page_count") or 0) == 9
            and int(ready.get("cache_budget_bytes") or 0) == 31_948_800
            and probe.get("ethercat_health") is True
            and probe.get("command_gate_ready") is True
            and probe.get("touch_registered") is True
            and probe.get("estop_active") is True
            and probe.get("machine_enabled") is False)


class DeterministicProbeError(RuntimeError):
    def __init__(self, message: str, probe: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.probe = probe


class TransientProbeError(RuntimeError):
    pass


class FatalMeasurementError(RuntimeError):
    pass


def prove_terminal(target: str, info: dict[str, Any],
                   probe_fn=ssh_probe, complete_fn=terminal_complete,
                   sleep_fn=time.sleep) -> tuple[dict[str, Any], int]:
    ui_pid = int((info.get("ready_metadata") or {}).get("ui_pid") or 0)
    if ui_pid <= 0:
        raise DeterministicProbeError("ui_ready metadata has no live ui_pid")
    for attempt in (1, 2):
        try:
            probe = probe_fn(target, ui_pid)
        except TransientProbeError:
            probe = None
        if probe is not None:
            if not complete_fn(probe, info):
                raise DeterministicProbeError(
                    "terminal owner/ABI/CRC/identity/safety verification failed", probe)
            return probe, attempt
        if attempt == 1:
            sleep_fn(2.0)
    raise RuntimeError("terminal probe transport/startup state failed twice")


def capture_console(port: str, stop: threading.Event, ready: threading.Event,
                    sink: list[tuple[float, str]], errors: list[str],
                    handles: list[Any]) -> None:
    console = None
    try:
        console = serial.Serial(port, 115200, timeout=0.2)
        handles.append(console)
        console.reset_input_buffer()
        ready.set()
        while not stop.is_set():
            raw = console.readline()
            if raw:
                sink.append((time.monotonic(),
                             raw.decode("utf-8", errors="replace").rstrip()))
    except Exception as exc:
        errors.append("%s: %s" % (type(exc).__name__, exc))
        ready.set()
    finally:
        if console is not None:
            try:
                console.close()
            except Exception as exc:
                errors.append("console close: %s: %s" % (type(exc).__name__, exc))
            try:
                handles.remove(console)
            except ValueError:
                pass


def write_segments(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=("stage", "host_elapsed_s", "board_uptime_s"), delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def fetch_boot_logs(target: str) -> dict[str, dict[str, Any]]:
    commands = {
        "ui": "cat /run/8ax/v5_ui_relay.log",
        "runtime": ("cat /run/8ax/v5_linuxcnc.log "
                    "/run/8ax/v5_position_status_publisher.log "
                    "/run/8ax/v5_wcs_status_publisher.log "
                    "/run/8ax/v5_state_publisher.log"),
    }
    fetched: dict[str, dict[str, Any]] = {}
    for name, command in commands.items():
        try:
            result = subprocess.run(
                ["ssh", "-o", "ConnectTimeout=2", target,
                 "taskset -c 1 sh -c " + shlex.quote(command)],
                text=True, encoding="utf-8", errors="replace",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=6.0, check=False)
            fetched[name] = {"stdout": result.stdout, "stderr": result.stderr,
                             "returncode": result.returncode}
        except Exception as exc:
            fetched[name] = {"stdout": "", "stderr": "%s: %s" %
                             (type(exc).__name__, exc), "returncode": None}
    return fetched


def append_ui_stage_evidence(state: dict[str, Any], require_complete: bool) -> str | None:
    info = state.get("remote_info") or {}
    ready = info.get("ready_metadata") or {}
    created_s = float(ready.get("created_monotonic_ns") or 0) / 1e9
    ui_rows = [row for row in state.get("rows") or [] if row.get("stage") == "ui_ready"]
    if not ui_rows or not state.get("ui_log") or created_s <= 0:
        return "UI stage evidence is unavailable"
    final_host = float(ui_rows[0]["host_elapsed_s"])
    stages = parse_boot_stages([(0.0, line) for line in state["ui_log"].splitlines()])
    state["rows"] = [row for row in state["rows"]
                     if not str(row.get("stage", "")).startswith("ui_log:")]
    for stage in stages:
        state["rows"].append({"stage": "ui_log:" + stage["stage"],
                              "host_elapsed_s": round(final_host - created_s + stage["uptime_s"], 3),
                              "board_uptime_s": stage["uptime_s"]})
    state["rows"].sort(key=lambda row: float(row["host_elapsed_s"]))
    expected = ["ui_service_start", "profile_snapshot_ready", "boot_inputs_ready",
                "remote_relay_ready", "ui_process_spawned", "ui_ready"]
    uptime = float((state.get("probe") or {}).get("uptime_s") or 0.0)
    valid = ([stage["stage"] for stage in stages] == expected
             and all(stage["uptime_s"] >= 0 and (uptime <= 0 or stage["uptime_s"] <= uptime)
                     for stage in stages)
             and bool(stages)
             and abs(created_s - stages[-1]["uptime_s"]) <= 2.0)
    if require_complete and not valid:
        return "UI stage log is not bound to current boot uptime"
    if not stages:
        return "UI stage log contained no V5_BOOT_STAGE rows"
    return None


def fetch_logs_into_state(target: str, state: dict[str, Any]) -> None:
    fetched = fetch_boot_logs(target)
    state["log_fetch"] = fetched
    state["ui_log"] = str((fetched.get("ui") or {}).get("stdout") or "")
    state["runtime_log"] = str((fetched.get("runtime") or {}).get("stdout") or "")
    failures = []
    for name in ("ui", "runtime"):
        item = fetched.get(name) or {}
        if item.get("returncode") != 0 or not item.get("stdout"):
            failures.append("%s rc=%r stderr=%s" %
                            (name, item.get("returncode"), item.get("stderr") or ""))
    state["log_fetch_errors"] = failures


def summarize_results(results: list[dict[str, Any]]) -> dict[str, Any]:
    good = [item for item in results if item.get("ok")]
    totals = []
    cycles = []
    for item in results:
        rows = sorted(item.get("segments") or [], key=lambda row: float(row["host_elapsed_s"]))
        deltas = []
        previous = None
        for row in rows:
            current = float(row["host_elapsed_s"])
            if previous is not None and current >= previous[1]:
                deltas.append({"from": previous[0], "to": row["stage"],
                               "delta_s": round(current - previous[1], 3)})
            previous = (row["stage"], current)
        ui_rows = [row for row in rows if row["stage"] == "ui_ready"]
        if item.get("ok") and ui_rows:
            totals.append(float(ui_rows[-1]["host_elapsed_s"]))
        cycles.append({"cycle": item.get("cycle"), "ok": bool(item.get("ok")),
                       "longest_five": sorted(deltas, key=lambda row: row["delta_s"], reverse=True)[:5]})
    return {"cycle_count": len(results), "passed_count": len(good),
            "failed_count": len(results) - len(good),
            "total_s_median": round(statistics.median(totals), 3) if totals else None,
            "total_s_max": round(max(totals), 3) if totals else None,
            "cycles": cycles}


def _write_text_retry(path: Path, content: str, errors: list[str]) -> None:
    for attempt in (1, 2):
        try:
            path.write_text(content, encoding="utf-8")
            return
        except Exception as exc:
            if attempt == 2:
                errors.append("write %s: %s: %s" % (path.name, type(exc).__name__, exc))


def persist_cycle_evidence(cycle_dir: Path, state: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    previous_errors = list(state.get("evidence_write_errors") or [])
    try:
        cycle_dir.mkdir(parents=True, exist_ok=True)
    except Exception as exc:
        return ["create evidence directory: %s: %s" % (type(exc).__name__, exc)]
    base = float(state.get("t0") or state.get("capture_started") or 0.0)
    serial_lines = state.get("serial_lines") or []
    serial_text = "\n".join(
        "%.3f\t%s" % (float(stamp_s) - base, line)
        for stamp_s, line in serial_lines) + ("\n" if serial_lines else "")
    _write_text_retry(cycle_dir / "power_stdout.log",
                      str(state.get("power_stdout") or ""), errors)
    _write_text_retry(cycle_dir / "serial.log", serial_text, errors)
    _write_text_retry(cycle_dir / "v5_ui_relay.log",
                      str(state.get("ui_log") or ""), errors)
    _write_text_retry(cycle_dir / "boot_runtime.log",
                      str(state.get("runtime_log") or ""), errors)
    try:
        write_segments(cycle_dir / "segments.tsv", state.get("rows") or [])
    except Exception as exc:
        errors.append("write segments.tsv: %s: %s" % (type(exc).__name__, exc))
        try:
            write_segments(cycle_dir / "segments.tsv", state.get("rows") or [])
            errors.pop()
        except Exception as retry_exc:
            errors.append("retry segments.tsv: %s: %s" %
                          (type(retry_exc).__name__, retry_exc))
    result = {
        "cycle": state["cycle"],
        "ok": bool(state.get("ok")),
        "error": state.get("error"),
        "t0": "canonical_CH4_power_on" if state.get("t0") else None,
        "console_pre_t0_s": (round(float(state["t0"]) - float(state["capture_started"]), 3)
                              if state.get("t0") else None),
        "segments": state.get("rows") or [],
        "remote_info": state.get("remote_info"),
        "probe": state.get("probe"),
        "final": state.get("final"),
        "log_fetch": state.get("log_fetch"),
        "log_fetch_errors": state.get("log_fetch_errors") or [],
        "stage_parse_error": state.get("stage_parse_error"),
        "post_ready_ethercat_errors": state.get("post_ready_ethercat_errors") or [],
        "evidence_write_errors": previous_errors + errors,
    }
    try:
        result_text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    except Exception as exc:
        errors.append("serialize result.json: %s: %s" % (type(exc).__name__, exc))
    else:
        _write_text_retry(cycle_dir / "result.json", result_text, errors)
    return errors


def measure_cycle(index: int, out_dir: Path, relay_port: str, console_port: str,
                  target: str, host: str, timeout_s: float) -> dict[str, Any]:
    cycle_dir = out_dir / ("cycle-%02d" % index)
    state: dict[str, Any] = {
        "cycle": index,
        "ok": False,
        "error": None,
        "capture_started": time.monotonic(),
        "t0": 0.0,
        "serial_lines": [],
        "rows": [],
        "power_stdout": "",
        "ui_log": "",
        "runtime_log": "",
        "remote_info": None,
        "probe": None,
        "final": None,
        "ssh_seen": False,
        "log_fetch": None,
        "log_fetch_errors": [],
        "stage_parse_error": None,
        "post_ready_ethercat_errors": [],
        "evidence_write_errors": [],
    }
    stop = threading.Event()
    console_ready = threading.Event()
    console_errors: list[str] = []
    console_handles: list[Any] = []
    thread: threading.Thread | None = None
    thread_started = False
    caught: BaseException | None = None
    fatal: FatalMeasurementError | None = None
    try:
        cycle_dir.mkdir(parents=True, exist_ok=True)
        thread = threading.Thread(target=capture_console,
                                  args=(console_port, stop, console_ready,
                                        state["serial_lines"], console_errors,
                                        console_handles), daemon=True)
        thread.start()
        thread_started = True
        if not console_ready.wait(3.0) or console_errors:
            raise RuntimeError("serial console did not become ready: %r" % console_errors)
        power_json = cycle_dir / "power_cycle.json"
        power = run([sys.executable, str(POWER_TOOL), "--relay-port", relay_port,
                     "--boot-mode", "sd", "--skip-net-check", "--target", target,
                     "--json-out", str(power_json.resolve())], timeout=30.0)
        state["power_stdout"] = power.stdout
        if power.returncode != 0:
            raise RuntimeError("power cycle failed: " + power.stdout[-1000:])
        power_payload = json.loads(power_json.read_text(encoding="utf-8"))
        state["t0"] = float(power_payload.get("power_on_monotonic_s") or 0.0)
        if state["t0"] <= 0.0:
            raise RuntimeError("canonical power tool did not report CH4 power-on T0")
        state["rows"].append({"stage": "power_on", "host_elapsed_s": 0.0,
                              "board_uptime_s": ""})
        deadline = time.monotonic() + timeout_s
        ssh_seen = tcp_seen = http_seen = False
        while time.monotonic() < deadline:
            if not tcp_seen:
                try:
                    with socket.create_connection((host, 22), timeout=0.2):
                        tcp_seen = True
                        state["rows"].append({"stage": "tcp22_ready", "host_elapsed_s": round(time.monotonic() - state["t0"], 3), "board_uptime_s": ""})
                except OSError:
                    pass
            info = http_probe(host)
            info_observed = time.monotonic()
            if info is not None and not http_seen:
                http_seen = True
                state["rows"].append({"stage": "http18080_ready", "host_elapsed_s": round(info_observed - state["t0"], 3), "board_uptime_s": ""})
            if (info is not None and info.get("ui_ready") is True
                    and state["remote_info"] is None):
                ui_elapsed = round(info_observed - state["t0"], 3)
                state["remote_info"] = info
                state["rows"].append({"stage": "ui_ready", "host_elapsed_s": ui_elapsed,
                                      "board_uptime_s": ""})
            if not ssh_seen:
                ssh_light = run(["ssh", "-o", "ConnectTimeout=2", target, "true"], timeout=3.0)
                if ssh_light.returncode == 0:
                    state["rows"].append({"stage": "ssh_ready", "host_elapsed_s": round(time.monotonic() - state["t0"], 3), "board_uptime_s": ""})
                    ssh_seen = True
                    state["ssh_seen"] = True
            if state["remote_info"] is not None:
                ui_elapsed = next(float(row["host_elapsed_s"]) for row in state["rows"]
                                  if row["stage"] == "ui_ready")
                try:
                    probe, attempts = prove_terminal(target, state["remote_info"])
                except DeterministicProbeError as exc:
                    state["probe"] = exc.probe
                    raise
                state["probe"] = probe
                state["final"] = {"runtime": probe, "remote_info": state["remote_info"],
                                  "terminal_probe_attempts": attempts,
                                  "ui_ready_host_elapsed_s": ui_elapsed}
                break
            time.sleep(0.5)
        if state["final"] is None:
            raise RuntimeError("UI was not ready within %.1fs" % timeout_s)
        if console_errors or not state["serial_lines"]:
            raise RuntimeError("serial console capture invalid: errors=%r lines=%d" %
                               (console_errors, len(state["serial_lines"])))
        serial_text = "\n".join(line for _, line in state["serial_lines"])
        if "U-Boot" not in serial_text or "Starting kernel" not in serial_text:
            raise RuntimeError("serial console is not bound to a complete current power-on")
        if "LinuxCNC backend transport ready;" not in serial_text:
            raise RuntimeError("serial console has no current EtherCAT backend-ready boundary")
        # Keep capture running beyond the terminal probe. IgH reports a lost
        # cyclic datagram after its timeout, so an immediate stop would hide
        # a readiness-probe-induced WKC loss.
        time.sleep(2.0)
        fetch_logs_into_state(target, state)
        if state["log_fetch_errors"]:
            raise RuntimeError("boot log fetch failed: " + "; ".join(state["log_fetch_errors"]))
        stage_error = append_ui_stage_evidence(state, require_complete=True)
        state["stage_parse_error"] = stage_error
        if stage_error:
            raise RuntimeError(stage_error)
        # Log fetch is part of the acceptance workload. Keep serial capture
        # alive beyond it and reject any WKC/datagram loss it exposes.
        time.sleep(1.0)
        state["post_ready_ethercat_errors"] = post_ready_ethercat_errors(
            state["serial_lines"])
        if state["post_ready_ethercat_errors"]:
            raise RuntimeError("EtherCAT WKC/datagram loss occurred after backend ready")
        state["ok"] = True
    except BaseException as exc:
        caught = exc
        state["error"] = "%s: %s" % (type(exc).__name__, exc)
    finally:
        fetch_possible = (state.get("ssh_seen") or state.get("remote_info") is not None
                          or state.get("probe") is not None)
        if fetch_possible and state.get("log_fetch") is None:
            try:
                fetch_logs_into_state(target, state)
                state["stage_parse_error"] = append_ui_stage_evidence(
                    state, require_complete=False)
            except BaseException as exc:
                state["log_fetch_errors"] = list(state.get("log_fetch_errors") or []) + [
                    "fetch exception %s: %s" % (type(exc).__name__, exc)]
        elif not fetch_possible and state.get("log_fetch") is None:
            state["log_fetch_errors"] = ["log fetch skipped: SSH never became ready"]
        stop.set()
        if thread is not None and thread_started:
            thread.join(2.0)
            if thread.is_alive():
                for handle in list(console_handles):
                    try:
                        handle.close()
                    except Exception as exc:
                        console_errors.append("forced console close: %s: %s" %
                                              (type(exc).__name__, exc))
                thread.join(2.0)
            if thread.is_alive():
                state["ok"] = False
                fatal = FatalMeasurementError("serial capture thread remained alive after forced close")
                if not state.get("error"):
                    state["error"] = "%s: %s" % (type(fatal).__name__, fatal)
        write_errors = persist_cycle_evidence(cycle_dir, state)
        if write_errors:
            state["ok"] = False
            state["evidence_write_errors"] = list(state.get("evidence_write_errors") or []) + write_errors
            fatal = FatalMeasurementError("persistent evidence write failure: " +
                                          "; ".join(write_errors))
            if not state.get("error"):
                state["error"] = "%s: %s" % (type(fatal).__name__, fatal)
            persist_cycle_evidence(cycle_dir, state)
    if fatal is not None:
        raise fatal
    if caught is not None:
        raise caught
    if not state["ok"]:
        raise RuntimeError(state.get("error") or "cycle evidence persistence failed")
    return json.loads((cycle_dir / "result.json").read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--relay-port", default="COM3")
    parser.add_argument("--console-port", default="COM4")
    parser.add_argument("--target", default="8ax-board")
    parser.add_argument("--host", default="192.168.1.221")
    parser.add_argument("--timeout-s", type=float, default=90.0)
    parser.add_argument("--lock-owner", required=True)
    args = parser.parse_args()
    if args.cycles <= 0 or args.cycles > 5:
        raise SystemExit("--cycles must be within 1..5")
    require_resource_lock(args.lock_owner)
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for i in range(args.cycles):
        try:
            results.append(measure_cycle(i + 1, args.out, args.relay_port, args.console_port,
                                         args.target, args.host, args.timeout_s))
        except FatalMeasurementError as exc:
            error = {"cycle": i + 1, "ok": False,
                     "error": "%s: %s" % (type(exc).__name__, exc)}
            results.append(error)
            break
        except Exception as exc:
            error = {"cycle": i + 1, "ok": False,
                     "error": "%s: %s" % (type(exc).__name__, exc)}
            results.append(error)
            cycle_result = args.out / ("cycle-%02d" % (i + 1)) / "result.json"
            if not cycle_result.exists():
                cycle_result.parent.mkdir(parents=True, exist_ok=True)
                cycle_result.write_text(json.dumps(error, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out / "summary.json").write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out / "statistics.json").write_text(
        json.dumps(summarize_results(results), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if all(item.get("ok") for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
