#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import textwrap


REMOTE_CHECK = r'''
from pathlib import Path
import re
import sys
import pwd

PIDFILES = {
    "v5_command_gate": "/run/8ax/v5_command_gate.pid",
    "v5_state_publisher": "/run/8ax/v5_state_publisher.pid",
    "v5_rtcp_status_publisher": "/run/8ax/v5_rtcp_status_publisher.pid",
    "v5_wcs_status_publisher": "/run/8ax/v5_wcs_status_publisher.pid",
    "v5_ui_relay": "/run/8ax/v5_ui_relay.pid",
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
audit_sandbox_paths()
sys.exit(rc)
'''


def main() -> int:
    board = os.environ.get("V5_BOARD_SSH", "")
    port = os.environ.get("V5_BOARD_SSH_PORT", "22")
    if not board:
        print("V5_BOARD_SSH is required", file=sys.stderr)
        return 2
    remote = "python3 - <<'PY'\n" + REMOTE_CHECK + "\nPY\n"
    proc = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=5", "-p", port, board, remote],
        text=True,
        capture_output=True,
        check=False,
    )
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
