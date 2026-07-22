from __future__ import annotations

import binascii
import ctypes
import errno
import math
import os
import select
import struct
import time
import uuid
from pathlib import Path

from v5_status_shm_reader import status_shm_payload_valid


class ReadyError(RuntimeError):
    pass


BUS_INI = "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"
POSITION_PATH, WCS_PATH, MODAL_PATH, STATE_PATH = map(Path, (
    "/dev/shm/v5_native_position_status.bin",
    "/dev/shm/v5_native_wcs_status.bin",
    "/dev/shm/v5_native_modal_tool_status.bin",
    "/dev/shm/v3_status_shm",
))
BUS_STATUS_PATH = Path("/dev/shm/v5_native_bus_status.bin")
POSITION_PID, POSITION_LOCK, WCS_PID, WCS_LOCK, STATE_PID, STATE_LOCK = map(Path, (
    "/run/8ax/v5_position_status_publisher.pid",
    "/run/8ax/v5_position_status_publisher.lock",
    "/run/8ax/v5_wcs_status_publisher.pid",
    "/run/8ax/v5_wcs_status_publisher.lock",
    "/run/8ax/v5_state_publisher.pid",
    "/run/8ax/v5_state_publisher.lock",
))
POSITION_FMT, WCS_FMT, MODAL_FMT = (
    struct.Struct("<IIIIIIIIQQ" + "d" * 20 + "5B3x" + "d" * 4 + "II"),
    struct.Struct("<IIIIiIIIIIQ" + "d" * 45 + "II"),
    struct.Struct("<IIIIIIIIIiIIQ128sdIIIiIiIIi128sIII"),
)
POSITION_SEQ_OFFSET = 24
STATE_FRAME_SIZE, STATE_PAYLOAD_SIZE, STATE_SEQ_OFFSET = 7128, 7096, 24
STATE_TRAJECTORY_COUNT_OFFSET = 888
STATE_SCENE_POINT_COUNT_OFFSET = 1056
STATE_SCENE_SEGMENT_COUNT_OFFSET = 1060
STATE_SCENE_MARKER_COUNT_OFFSET = 1064
KERNEL_BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
PUBLISHER_OWNER_NAMES = ("position", "wcs", "state")


def fnv32(raw):
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def proc_start_ticks(pid): return Path(f"/proc/{pid}/stat").read_text(encoding="ascii").rsplit(")", 1)[1].split()[19]


def read_kernel_boot_id(path=KERNEL_BOOT_ID_PATH):
    raw = path.read_text(encoding="ascii").strip().lower()
    try:
        parsed = uuid.UUID(raw)
    except (ValueError, AttributeError) as exc:
        raise ReadyError(f"kernel boot_id invalid path={path} value={raw!r}") from exc
    if str(parsed) != raw:
        raise ReadyError(f"kernel boot_id is not canonical path={path} value={raw!r}")
    return raw


