#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import textwrap

DEFAULT_BOARD_TARGET = "root@192.168.1.221"

REMOTE_CHECK = r'''
from pathlib import Path
import json
import re
import sys
import time
import urllib.request
import pwd

PIDFILES = {
    "v5_command_gate": "/run/8ax/v5_command_gate.pid",
    "v5_state_publisher": "/run/8ax/v5_state_publisher.pid",
    "v5_rtcp_status_publisher": "/run/8ax/v5_rtcp_status_publisher.pid",
    "v5_wcs_status_publisher": "/run/8ax/v5_wcs_status_publisher.pid",
    "v5_ui_relay": "/run/8ax/v5_ui_relay.pid",
    "v5_lvgl_shell": "/run/8ax/v5_ui_shell.pid",
    "v5_settings_actiond": "/run/8ax/v5_settings_actiond.pid",
    "v5_touch_diagnostics": "/run/8ax/v5_touch_diagnostics.pid",
}

SANDBOX_AUDIT_PATHS = (
    ("framebuffer", "/dev/fb0"),
    ("input_dir", "/dev/input"),
    ("status_shm", "/dev/shm/v3_status_shm"),
    ("product_run_dir", "/run/8ax_v5_product_ui"),
    ("command_gate_socket", "/run/8ax_v5_product_ui/v5_command_gate.sock"),
    ("settings_actiond_socket", "/run/8ax_v5_product_ui/settings_actiond.sock"),
)

def cpu_list_contains_zero(text: str) -> bool:
    for part in text.strip().split(","):
        if not part:
            continue
        if "-" in part:
            start, end = part.split("-", 1)
            try:
                if int(start) <= 0 <= int(end):
                    return True
            except ValueError:
                return True
        else:
            try:
                if int(part) == 0:
                    return True
            except ValueError:
                return True
    return False

def read_pid(path: str):
    try:
        text = Path(path).read_text(encoding="ascii").strip()
        pid = int(text)
        if not Path(f"/proc/{pid}").exists():
            return None
        return pid
    except Exception:
        return None

def read_status_field(pid: int, field: str) -> str:
    for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return ""

def read_sched_policy(pid: int) -> int:
    try:
        for line in Path(f"/proc/{pid}/sched").read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.strip().startswith("policy"):
                return int(line.split(":", 1)[1].strip())
    except Exception:
        pass
    try:
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8", errors="ignore").split()
        if len(stat) >= 41:
            return int(stat[40])
    except Exception:
        pass
    return -1

def read_uid(pid: int):
    uid_text = read_status_field(pid, "Uid").split()
    if not uid_text:
        return None
    try:
        return int(uid_text[0])
    except ValueError:
        return None

def read_environ(pid: int):
    try:
        raw = Path(f"/proc/{pid}/environ").read_bytes()
    except Exception:
        return {}
    env = {}
    for item in raw.split(b"\0"):
        if not item or b"=" not in item:
            continue
        key, value = item.split(b"=", 1)
        try:
            env[key.decode("utf-8", errors="ignore")] = value.decode("utf-8", errors="ignore")
        except Exception:
            pass
    return env

def uid_name(uid: int) -> str:
    try:
        return pwd.getpwuid(uid).pw_name
    except Exception:
        return str(uid)

def path_kind(path: Path) -> str:
    try:
        if path.is_socket():
            return "socket"
        if path.is_char_device():
            return "char"
        if path.is_dir():
            return "dir"
        if path.is_file():
            return "file"
    except Exception:
        pass
    return "other"

def audit_path(label: str, path_text: str):
    path = Path(path_text)
    if not path.exists():
        print(f"WARN_SANDBOX_PATH_MISSING {label}: {path_text}")
        return
    try:
        st = path.stat()
        mode = oct(st.st_mode & 0o777)
        print(
            f"AUDIT_SANDBOX_PATH {label}: path={path_text} type={path_kind(path)} "
            f"owner={uid_name(st.st_uid)}:{st.st_gid} mode={mode}"
        )
    except Exception as exc:
        print(f"WARN_SANDBOX_PATH_UNREADABLE {label}: {path_text}: {type(exc).__name__}")

def audit_sandbox_paths():
    for label, path in SANDBOX_AUDIT_PATHS:
        audit_path(label, path)
    input_dir = Path("/dev/input")
    if input_dir.exists():
        events = sorted(input_dir.glob("event*"))
        if events:
            for event in events[:16]:
                audit_path("input_event", str(event))
        else:
            print("WARN_SANDBOX_PATH_MISSING input_event: /dev/input/event*")

def parse_proc_tcp_listeners(port: int):
    listeners = []
    for path in (Path("/proc/net/tcp"), Path("/proc/net/tcp6")):
        try:
            lines = path.read_text(encoding="ascii", errors="ignore").splitlines()[1:]
        except Exception:
            continue
        for line in lines:
            fields = line.split()
            if len(fields) < 4:
                continue
            local = fields[1]
            state = fields[3]
            if state != "0A" or ":" not in local:
                continue
            local_addr, local_port = local.rsplit(":", 1)
            try:
                parsed_port = int(local_port, 16)
            except ValueError:
                continue
            if parsed_port == port:
                listeners.append((path.name, local_addr))
    return listeners

def read_remote_diagnostics():
    with urllib.request.urlopen("http://127.0.0.1:18080/remote/diagnostics", timeout=2.0) as response:
        return json.loads(response.read().decode("utf-8"))

def metric_delta(first: dict, second: dict, name: str) -> int:
    try:
        return int(second.get(name, 0)) - int(first.get(name, 0))
    except Exception:
        return 0

def cpu_busy_delta(first: dict, second: dict, name: str):
    a = (first.get("cpu_samples") or {}).get(name)
    b = (second.get("cpu_samples") or {}).get(name)
    if not isinstance(a, dict) or not isinstance(b, dict):
        return None
    try:
        total_delta = int(b["total"]) - int(a["total"])
        idle_delta = int(b["idle"]) - int(a["idle"])
    except Exception:
        return None
    if total_delta <= 0 or idle_delta < 0:
        return None
    return round(max(0.0, min(100.0, (1.0 - (idle_delta / total_delta)) * 100.0)), 1)

def process_tick_delta(first: dict, second: dict):
    try:
        a = int((first.get("process") or {}).get("cpu_ticks") or 0)
        b = int((second.get("process") or {}).get("cpu_ticks") or 0)
    except Exception:
        return None
    return max(0, b - a)

def thread_tick_deltas(first: dict, second: dict):
    first_threads = {
        int(item.get("tid")): item
        for item in (first.get("process") or {}).get("threads", [])
        if isinstance(item, dict) and item.get("tid") is not None
    }
    deltas = []
    for item in (second.get("process") or {}).get("threads", []):
        if not isinstance(item, dict) or item.get("tid") is None:
            continue
        try:
            tid = int(item.get("tid"))
            now_ticks = int(item.get("cpu_ticks") or 0)
            previous_ticks = int((first_threads.get(tid) or {}).get("cpu_ticks") or 0)
        except Exception:
            continue
        deltas.append((max(0, now_ticks - previous_ticks), tid, item.get("comm", ""), item.get("cpus_allowed_list", "")))
    return sorted(deltas, reverse=True)

def audit_remote_relay(pid: int) -> int:
    rc = 0
    env = read_environ(pid)
    bind = env.get("V5_UI_REMOTE_BIND", "")
    allow = env.get("V5_UI_REMOTE_ALLOW_CIDRS", "")
    print(f"AUDIT_REMOTE_RELAY_ENV pid={pid} bind={bind or '<missing>'} allow_cidrs={allow or '<missing>'}")
    if not allow:
        print(f"FAIL v5_ui_relay: pid={pid} missing V5_UI_REMOTE_ALLOW_CIDRS", file=sys.stderr)
        rc = 1
    listeners = parse_proc_tcp_listeners(18080)
    if not listeners:
        print("WARN_REMOTE_RELAY_PORT: no tcp listener on 18080")
        return rc
    for table, local_addr in listeners:
        print(f"AUDIT_REMOTE_RELAY_LISTENER table={table} local_addr_hex={local_addr} port=18080")
    try:
        first_diagnostics = read_remote_diagnostics()
        time.sleep(1.0)
        diagnostics = read_remote_diagnostics()
        metrics = diagnostics.get("metrics")
    except Exception as exc:
        print(f"FAIL v5_ui_relay: diagnostics unavailable: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    required_metrics = (
        "full_frame_requests",
        "stream_sessions",
        "stream_active_sessions",
        "stream_initial_full_frames",
        "stream_repair_full_frames",
        "stream_repair_missing_dirty_events",
        "stream_idle_pings",
        "stream_disconnects",
        "stream_send_failures",
        "dirty_events",
        "dirty_coalesced_events",
        "dirty_rect_frames",
        "dirty_payload_bytes",
        "dirty_payload_rows",
        "dirty_payload_rects",
        "dirty_payload_contiguous_frames",
        "dirty_payload_union_frames",
        "dirty_payload_union_source_rects",
        "dirty_large_frames",
        "dirty_large_pixels",
        "dirty_large_throttle_sleeps",
        "dirty_large_throttle_ms",
        "framebuffer_mmap_refreshes",
        "input_sessions",
        "input_active_sessions",
        "input_disconnects",
        "input_messages",
        "input_accepted",
        "input_rejected",
    )
    if not isinstance(metrics, dict):
        print("FAIL v5_ui_relay: diagnostics metrics missing", file=sys.stderr)
        return 1
    missing = [name for name in required_metrics if name not in metrics]
    if missing:
        print(f"FAIL v5_ui_relay: diagnostics metrics missing keys={','.join(missing)}", file=sys.stderr)
        return 1
    first_metrics = first_diagnostics.get("metrics") or {}
    if not isinstance(first_metrics, dict):
        first_metrics = {}
    for key in ("cpu_samples", "process"):
        if key not in diagnostics:
            print(f"FAIL v5_ui_relay: diagnostics missing {key}", file=sys.stderr)
            rc = 1
    deltas = {name: metric_delta(first_metrics, metrics, name) for name in required_metrics}
    print(
        "AUDIT_REMOTE_RELAY_METRICS "
        f"full_frame_requests={metrics.get('full_frame_requests')} "
        f"stream_sessions={metrics.get('stream_sessions')} "
        f"stream_active_sessions={metrics.get('stream_active_sessions')} "
        f"dirty_rect_frames={metrics.get('dirty_rect_frames')} "
        f"dirty_payload_bytes={metrics.get('dirty_payload_bytes')} "
        f"stream_repair_full_frames={metrics.get('stream_repair_full_frames')}"
    )
    print(
        "AUDIT_REMOTE_RELAY_WINDOW seconds=1.0 "
        f"cpu0_busy_delta={cpu_busy_delta(first_diagnostics, diagnostics, 'cpu0')} "
        f"cpu1_busy_delta={cpu_busy_delta(first_diagnostics, diagnostics, 'cpu1')} "
        f"relay_cpu_ticks_delta={process_tick_delta(first_diagnostics, diagnostics)} "
        f"dirty_events_delta={deltas.get('dirty_events')} "
        f"dirty_rect_frames_delta={deltas.get('dirty_rect_frames')} "
        f"dirty_payload_bytes_delta={deltas.get('dirty_payload_bytes')} "
        f"full_frame_requests_delta={deltas.get('full_frame_requests')} "
        f"stream_repair_full_frames_delta={deltas.get('stream_repair_full_frames')} "
        f"stream_active_sessions={metrics.get('stream_active_sessions')}"
    )
    for ticks, tid, comm, cpus in thread_tick_deltas(first_diagnostics, diagnostics)[:8]:
        print(f"AUDIT_REMOTE_RELAY_THREAD_WINDOW tid={tid} comm={comm} cpu_ticks_delta={ticks} Cpus_allowed_list={cpus}")
    return rc

def cpu_list_is_exact_zero(text: str) -> bool:
    return text.strip() == "0"

def rtapi_pids():
    pids = []
    for path in Path("/proc").glob("[0-9]*"):
        try:
            comm = (path / "comm").read_text(encoding="utf-8", errors="ignore").strip()
        except Exception:
            continue
        if comm == "rtapi_app":
            try:
                pids.append(int(path.name))
            except ValueError:
                pass
    return sorted(pids)

def audit_linuxcnc_rtapi_affinity() -> int:
    pids = rtapi_pids()
    if not pids:
        print("FAIL linuxcnc_rtapi_affinity: rtapi_app not running", file=sys.stderr)
        return 1
    rc = 0
    for pid in pids:
        task_dir = Path(f"/proc/{pid}/task")
        for task in sorted(task_dir.glob("[0-9]*")):
            try:
                tid = int(task.name)
            except ValueError:
                continue
            cpus = ""
            try:
                for line in (task / "status").read_text(encoding="utf-8", errors="ignore").splitlines():
                    if line.startswith("Cpus_allowed_list:"):
                        cpus = line.split(":", 1)[1].strip()
                        break
            except Exception:
                pass
            try:
                comm = (task / "comm").read_text(encoding="utf-8", errors="ignore").strip()
            except Exception:
                comm = "<unknown>"
            if not cpu_list_is_exact_zero(cpus):
                print(
                    f"FAIL linuxcnc_rtapi_affinity: pid={pid} tid={tid} comm={comm} "
                    f"Cpus_allowed_list={cpus} expected=0",
                    file=sys.stderr,
                )
                rc = 1
            else:
                print(f"OK_LINUXCNC_RTAPI_CPU0 pid={pid} tid={tid} comm={comm} Cpus_allowed_list={cpus}")
    return rc

rc = 0
for name, pidfile in PIDFILES.items():
    pid = read_pid(pidfile)
    if pid is None:
        print(f"SKIP {name}: no live pid")
        continue
    service_rc = 0
    cpus = read_status_field(pid, "Cpus_allowed_list")
    policy = read_sched_policy(pid)
    uid = read_uid(pid)
    if cpu_list_contains_zero(cpus):
        print(f"FAIL {name}: pid={pid} Cpus_allowed_list={cpus} contains CPU0", file=sys.stderr)
        service_rc = 1
    if policy != 0:
        print(f"FAIL {name}: pid={pid} sched_policy={policy} expected SCHED_OTHER(0)", file=sys.stderr)
        service_rc = 1
    if uid == 0:
        print(f"AUDIT_SANDBOX_ROOT {name}: pid={pid} uid=0(root) allowlist_required_before_drop")
    elif uid is not None:
        print(f"OK_SANDBOX_USER {name}: pid={pid} user={uid_name(uid)} uid={uid}")
    cap_eff = read_status_field(pid, "CapEff")
    if cap_eff:
        print(f"AUDIT_SANDBOX_CAPS {name}: pid={pid} CapEff={cap_eff}")
    if name == "v5_ui_relay":
        service_rc |= audit_remote_relay(pid)
    if service_rc == 0:
        print(f"OK {name}: pid={pid} Cpus_allowed_list={cpus} sched_policy={policy}")
    rc |= service_rc
rc |= audit_linuxcnc_rtapi_affinity()
audit_sandbox_paths()
sys.exit(rc)
'''


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit live v5 board runtime process policy.")
    parser.add_argument("--board-target", default=os.environ.get("V5_BOARD_SSH", DEFAULT_BOARD_TARGET))
    parser.add_argument("--board-port", default=os.environ.get("V5_BOARD_SSH_PORT", "22"))
    args = parser.parse_args()
    board = args.board_target
    port = args.board_port
    proc = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=5", "-p", port, board, "python3", "-"],
        input=REMOTE_CHECK,
        text=True,
        capture_output=True,
        check=False,
    )
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
