#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import textwrap

DEFAULT_BOARD_TARGET = "root@192.168.1.221"

REMOTE_CHECK = r'''
from __future__ import annotations

from pathlib import Path
import json
import math
import os
import re
import stat
import struct
import subprocess
import sys
import time
import urllib.request
import pwd
import grp

PIDFILES = {
    "v5_command_gate": "/run/8ax/v5_command_gate.pid",
    "v5_state_publisher": "/run/8ax/v5_state_publisher.pid",
    "v5_position_status_publisher": "/run/8ax/v5_position_status_publisher.pid",
    "v5_wcs_status_publisher": "/run/8ax/v5_wcs_status_publisher.pid",
    "v5_ui_relay": "/run/8ax/v5_ui_relay.pid",
    "v5_lvgl_shell": "/run/8ax/v5_ui_shell.pid",
    "v5_settings_actiond": "/run/8ax/v5_settings_actiond.pid",
    "v5_touch_diagnostics": "/run/8ax/v5_touch_diagnostics.pid",
}

PROC_ROOT = "/proc"
PROC_LOCKS_PATH = "/proc/locks"
CMDLINE_PATH = "/proc/cmdline"
ISOLATED_CPU_PATH = "/sys/devices/system/cpu/isolated"
POSITION_LOCK_PATH = "/run/8ax/v5_position_status_publisher.lock"
POSITION_BLOCK_PATH = "/dev/shm/v5_native_position_status.bin"
POSITION_DAEMON_PATH = "/usr/libexec/8ax/v5_position_status_publisher"
SETTINGS_ACTIOND_DAEMON_PATH = "/usr/libexec/8ax/drive_profile/v5_settings_actiond.py"
DEV_ROOT = "/dev"
SYS_CLASS_UIO_ROOT = "/sys/class/uio"
TCF_ROOT = "/"
TCF_EXECUTABLE_NAME = "tcf-agent"
TCF_LISTENER_PORT = 1534
TCF_ABSENCE_PATHS = (
    "usr/bin/tcf-agent",
    "usr/sbin/tcf-agent",
)
TCF_ROOTFS_MANIFEST_PATHS = ("boot/v5-rootfs-file-manifest.tsv",)
RUNTIME_STARTUP_INIT = Path("/etc/init.d/v5-runtime-startup")
BACKEND_READINESS_PROBE = Path("/usr/libexec/8ax/v5_backend_readiness_probe")
PRODUCT_BOOT_SERVICES = (
    "v5-linuxcnc-command-gate",
    "v5-position-status-publisher",
    "v5-wcs-status-publisher",
    "v5-state-publisher",
    "v5-settings-actiond",
    "v5-ui-relay",
)

EXPECTED_NICE = {
    "v5_command_gate": -5,
    "v5_state_publisher": 10,
    "v5_position_status_publisher": 0,
    "v5_wcs_status_publisher": 10,
    "v5_lvgl_shell": 0,
}

LINUXCNC_NON_REALTIME_NAMES = (
    "linuxcncsvr",
    "milltask",
    "io",
    "linuxcncrsh",
    "v5_native_hal_owner",
)

SANDBOX_AUDIT_PATHS = (
    ("framebuffer", "/dev/fb0"),
    ("input_dir", "/dev/input"),
    ("status_shm", "/dev/shm/v3_status_shm"),
    ("product_run_dir", "/run/8ax_v5_product_ui"),
    ("command_gate_socket", "/run/8ax_v5_product_ui/v5_command_gate.sock"),
    ("native_hal_owner_socket", "/run/8ax_v5_product_ui/v5_native_hal_owner.sock"),
    ("settings_actiond_socket", "/run/8ax_v5_product_ui/settings_actiond.sock"),
)

def audit_runtime_startup_boot_graph() -> int:
    failures = []
    for path in (RUNTIME_STARTUP_INIT, BACKEND_READINESS_PROBE):
        if not path.is_file() or not os.access(path, os.X_OK):
            failures.append(f"FAIL_RUNTIME_STARTUP_EXECUTABLE:{path}")
    expected_links = tuple(
        (Path(f"/etc/rc{level}.d/S05v5-runtime-startup"), "../init.d/v5-runtime-startup")
        for level in (2, 3, 4, 5)
    ) + tuple(
        (Path(f"/etc/rc{level}.d/K14v5-runtime-startup"), "../init.d/v5-runtime-startup")
        for level in (0, 1, 6)
    )
    for path, expected_target in expected_links:
        if not path.is_symlink():
            failures.append(f"FAIL_RUNTIME_STARTUP_LINK_MISSING:{path}")
            continue
        try:
            target = os.readlink(path)
        except OSError as exc:
            failures.append(f"FAIL_RUNTIME_STARTUP_LINK_READ:{path}:{exc}")
            continue
        if target != expected_target:
            failures.append(
                f"FAIL_RUNTIME_STARTUP_LINK_TARGET:{path}:{target}:{expected_target}"
            )
    for level in range(7):
        root = Path(f"/etc/rc{level}.d")
        if not root.is_dir():
            continue
        for service in PRODUCT_BOOT_SERVICES:
            for pattern in (f"S??{service}", f"K??{service}"):
                for survivor in root.glob(pattern):
                    failures.append(f"FAIL_RUNTIME_SHADOW_BOOT_LINK:{survivor}")
    for failure in failures:
        print(failure, file=sys.stderr)
    if not failures:
        print("OK_RUNTIME_STARTUP_EVENT_DAG links=7 shadow_product_links=0")
    return int(bool(failures))

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
        pid = int(text.split()[0])
        if not (Path(PROC_ROOT) / str(pid)).exists():
            return None
        return pid
    except Exception:
        return None

def read_position_start_ticks(pid: int) -> int:
    text = (Path(PROC_ROOT) / str(pid) / "stat").read_text(encoding="ascii")
    closing = text.rfind(")")
    tail = text[closing + 2 :].split() if closing >= 0 else []
    if len(tail) <= 19 or not tail[19].isdigit():
        raise RuntimeError("proc_stat")
    return int(tail[19])

def read_position_argv(pid: int) -> list[str]:
    raw = (Path(PROC_ROOT) / str(pid) / "cmdline").read_bytes()
    argv = [item.decode("utf-8", "replace") for item in raw.split(b"\0") if item]
    if not argv:
        raise RuntimeError("empty_argv")
    return argv

def read_position_owner_record(pidfile: str) -> tuple[int, int, int]:
    try:
        fields = Path(pidfile).read_text(encoding="ascii").split()
    except OSError as exc:
        raise RuntimeError("pidfile_missing") from exc
    if len(fields) != 3 or any(re.fullmatch(r"[0-9]+", field) is None for field in fields):
        raise RuntimeError("pidfile_fields")
    pid, start_ticks, writer_identity = (int(field) for field in fields)
    if not (0 < pid <= 0x7fffffff and 0 < start_ticks <= 0xffffffffffffffff and
            0 < writer_identity <= 0xffffffff):
        raise RuntimeError("pidfile_range")
    if not (Path(PROC_ROOT) / str(pid)).is_dir():
        raise RuntimeError("dead_pid")
    if read_position_start_ticks(pid) != start_ticks:
        raise RuntimeError("start_ticks")
    return pid, start_ticks, writer_identity

def position_lock_owner_pid() -> int:
    lock_stat = os.stat(POSITION_LOCK_PATH)
    expected = (os.major(lock_stat.st_dev), os.minor(lock_stat.st_dev), lock_stat.st_ino)
    matches = []
    for line in Path(PROC_LOCKS_PATH).read_text(encoding="ascii").splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[1] == "->":
            parts = parts[:1] + parts[2:]
        if len(parts) < 6:
            continue
        dev_inode = parts[5].split(":")
        try:
            identity = (int(dev_inode[0], 16), int(dev_inode[1], 16), int(dev_inode[2], 10))
            owner_pid = int(parts[4], 10)
        except (IndexError, ValueError):
            continue
        if identity == expected:
            matches.append((parts[1], parts[3], owner_pid))
    if len(matches) != 1:
        raise RuntimeError("lock_record_count")
    lock_kind, lock_mode, owner_pid = matches[0]
    if lock_kind != "FLOCK" or lock_mode != "WRITE":
        raise RuntimeError("lock_mode")
    return owner_pid

def position_identity_audit(pid: int, pidfile: str, owner_record=None) -> int:
    try:
        record = owner_record or read_position_owner_record(pidfile)
        owner_pid, start_ticks, writer_identity = record
        if owner_pid != pid:
            raise RuntimeError("pidfile_pid")
        if POSITION_DAEMON_PATH not in read_position_argv(pid):
            raise RuntimeError("canonical_cmdline")
        matches = 0
        for proc in Path(PROC_ROOT).glob("[0-9]*/cmdline"):
            try:
                argv = [item.decode("utf-8", "replace") for item in proc.read_bytes().split(b"\0") if item]
            except OSError:
                continue
            if POSITION_DAEMON_PATH in argv:
                matches += 1
        if matches != 1:
            raise RuntimeError("unique_process")
        fields = [str(owner_pid), str(start_ticks), str(writer_identity)]
        if Path(POSITION_LOCK_PATH).read_text(encoding="ascii").split() != fields:
            raise RuntimeError("lock_record")
        if position_lock_owner_pid() != owner_pid:
            raise RuntimeError("lock_owner")
        payload = Path(POSITION_BLOCK_PATH).read_bytes()
        if len(payload) != 256:
            raise RuntimeError("block_size")
        magic, version, size, _mask, _axes, block_writer, sequence, _reserved = (
            struct.unpack_from("<8I", payload, 0))
        source_time, source_generation = struct.unpack_from("<QQ", payload, 32)
        if ((magic, version, size, block_writer) !=
                (0x56504F53, 3, 256, writer_identity) or
                sequence == 0 or sequence & 1 or
                source_time == 0 or source_generation == 0):
            raise RuntimeError("block_identity")
        unit_per_count = struct.unpack_from("<5d", payload, 128)
        following_error = struct.unpack_from("<5d", payload, 168)
        display_digits = struct.unpack_from("<5B", payload, 208)
        if (not all(math.isfinite(value) and value > 0.0
                    for value in unit_per_count) or
                not all(math.isfinite(value) for value in following_error) or
                display_digits != (3, 3, 3, 3, 3)):
            raise RuntimeError("display_metadata")
        expected_crc = struct.unpack_from("<I", payload, 248)[0]
        actual_crc = 2166136261
        for byte in payload[:248]:
            actual_crc = ((actual_crc ^ byte) * 16777619) & 0xffffffff
        if actual_crc != expected_crc:
            raise RuntimeError("block_crc")
    except Exception as exc:
        print(f"FAIL v5_position_status_publisher: identity={exc}", file=sys.stderr)
        return 1
    print(
        f"OK_POSITION_IDENTITY pid={pid} start_ticks={start_ticks} "
        f"writer_identity={writer_identity}")
    return 0

def position_service_audit(pidfile: str):
    try:
        record = read_position_owner_record(pidfile)
    except Exception as exc:
        print(f"FAIL v5_position_status_publisher: owner_record={exc}", file=sys.stderr)
        return None, 1
    pid = record[0]
    return pid, position_identity_audit(pid, pidfile, record)

def read_status_field(pid: int, field: str) -> str:
    for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return ""

def read_sched_policy(pid: int) -> int:
    try:
        stat_line = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8", errors="ignore")
        tail = stat_line[stat_line.rfind(")") + 2 :].split()
        if len(tail) >= 39:
            return int(tail[38])
    except Exception:
        pass
    return -1

def read_rt_priority(pid: int) -> int:
    try:
        stat_line = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8", errors="ignore")
        tail = stat_line[stat_line.rfind(")") + 2 :].split()
        if len(tail) >= 38:
            return int(tail[37])
    except Exception:
        pass
    return -1

def read_task_records(pid: int) -> list[tuple[int, str, int, int]]:
    records = []
    for task in sorted(Path(f"/proc/{pid}/task").glob("[0-9]*")):
        try:
            tid = int(task.name)
            stat = (task / "stat").read_text(encoding="utf-8", errors="ignore")
            tail = stat[stat.rfind(")") + 2 :].split()
            comm = (task / "comm").read_text(encoding="utf-8", errors="ignore").strip()
            records.append((tid, comm, int(tail[16]), read_sched_policy(tid)))
        except Exception:
            records.append((-1, "<unreadable>", 999, -1))
    return records

def read_task_nice_values(pid: int) -> list[int]:
    return [record[2] for record in read_task_records(pid)]

def pids_by_executable_name(name: str) -> list[int]:
    pids = []
    for proc in Path(PROC_ROOT).glob("[0-9]*"):
        try:
            argv0 = (proc / "cmdline").read_bytes().split(b"\0", 1)[0].decode("utf-8", errors="ignore")
        except Exception:
            continue
        if Path(argv0).name == name:
            pids.append(int(proc.name))
    return sorted(pids)

def pids_by_exact_argv_element(path: str) -> list[int]:
    pids = []
    for proc in Path(PROC_ROOT).glob("[0-9]*"):
        try:
            argv = [item.decode("utf-8", errors="ignore")
                    for item in (proc / "cmdline").read_bytes().split(b"\0") if item]
        except Exception:
            continue
        if path in argv:
            pids.append(int(proc.name))
    return sorted(pids)

def read_uid(pid: int):
    uid_text = read_status_field(pid, "Uid").split()
    if not uid_text:
        return None
    try:
        return int(uid_text[0])
    except ValueError:
        return None

def read_fs_ids(pid: int):
    status = Path(PROC_ROOT) / str(pid) / "status"
    values = {}
    for line in status.read_text(encoding="ascii", errors="ignore").splitlines():
        if line.startswith("Uid:") or line.startswith("Gid:"):
            fields = line.split()[1:]
            if len(fields) == 4 and all(field.isdigit() for field in fields):
                values[line[:3]] = tuple(int(field) for field in fields)
    if "Uid" not in values or "Gid" not in values:
        raise RuntimeError("fsids_missing")
    return values["Uid"][3], values["Gid"][3]

def validate_uio_consumer_fsids(records, petalinux_uid: int, petalinux_gid: int):
    failures = []
    expected = {
        "stepgen": ((petalinux_uid, petalinux_gid), "FAIL_UIO_STEPGEN_CONSUMER_MISSING"),
        "dna": ((0, 0), "FAIL_UIO_DNA_CONSUMER_MISSING"),
    }
    for role, (expected_ids, missing_marker) in expected.items():
        if role not in records:
            failures.append(missing_marker)
        elif tuple(records[role]) != expected_ids:
            ids = records[role]
            failures.append(f"FAIL_UIO_{role.upper()}_CONSUMER_FSIDS:{ids}")
    return failures

def validate_uio_records(records, petalinux_gid: int):
    failures = []
    by_role = {record.get("role"): record for record in records if record.get("role") != "unexpected"}
    expected = {
        "stepgen": {"name": "stepgen", "mode": 0o660, "uid": 0, "gid": petalinux_gid},
        "dna": {"name": "dna", "mode": 0o600, "uid": 0, "gid": 0},
    }
    for role, contract in expected.items():
        record = by_role.get(role)
        if record is None:
            failures.append(f"FAIL_UIO_{role.upper()}_MISSING")
            continue
        if not record.get("is_symlink"):
            failures.append(f"FAIL_UIO_{role.upper()}_SYMLINK")
        if not record.get("is_char"):
            failures.append(f"FAIL_UIO_{role.upper()}_CHAR")
        if contract["name"] not in str(record.get("name", "")).lower():
            failures.append(f"FAIL_UIO_{role.upper()}_NAME:{record.get('name', '')}")
        for key in ("mode", "uid", "gid"):
            if record.get(key) != contract[key]:
                failures.append(f"FAIL_UIO_{role.upper()}_{key.upper()}:{record.get(key)}")
    if all(role in by_role for role in expected) and by_role["stepgen"].get("target") == by_role["dna"].get("target"):
        failures.append("FAIL_UIO_TARGET_ALIAS")
    for record in records:
        if record.get("role") == "unexpected" and int(record.get("mode", 0)) & 0o022:
            failures.append(f"FAIL_UIO_UNEXPECTED_WRITABLE:{record.get('target', '')}")
    return failures

def collect_uio_record(role: str, link_path: Path):
    record = {"role": role, "target": "", "is_symlink": link_path.is_symlink(),
              "is_char": False, "name": "", "mode": None, "uid": None, "gid": None}
    try:
        target = link_path.resolve(strict=True)
        info = target.stat()
        record.update({"target": str(target), "is_char": stat.S_ISCHR(info.st_mode),
                       "mode": stat.S_IMODE(info.st_mode), "uid": info.st_uid, "gid": info.st_gid})
        record["name"] = (Path(SYS_CLASS_UIO_ROOT) / target.name / "name").read_text(
            encoding="ascii", errors="ignore").strip()
    except OSError:
        pass
    return record

def audit_uio_devices() -> int:
    try:
        petalinux = pwd.getpwnam("petalinux")
        petalinux_gid = grp.getgrnam("petalinux").gr_gid
    except Exception as exc:
        print(f"FAIL_UIO_PETALINUX_IDENTITY:{exc}", file=sys.stderr)
        return 1
    dev_root = Path(DEV_ROOT)
    records = [
        collect_uio_record("stepgen", dev_root / "v5-stepgen-uio"),
        collect_uio_record("dna", dev_root / "v5-dna-uio"),
    ]
    expected_targets = {record["target"] for record in records if record["target"]}
    for target in dev_root.glob("uio[0-9]*"):
        try:
            info = target.stat()
        except OSError:
            continue
        if str(target.resolve()) not in expected_targets:
            records.append({"role": "unexpected", "target": str(target),
                            "mode": stat.S_IMODE(info.st_mode)})
    failures = validate_uio_records(records, petalinux_gid)
    consumer_ids = {}
    rtapi_pids = pids_by_executable_name("rtapi_app")
    if len(rtapi_pids) == 1:
        try:
            consumer_ids["stepgen"] = read_fs_ids(rtapi_pids[0])
        except Exception as exc:
            failures.append(f"FAIL_UIO_STEPGEN_CONSUMER_FSIDS:{exc}")
    elif len(rtapi_pids) != 1:
        failures.append(f"FAIL_UIO_STEPGEN_CONSUMER_COUNT:{len(rtapi_pids)}")
    actiond_pids = pids_by_exact_argv_element(SETTINGS_ACTIOND_DAEMON_PATH)
    actiond_pidfile_pid = read_pid(PIDFILES["v5_settings_actiond"])
    if len(actiond_pids) != 1:
        failures.append(f"FAIL_UIO_DNA_CONSUMER_COUNT:{len(actiond_pids)}")
    elif actiond_pidfile_pid != actiond_pids[0]:
        failures.append(
            f"FAIL_UIO_DNA_CONSUMER_PIDFILE_MISMATCH:{actiond_pidfile_pid}:{actiond_pids[0]}")
    else:
        try:
            consumer_ids["dna"] = read_fs_ids(actiond_pids[0])
        except Exception as exc:
            failures.append(f"FAIL_UIO_DNA_CONSUMER_FSIDS:{exc}")
    failures.extend(validate_uio_consumer_fsids(
        consumer_ids, petalinux.pw_uid, petalinux_gid))
    for failure in failures:
        print(failure, file=sys.stderr)
    if not failures:
        print("OK_UIO_DEVICE_PERMISSIONS")
    return int(bool(failures))

def validate_ethercat_records(records, petalinux_gid: int):
    failures = []
    if not records:
        return ["FAIL_ETHERCAT_DEVICE_MISSING"]
    for record in records:
        path = record.get("path", "")
        if not record.get("is_char"):
            failures.append(f"FAIL_ETHERCAT_DEVICE_CHAR:{path}")
        if record.get("uid") != 0:
            failures.append(f"FAIL_ETHERCAT_DEVICE_UID:{path}:{record.get('uid')}")
        if record.get("gid") != petalinux_gid:
            failures.append(f"FAIL_ETHERCAT_DEVICE_GID:{path}:{record.get('gid')}")
        if record.get("mode") != 0o660:
            failures.append(f"FAIL_ETHERCAT_DEVICE_MODE:{path}:{record.get('mode')}")
    return failures

def audit_ethercat_devices() -> int:
    try:
        petalinux_gid = grp.getgrnam("petalinux").gr_gid
    except Exception as exc:
        print(f"FAIL_ETHERCAT_PETALINUX_IDENTITY:{exc}", file=sys.stderr)
        return 1
    records = []
    for path in Path(DEV_ROOT).glob("EtherCAT*"):
        try:
            info = path.stat()
        except OSError:
            continue
        records.append({"path": str(path), "is_char": stat.S_ISCHR(info.st_mode),
                        "uid": info.st_uid, "gid": info.st_gid,
                        "mode": stat.S_IMODE(info.st_mode)})
    failures = validate_ethercat_records(records, petalinux_gid)
    for failure in failures:
        print(failure, file=sys.stderr)
    if not failures:
        print("OK_ETHERCAT_DEVICE_PERMISSIONS")
    return int(bool(failures))

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
    for path in (Path(PROC_ROOT) / "net" / "tcp", Path(PROC_ROOT) / "net" / "tcp6"):
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

def audit_linuxcnc_control_listeners() -> int:
    rc = 0
    listeners_5005 = parse_proc_tcp_listeners(5005)
    if listeners_5005:
        print(f"FAIL_LINUXCNC_NML_TCP_5005: {listeners_5005}", file=sys.stderr)
        rc = 1
    listeners_5007 = parse_proc_tcp_listeners(5007)
    if not listeners_5007:
        print("FAIL_LINUXCNCRSH_5007_MISSING", file=sys.stderr)
        return 1
    allowed = {
        ("tcp", "0100007F"),
        ("tcp6", "00000000000000000000000001000000"),
    }
    for listener in listeners_5007:
        if listener not in allowed:
            print(f"FAIL_LINUXCNCRSH_5007_NON_LOOPBACK: {listener}", file=sys.stderr)
            rc = 1
    if rc == 0:
        print(f"OK_LINUXCNCRSH_5007_LOOPBACK: {listeners_5007}")
    return rc

def collect_tcf_proc_evidence(proc_root: Path):
    failures = []
    listeners = []
    tcf_pids = []
    if not proc_root.is_dir():
        return [], [], ["FAIL_TCF_PROC_ROOT_EVIDENCE"]
    proc_dirs = list(proc_root.glob("[0-9]*"))
    if not proc_dirs:
        failures.append("FAIL_TCF_PROCESS_EVIDENCE")
    readable_processes = 0
    for proc in proc_dirs:
        try:
            argv = [item for item in (proc / "cmdline").read_bytes().split(b"\0") if item]
        except OSError:
            continue
        readable_processes += 1
        if argv and Path(argv[0].decode("utf-8", "ignore")).name == TCF_EXECUTABLE_NAME:
            tcf_pids.append(int(proc.name))
    if proc_dirs and readable_processes == 0:
        failures.append("FAIL_TCF_PROCESS_EVIDENCE")
    for table in ("tcp", "tcp6"):
        path = proc_root / "net" / table
        try:
            lines = path.read_text(encoding="ascii", errors="strict").splitlines()
        except (OSError, UnicodeError):
            failures.append(f"FAIL_TCF_PROC_{table.upper()}_EVIDENCE")
            continue
        if not lines:
            failures.append(f"FAIL_TCF_PROC_{table.upper()}_EVIDENCE")
            continue
        for line in lines[1:]:
            fields = line.split()
            if len(fields) < 4 or fields[3] != "0A" or ":" not in fields[1]:
                continue
            address, port_text = fields[1].rsplit(":", 1)
            try:
                port = int(port_text, 16)
            except ValueError:
                continue
            if port == TCF_LISTENER_PORT:
                listeners.append((table, address))
    return tcf_pids, listeners, failures

def audit_tcf_retirement() -> int:
    root = Path(TCF_ROOT)
    failures = []
    for relative in TCF_ABSENCE_PATHS:
        if (root / relative).exists():
            failures.append(f"FAIL_TCF_RUNTIME_FILE:{relative}")
    init_root = root / "etc"
    for entry in (init_root / "init.d").glob("*tcf-agent*"):
        failures.append(f"FAIL_TCF_INIT_ENTRY:{entry.relative_to(root)}")
    for rc_dir in init_root.glob("rc*.d"):
        for entry in rc_dir.glob("*tcf-agent*"):
            failures.append(f"FAIL_TCF_RC_LINK:{entry.relative_to(root)}")
    token = re.compile(r"(?<![A-Za-z0-9_-])tcf-agent(?![A-Za-z0-9_-])")
    readable_manifests = 0
    for relative in TCF_ROOTFS_MANIFEST_PATHS:
        path = root / relative
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError as exc:
            failures.append(f"FAIL_TCF_ROOTFS_MANIFEST_READ:{relative}:{exc}")
            continue
        readable_manifests += 1
        if token.search(text):
            failures.append(f"FAIL_TCF_ROOTFS_MANIFEST:{relative}")
    if readable_manifests == 0:
        failures.append("FAIL_TCF_ROOTFS_MANIFEST_EVIDENCE")
    pids, listeners, proc_failures = collect_tcf_proc_evidence(Path(PROC_ROOT))
    failures.extend(proc_failures)
    if pids:
        failures.append(f"FAIL_TCF_PROCESS:{pids}")
    if listeners:
        failures.append(f"FAIL_TCF_LISTENER_1534:{listeners}")
    for failure in failures:
        print(failure, file=sys.stderr)
    if not failures:
        print("OK_TCF_PRODUCTION_RETIRED")
    return int(bool(failures))

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

def audit_linuxcnc_privileged_helpers() -> int:
    rc = 0
    for helper in (Path("/usr/bin/rtapi_app"), Path("/usr/bin/linuxcnc_module_helper")):
        try:
            helper_stat = helper.stat()
        except OSError as exc:
            print(f"FAIL linuxcnc_privileged_helper: path={helper} error={exc}", file=sys.stderr)
            rc = 1
            continue
        mode = stat.S_IMODE(helper_stat.st_mode)
        if helper_stat.st_uid != 0 or helper_stat.st_gid != 0 or mode != 0o4755:
            print(
                f"FAIL linuxcnc_privileged_helper: path={helper} "
                f"uid={helper_stat.st_uid} gid={helper_stat.st_gid} mode={mode:o} expected=0:0:4755",
                file=sys.stderr,
            )
            rc = 1
        else:
            print(f"OK_LINUXCNC_PRIVILEGED_HELPER path={helper} owner=0:0 mode=4755")
    return rc

def audit_linuxcnc_rtapi_affinity() -> int:
    pids = rtapi_pids()
    if not pids:
        print("FAIL linuxcnc_rtapi_affinity: rtapi_app not running", file=sys.stderr)
        return 1
    rc = 0
    realtime_threads = 0
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
            policy = read_sched_policy(tid)
            if comm.startswith("rtapi_app:T"):
                realtime_threads += 1
                if policy not in (1, 2):
                    print(
                        f"FAIL linuxcnc_rtapi_scheduler: pid={pid} tid={tid} comm={comm} "
                        f"policy={policy} expected=SCHED_FIFO(1) or SCHED_RR(2)",
                        file=sys.stderr,
                    )
                    rc = 1
                else:
                    print(
                        f"OK_LINUXCNC_RTAPI_SCHEDULER pid={pid} tid={tid} comm={comm} policy={policy}"
                    )
            if not cpu_list_is_exact_zero(cpus):
                print(
                    f"FAIL linuxcnc_rtapi_affinity: pid={pid} tid={tid} comm={comm} "
                    f"Cpus_allowed_list={cpus} expected=0",
                    file=sys.stderr,
                )
                rc = 1
            else:
                print(f"OK_LINUXCNC_RTAPI_CPU0 pid={pid} tid={tid} comm={comm} Cpus_allowed_list={cpus}")
    if realtime_threads == 0:
        print("FAIL linuxcnc_rtapi_scheduler: no rtapi_app:T realtime thread found", file=sys.stderr)
        rc = 1
    return rc

def audit_linuxcnc_non_realtime_scheduling() -> int:
    rc = 0
    for name in LINUXCNC_NON_REALTIME_NAMES:
        pids = pids_by_executable_name(name)
        if not pids:
            print(f"FAIL linuxcnc_non_realtime: name={name} not running", file=sys.stderr)
            rc = 1
            continue
        for pid in pids:
            cpus = read_status_field(pid, "Cpus_allowed_list")
            task_records = read_task_records(pid)
            nice_values = [record[2] for record in task_records]
            if (
                cpus != "1"
                or not nice_values
                or any(value != -5 for value in nice_values)
                or any(record[3] != 0 for record in task_records)
            ):
                print(
                    f"FAIL linuxcnc_non_realtime: name={name} pid={pid} "
                    f"cpus={cpus} nice={nice_values} expected_cpu=1 expected_nice=-5",
                    file=sys.stderr,
                )
                rc = 1
            else:
                print(f"OK_LINUXCNC_NON_RT_CPU1 name={name} pid={pid} nice=-5")
    return rc

def audit_kernel_boot_cpu_layout() -> int:
    try:
        cmdline = Path(CMDLINE_PATH).read_text(
            encoding="ascii", errors="strict").strip().split()
        isolated = Path(ISOLATED_CPU_PATH).read_text(
            encoding="ascii", errors="strict").strip()
    except (OSError, UnicodeError) as exc:
        print(f"FAIL kernel_boot_cpu_layout: readback={exc}", file=sys.stderr)
        return 1
    isolcpus_tokens = [token for token in cmdline if token.startswith("isolcpus=")]
    if isolcpus_tokens:
        print(
            f"FAIL kernel_boot_cpu_layout: isolcpus_tokens={isolcpus_tokens} "
            "expected=[]",
            file=sys.stderr,
        )
        return 1
    if isolated:
        print(
            f"FAIL kernel_boot_cpu_layout: isolated={isolated} expected=<empty>",
            file=sys.stderr,
        )
        return 1
    print("OK_KERNEL_BOOT_CPU_LAYOUT isolcpus=absent isolated=empty")
    return 0


def normalize_cpu_mask(text: str) -> int:
    compact = text.strip().replace(",", "")
    return int(compact or "0", 16)

def audit_network_cpu_isolation() -> int:
    rc = 0
    interrupts = Path("/proc/interrupts").read_text(encoding="utf-8", errors="ignore").splitlines()
    for iface, expected_cpu, expected_rps in (("eth0", "1", 2), ("eth1", "0", 0)):
        irqs = []
        for line in interrupts:
            if iface not in line or ":" not in line:
                continue
            token = line.split(":", 1)[0].strip()
            if token.isdigit():
                irqs.append(token)
        if not irqs:
            print(f"FAIL network_cpu_isolation: iface={iface} irq=missing", file=sys.stderr)
            rc = 1
        for irq in irqs:
            affinity = Path(f"/proc/irq/{irq}/smp_affinity_list").read_text(encoding="ascii", errors="ignore").strip()
            if affinity != expected_cpu:
                print(
                    f"FAIL network_cpu_isolation: iface={iface} irq={irq} "
                    f"affinity={affinity} expected={expected_cpu}",
                    file=sys.stderr,
                )
                rc = 1
        rps_queues = list(Path(f"/sys/class/net/{iface}/queues").glob("rx-*/rps_cpus"))
        if not rps_queues:
            print(f"FAIL network_cpu_isolation: iface={iface} rps_queue=missing", file=sys.stderr)
            rc = 1
        for queue in rps_queues:
            try:
                actual = normalize_cpu_mask(queue.read_text(encoding="ascii", errors="ignore"))
            except (OSError, ValueError):
                print(f"FAIL network_cpu_isolation: path={queue} readback=unavailable", file=sys.stderr)
                rc = 1
                continue
            if actual != expected_rps:
                print(f"FAIL network_cpu_isolation: path={queue} actual={actual:x} expected={expected_rps:x}", file=sys.stderr)
                rc = 1
    if pids_by_executable_name("irqbalance"):
        print("FAIL network_cpu_isolation: irqbalance is active", file=sys.stderr)
        rc = 1
    softirq_records = []
    for comm in Path(PROC_ROOT).glob("[0-9]*/task/[0-9]*/comm"):
        try:
            if comm.read_text(encoding="ascii").strip() != "ksoftirqd/0":
                continue
            tid = int(comm.parent.name)
            softirq_records.append(
                (tid, read_status_field(tid, "Cpus_allowed_list"),
                 read_sched_policy(tid), read_rt_priority(tid)))
        except (OSError, ValueError):
            continue
    if len(softirq_records) != 1 or softirq_records[0][1:] != ("0", 1, 49):
        print(
            f"FAIL network_cpu_isolation: ksoftirqd0={softirq_records} "
            "expected=one CPU0/SCHED_FIFO/49 thread",
            file=sys.stderr,
        )
        rc = 1
    if rc == 0:
        print("OK_NETWORK_CPU_ISOLATION eth0=CPU1 eth1=CPU0 ksoftirqd0=FIFO49")
    return rc

def audit_management_daemon_cpu_isolation() -> int:
    rc = 0
    dropbear_pids = pids_by_executable_name("dropbear")
    if not dropbear_pids:
        print("FAIL management_daemon_cpu_isolation: dropbear=missing", file=sys.stderr)
        return 1
    for pid in dropbear_pids:
        try:
            cpus = read_status_field(pid, "Cpus_allowed_list")
            policy = read_sched_policy(pid)
            task_records = read_task_records(pid)
        except OSError:
            continue
        if cpus != "1" or policy != 0 or any(record[3] != 0 for record in task_records):
            print(
                f"FAIL management_daemon_cpu_isolation: name=dropbear pid={pid} "
                f"cpus={cpus} policy={policy} tasks={task_records} expected=CPU1/SCHED_OTHER",
                file=sys.stderr,
            )
            rc = 1
    if rc == 0:
        print(f"OK_MANAGEMENT_DAEMON_CPU_ISOLATION dropbear_pids={dropbear_pids} cpu=1")
    return rc

rc = 0
for name, pidfile in PIDFILES.items():
    if name == "v5_position_status_publisher":
        pid, service_rc = position_service_audit(pidfile)
        if pid is None:
            rc |= service_rc
            continue
    else:
        pid = read_pid(pidfile)
        if pid is None:
            print(f"SKIP {name}: no live pid")
            continue
        service_rc = 0
    cpus = read_status_field(pid, "Cpus_allowed_list")
    policy = read_sched_policy(pid)
    task_records = read_task_records(pid)
    nice_values = [record[2] for record in task_records]
    uid = read_uid(pid)
    if cpu_list_contains_zero(cpus):
        print(f"FAIL {name}: pid={pid} Cpus_allowed_list={cpus} contains CPU0", file=sys.stderr)
        service_rc = 1
    if policy != 0 or any(record[3] != 0 for record in task_records):
        print(
            f"FAIL {name}: pid={pid} sched_policy={policy} task_records={task_records} "
            f"expected SCHED_OTHER(0)",
            file=sys.stderr,
        )
        service_rc = 1
    expected_nice = EXPECTED_NICE.get(name)
    if expected_nice is not None and (not nice_values or any(value != expected_nice for value in nice_values)):
        print(
            f"FAIL {name}: pid={pid} nice={nice_values} expected={expected_nice}",
            file=sys.stderr,
        )
        service_rc = 1
    if name == "v5_ui_relay":
        frame_records = [record for record in task_records if record[2] == 10]
        input_records = [record for record in task_records if record[2] != 10]
        if (
            len(frame_records) != 1
            or frame_records[0][2] != 10
            or not input_records
            or any(record[2] != 0 for record in input_records)
        ):
            print(
                f"FAIL {name}: pid={pid} task_records={task_records} "
                f"expected exactly one frame-producer worker=10 and every input/server thread=0",
                file=sys.stderr,
            )
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
        print(f"OK {name}: pid={pid} Cpus_allowed_list={cpus} sched_policy={policy} nice={nice_values}")
    rc |= service_rc
rc |= audit_linuxcnc_privileged_helpers()
rc |= audit_linuxcnc_rtapi_affinity()
rc |= audit_linuxcnc_non_realtime_scheduling()
rc |= audit_kernel_boot_cpu_layout()
rc |= audit_network_cpu_isolation()
rc |= audit_management_daemon_cpu_isolation()
rc |= audit_linuxcnc_control_listeners()
rc |= audit_tcf_retirement()
rc |= audit_uio_devices()
rc |= audit_ethercat_devices()
rc |= audit_runtime_startup_boot_graph()
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