def proc_argv(pid):
    return [part.decode("utf-8", "strict") for part in
            Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0") if part]


def cpu_lists_are_cpu1(values): return bool(values) and all(value == "1" for value in values)


def require_cpu1(pid):
    values = []
    for task in Path(f"/proc/{pid}/task").glob("[0-9]*"):
        lines = (task / "status").read_text(encoding="ascii", errors="replace").splitlines()
        values.append(next((line.split(":", 1)[1].strip() for line in lines
                            if line.startswith("Cpus_allowed_list:")), ""))
    if not cpu_lists_are_cpu1(values):
        raise ReadyError(f"publisher threads are not confined to CPU1 pid={pid}")


def accept_identity(name, pid, expected_start, argv, allowed, cache, binary=False, extra=()):
    del binary
    current = (pid, proc_start_ticks(pid), tuple(argv)) + tuple(extra)
    if cache.get(name) not in (None, current):
        raise ReadyError(f"{name} owner changed during barrier")
    if current[1] != expected_start:
        raise ReadyError(f"{name} owner PID/start mismatch")
    if argv not in allowed:
        if name in cache:
            raise ReadyError(f"{name} owner argv changed during barrier")
        return False
    require_cpu1(pid)
    cache[name] = current
    return True


def read_record(path, count):
    try:
        fields = path.read_text(encoding="ascii").split()
    except FileNotFoundError:
        return None
    if len(fields) != count:
        raise ReadyError(f"publisher PID record malformed path={path}")
    return fields


def require_lifecycle_lock_held(path, name, flock_fn=None):
    with path.open("r") as stream:
        if flock_fn is None:
            flock_fn = ctypes.CDLL(None, use_errno=True).flock; flock_fn.argtypes = (ctypes.c_int, ctypes.c_int); flock_fn.restype = ctypes.c_int
        ctypes.set_errno(0); result = flock_fn(stream.fileno(), 2 | 4); error = ctypes.get_errno()
        if result == -1 and error in (errno.EACCES, errno.EAGAIN, errno.EWOULDBLOCK): return
        if result == 0: flock_fn(stream.fileno(), 8); raise ReadyError(f"{name} owner lock is not held")
        raise ReadyError(f"{name} owner lock probe failed errno={error}")


def publisher_identities(cache=None, unique=False, expected_ini=""):
    del unique
    cache, owners = ({} if cache is None else cache), {}
    record, lock_record = read_record(POSITION_PID, 3), read_record(POSITION_LOCK, 3)
    if record is not None:
        if record != lock_record:
            raise ReadyError("position owner record mismatch")
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_position_status_publisher", "--path",
                    str(POSITION_PATH), "--bus-path", str(BUS_STATUS_PATH),
                    "--interval-ms", "33"]
        if accept_identity("position", pid, record[1], proc_argv(pid),
                           [expected], cache, extra=(record[2],)):
            require_lifecycle_lock_held(POSITION_LOCK, "position")
            owners["position"] = pid
    record, lock_record = read_record(WCS_PID, 2), read_record(WCS_LOCK, 2)
    if record is not None:
        if record != lock_record:
            raise ReadyError("wcs owner record mismatch")
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_wcs_status_publisher.py", "--path", str(WCS_PATH),
                    "--modal-tool-path", str(MODAL_PATH), "--operator-error-path",
                    "/dev/shm/v5_native_operator_error_status.bin", "--operator-error-map",
                    "/opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv", "--ini", expected_ini,
                    "--interval-ms", "200"]
        if accept_identity("wcs", pid, record[1], proc_argv(pid)[1:],
                           [expected], cache):
            require_lifecycle_lock_held(WCS_LOCK, "wcs")
            owners["wcs"] = pid
    record, lock_record = read_record(STATE_PID, 2), read_record(STATE_LOCK, 2)
    if record is not None:
        if record != lock_record:
            raise ReadyError("state owner record mismatch")
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_state_publisher", "--path", str(STATE_PATH),
                    "--interval-ms", "33"]
        if accept_identity("state", pid, record[1], proc_argv(pid),
                           [expected], cache, True):
            require_lifecycle_lock_held(STATE_LOCK, "state")
            owners["state"] = pid
    return owners


def identity_tokens(cache):
    tokens = {}
    for name in PUBLISHER_OWNER_NAMES:
        raw = cache.get(name)
        if raw is None or len(raw) < 3:
            continue
        token = {
            "pid": int(raw[0]),
            "start_ticks": int(raw[1]),
            "argv": list(raw[2]),
        }
        if name == "position":
            if len(raw) != 4:
                raise ReadyError("Position owner identity token malformed")
            token["writer_identity"] = int(raw[3])
        elif len(raw) != 3:
            raise ReadyError(f"{name} owner identity token malformed")
        tokens[name] = token
    return tokens


def identity_cache_from_tokens(tokens):
    if not isinstance(tokens, dict) or set(tokens) != set(PUBLISHER_OWNER_NAMES):
        raise ReadyError("pre-UI publisher identity set incomplete")
    cache = {}
    for name in PUBLISHER_OWNER_NAMES:
        token = tokens.get(name)
        if not isinstance(token, dict) or not isinstance(token.get("argv"), list):
            raise ReadyError(f"pre-UI {name} identity malformed")
        try:
            pid = int(token.get("pid") or 0)
            start_ticks = int(token.get("start_ticks") or 0)
        except (TypeError, ValueError) as exc:
            raise ReadyError(f"pre-UI {name} identity malformed") from exc
        argv = tuple(str(value) for value in token["argv"])
        if pid <= 0 or start_ticks <= 0 or not argv:
            raise ReadyError(f"pre-UI {name} identity invalid")
        current = (pid, str(start_ticks), argv)
        if name == "position":
            try:
                writer_identity = int(token.get("writer_identity") or 0)
            except (TypeError, ValueError) as exc:
                raise ReadyError("pre-UI Position writer identity malformed") from exc
            if writer_identity <= 0:
                raise ReadyError("pre-UI Position writer identity invalid")
            current += (str(writer_identity),)
        cache[name] = current
    return cache


def read_atomic(path, fmt, crc_index=-2, seq_offset=None):
    raw = path.read_bytes()
    if len(raw) != fmt.size:
        raise ReadyError(f"block size mismatch path={path} actual={len(raw)} expected={fmt.size}")
    values = fmt.unpack(raw); tail = 12 if crc_index == -3 else 8
    if seq_offset is not None:
        sequence = struct.unpack_from("<I", raw, seq_offset)[0]
        if not sequence or sequence & 1:
            raise ReadyError(f"block sequence invalid path={path}")
    if values[crc_index] != fnv32(raw[:-tail]):
        raise ReadyError(f"block CRC mismatch path={path}")
    return values


def read_state_seqlock(path):
    try:
        fd = os.open(
            str(path),
            os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) |
            getattr(os, "O_BINARY", 0))
    except FileNotFoundError:
        return None
    def pread(size, offset):
        if hasattr(os, "pread"):
            return os.pread(fd, size, offset)
        current = os.lseek(fd, 0, os.SEEK_CUR)
        os.lseek(fd, offset, os.SEEK_SET)
        data = os.read(fd, size)
        os.lseek(fd, current, os.SEEK_SET)
        return data
    try:
        for _ in range(3):
            before = pread(4, STATE_SEQ_OFFSET)
            raw = pread(STATE_FRAME_SIZE, 0)
            after = pread(4, STATE_SEQ_OFFSET)
            if (len(before) != 4 or len(after) != 4 or
                    len(raw) != STATE_FRAME_SIZE):
                continue
            before, after = struct.unpack("<I", before)[0], struct.unpack("<I", after)[0]
            local = struct.unpack_from("<I", raw, 24)[0]
            if before and not before & 1 and before == local == after:
                return raw
        return None
    finally:
        os.close(fd)


def current_pre_ui_inputs(cache=None, unique=False, expected_ini=""):
    owners = publisher_identities(cache, unique, expected_ini)
    markers = {"position": None, "state": None, "wcs": None, "modal": None}
    position_writer_identity = None
    position_source_time = None
    position_source_generation = None
    try:
        pos = read_atomic(POSITION_PATH, POSITION_FMT, seq_offset=POSITION_SEQ_OFFSET)
        writer = int(POSITION_PID.read_text(encoding="ascii").split()[2])
        if pos[:3] != (0x56504F53, 3, POSITION_FMT.size) or pos[4] != 5 or pos[5] != writer:
            raise ReadyError("Position block owner/header mismatch")
        if (pos[8] <= 0 or pos[9] <= 0 or
                not all(math.isfinite(value) for value in pos[10:30]) or
                not all(value > 0.0 for value in pos[20:25]) or
                tuple(pos[30:35]) != (3, 3, 3, 3, 3)):
            raise ReadyError("Position block values are non-finite")
        if pos[3] & 3 == 3:
            if not 0 <= time.monotonic_ns() - pos[8] <= 1_000_000_000:
                raise ReadyError("Position block stale")
            markers["position"] = pos[9]
            position_writer_identity = pos[5]
            position_source_time = pos[8]
            position_source_generation = pos[9]
    except FileNotFoundError:
        pass
    raw = read_state_seqlock(STATE_PATH)
    if raw is not None:
        header = struct.unpack_from("<8I", raw)
        calc = binascii.crc32(raw[:24]); calc = binascii.crc32(raw[32:], calc) & 0xFFFFFFFF
        epoch, valid, typed = struct.unpack_from("<QII", raw, 32)
        writer_identity = struct.unpack_from("<I", raw, 48)[0]
        source_time, source_generation, scene_generation = struct.unpack_from("<QQQ", raw, 56)
        scene_build = struct.unpack_from("<Q", raw, 1024)[0]
        scene_flags = struct.unpack_from("<I", raw, 1052)[0]
        values = struct.unpack_from("<10d", raw, 80)
        unit_per_count = struct.unpack_from("<5d", raw, 160)
        following_error = struct.unpack_from("<5d", raw, 200)
        display_digits = struct.unpack_from("<5B", raw, 240)
        trajectory_count = struct.unpack_from(
            "<I", raw, STATE_TRAJECTORY_COUNT_OFFSET)[0]
        scene_point_count = struct.unpack_from(
            "<I", raw, STATE_SCENE_POINT_COUNT_OFFSET)[0]
        scene_segment_count = struct.unpack_from(
            "<I", raw, STATE_SCENE_SEGMENT_COUNT_OFFSET)[0]
        scene_marker_count = struct.unpack_from(
            "<I", raw, STATE_SCENE_MARKER_COUNT_OFFSET)[0]
        if header[:5] != (0x56355348, 3, STATE_FRAME_SIZE, STATE_FRAME_SIZE, STATE_PAYLOAD_SIZE) or header[7] != calc:
            raise ReadyError("State ABI/SeqLock/CRC mismatch")
        if not status_shm_payload_valid(raw):
            raise ReadyError("State typed payload contract invalid")
        if (trajectory_count > 16 or scene_point_count > 512 or
                scene_segment_count > 48 or scene_marker_count > 16):
            raise ReadyError("State trajectory/scene counts invalid")
        if (not writer_identity or not source_time or not source_generation or
                not scene_generation or not scene_build or not (scene_flags & 1) or
                not all(math.isfinite(value) for value in values) or
                not all(math.isfinite(value) and value > 0.0 for value in unit_per_count) or
                not all(math.isfinite(value) for value in following_error) or
                display_digits != (3, 3, 3, 3, 3)):
            raise ReadyError("State coordinate/trajectory/scene values invalid")
        lineage_valid = (
            position_writer_identity is not None and
            writer_identity == position_writer_identity and
            0 < source_generation <= position_source_generation and
            0 < source_time <= position_source_time)
        if (valid & 0x203 == 0x203 and not (typed & 7 or header[5] & 7) and
                lineage_valid):
            if not 0 <= time.monotonic_ns() - source_time <= 500_000_000:
                raise ReadyError("State block stale")
            markers["state"] = source_generation
    try:
        wcs = read_atomic(WCS_PATH, WCS_FMT)
        if wcs[:3] != (0x56574353, 2, 416) or wcs[5:7] != (9, 5):
            raise ReadyError("WCS header/count mismatch")
        if wcs[3] == 1 and wcs[7] == 1:
            if not 0 <= wcs[4] <= 8 or wcs[8] <= 0 or not all(math.isfinite(v) for v in wcs[11:56]):
                raise ReadyError("WCS table/epoch/value mismatch")
            if not 0 <= time.monotonic_ns() - wcs[10] <= 1_000_000_000:
                raise ReadyError("WCS block stale")
            markers["wcs"] = wcs[10]
    except FileNotFoundError:
        pass
    try:
        modal = read_atomic(MODAL_PATH, MODAL_FMT, -3)
        if modal[:3] != (0x564D544C, 4, 368):
            raise ReadyError("modal/tool block header mismatch")
        if modal[3] == 1 and modal[4:7] == (1, 1, 1):
            modal_text = modal[13].split(b"\0", 1)[0]
            if modal[9] < 0 or not modal_text or not math.isfinite(modal[14]): raise ReadyError("modal/tool actual invalid")
            if not 0 <= time.monotonic_ns() - modal[12] <= 1_000_000_000: raise ReadyError("modal/tool block stale")
            markers["modal"] = modal[12]
    except FileNotFoundError:
        pass
    return markers, owners


class ShmEventWatcher:
    MASK, OVERFLOW = 0x00000100 | 0x00000080 | 0x00000008, 0x00004000
    NAMES = {path.name for path in (POSITION_PATH, STATE_PATH, WCS_PATH, MODAL_PATH)}
    def __init__(self, directory="/dev/shm", names=None):
        libc = ctypes.CDLL(None, use_errno=True)
        self.fd = libc.inotify_init1(os.O_CLOEXEC | os.O_NONBLOCK)
        if self.fd < 0 or libc.inotify_add_watch(self.fd, os.fsencode(directory), self.MASK) < 0:
            raise ReadyError("cannot register /dev/shm inotify barrier")
        self.names = self.NAMES if names is None else set(names)
        self.poller = select.poll(); self.poller.register(self.fd, select.POLLIN); self.pidfds = {}
    def watch_pids(self, pids):
        libc = ctypes.CDLL(None, use_errno=True)
        for pid in pids:
            if pid in self.pidfds:
                continue
            fd = libc.syscall(434, pid, 0)
            if fd < 0:
                raise ReadyError(f"pidfd_open failed pid={pid}")
            self.pidfds[pid] = fd
            self.poller.register(fd, select.POLLIN | select.POLLHUP)
    def wait(self, timeout_ms):
        deadline = time.monotonic() + timeout_ms / 1000.0
        while True:
            events = self.poller.poll(max(0, int((deadline - time.monotonic()) * 1000)))
            if not events:
                raise ReadyError("pre-UI actual barrier timeout")
            for fd, _ in events:
                if fd != self.fd:
                    pid = next(pid for pid, pidfd in self.pidfds.items() if pidfd == fd)
                    raise ReadyError(f"publisher exited before UI barrier pid={pid}")
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                data = b""
            offset = 0
            while offset + 16 <= len(data):
                _, mask, _, length = struct.unpack_from("iIII", data, offset)
                name = data[offset + 16:offset + 16 + length].split(b"\0", 1)[0].decode("utf-8", "replace")
                offset += 16 + length
                if mask & self.OVERFLOW:
                    raise ReadyError("pre-UI inotify queue overflow")
                if mask & self.MASK and name in self.names:
                    return
    def close(self):
        for fd in self.pidfds.values():
            os.close(fd)
        os.close(self.fd)


def wait_pre_ui_inputs(timeout, checker=current_pre_ui_inputs,
                       watcher_factory=ShmEventWatcher, expected_ini=""):
    if checker is current_pre_ui_inputs and expected_ini != BUS_INI: raise ReadyError("pre-UI inputs require the canonical BUS INI")
    watcher, deadline, baseline, identities = watcher_factory(), time.monotonic() + timeout, None, {}
    try:
        while True:
            try:
                markers, owners = (checker(identities, False, expected_ini)
                                   if checker is current_pre_ui_inputs else checker())
            except ReadyError as exc:
                transient = str(exc).startswith((
                    "block size mismatch", "block sequence invalid", "block CRC mismatch",
                    "Position block owner/header mismatch", "Position block values are non-finite",
                    "Position block stale", "State ABI/SeqLock/CRC mismatch",
                    "State typed payload contract invalid",
                    "State coordinate/trajectory values invalid", "State block stale",
                    "State coordinate/trajectory/scene values invalid",
                    "WCS header/count mismatch", "WCS table/epoch/value mismatch", "WCS block stale",
                    "modal/tool block header mismatch", "modal/tool actual invalid", "modal/tool block stale",
                ))
                if not transient:
                    raise
                baseline = None
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ReadyError(f"pre-UI actual barrier timeout last={exc}") from exc
                watcher.wait(max(1, int(remaining * 1000)))
                continue
            watcher.watch_pids(owners.values())
            all_valid = len(owners) == 3 and all(markers.values())
            if all_valid and baseline is None:
                baseline = dict(markers)
            ready = (baseline is not None and all_valid and
                     all(markers[name] > baseline[name] for name in markers))
            if ready and checker is current_pre_ui_inputs:
                markers, owners = checker(identities, True, expected_ini)
                ready = len(owners) == 3 and all(markers.values()) and all(markers[name] > baseline[name] for name in markers)
            if ready:
                return {
                    "owners": owners,
                    "markers": markers,
                    "owner_identities": identity_tokens(identities),
                }
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ReadyError("pre-UI actual barrier timeout")
            watcher.wait(max(1, int(remaining * 1000)))
    finally:
        watcher.close()
