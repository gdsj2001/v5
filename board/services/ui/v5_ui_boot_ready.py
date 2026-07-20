#!/usr/bin/env python3
"""Validate the off-screen page queue and publish the one-shot UI ready gate."""
from __future__ import annotations
import argparse, binascii, ctypes, errno, json, math, os, re, select, struct, tempfile, time, urllib.error, urllib.request, uuid
from pathlib import Path
from v5_status_shm_reader import status_shm_payload_valid
from v5_ui_cache_queue_contract import validate_queue_trace
EXPECTED_QUEUE = (
    "main",
    "settings",
    "tool",
    "probe",
    "offset",
    "io",
    "network",
    "program",
    "mdi",
)
EXPECTED_CACHE_BUDGET_BYTES = 1024 * 600 * 4 * 13
CACHE_LINE_RE = re.compile(
    r"^V5_UI_CACHE_PREP page=(?P<page>[a-z]+) "
    r"completed=(?P<completed>[0-9]+) total=(?P<total>[0-9]+) "
    r"elapsed_us=(?P<elapsed>[0-9]+) "
    r"create_us=(?P<create>[0-9]+) prepare_us=(?P<prepare>[0-9]+) "
    r"yield_us=(?P<yield_us>[0-9]+) "
    r"cpu_pct_x100=(?P<cpu_pct_x100>[0-9]+) "
    r"peak_cpu_pct_x100=(?P<peak_cpu_pct_x100>[0-9]+) "
    r"worker_id=(?P<worker_id>[0-9]+) cache_valid=(?P<cache_valid>[01]) "
    r"invalidation_clean=(?P<invalidation_clean>[01]) "
    r"budget_bytes=(?P<budget>[0-9]+)$"
)
class ReadyError(RuntimeError): pass
class FinalBarrierError(ReadyError): pass
BUS_INI = "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"; POSITION_PATH, WCS_PATH, MODAL_PATH, STATE_PATH = map(Path, ("/dev/shm/v5_native_position_status.bin", "/dev/shm/v5_native_wcs_status.bin", "/dev/shm/v5_native_modal_tool_status.bin", "/dev/shm/v3_status_shm"))
BUS_STATUS_PATH = Path("/dev/shm/v5_native_bus_status.bin")
POSITION_PID, POSITION_LOCK, WCS_PID, STATE_PID = map(Path, ("/run/8ax/v5_position_status_publisher.pid", "/run/8ax/v5_position_status_publisher.lock", "/run/8ax/v5_wcs_status_publisher.pid", "/run/8ax/v5_state_publisher.pid"))
POSITION_FMT, WCS_FMT, MODAL_FMT = (
    struct.Struct("<IIIIIIIIQQ" + "d" * 20 + "5B3x" + "d" * 4 + "II"),
    struct.Struct("<IIIIiIIIIIQ" + "d" * 45 + "II"),
    struct.Struct("<IIIIIIIIIiIIQ128sdIIIiIiIIi128sIII"))
POSITION_SEQ_OFFSET = 24
STATE_FRAME_SIZE, STATE_PAYLOAD_SIZE, STATE_SEQ_OFFSET = 7128, 7096, 24
STATE_TRAJECTORY_COUNT_OFFSET = 888
STATE_SCENE_POINT_COUNT_OFFSET = 1056
STATE_SCENE_SEGMENT_COUNT_OFFSET = 1060
STATE_SCENE_MARKER_COUNT_OFFSET = 1064
KERNEL_BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
INPUT_BARRIER_SCHEMA = "v5.ui_input_barrier.v1"
FAILURE_SCHEMA = "v5.ui_failure.v1"
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
def exact_processes(expected, binary=False):
    matches = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            argv = proc_argv(int(entry.name))
        except (OSError, UnicodeError):
            continue
        if (argv == expected if binary else argv[1:] == expected):
            matches.append(int(entry.name))
    return matches
def accept_identity(name, pid, expected_start, argv, allowed, cache, unique, binary=False, extra=()):
    current = (pid, proc_start_ticks(pid), tuple(argv)) + tuple(extra)
    if cache.get(name) not in (None, current):
        raise ReadyError(f"{name} owner changed during barrier")
    if current[1] != expected_start:
        raise ReadyError(f"{name} owner PID/start mismatch")
    if argv not in allowed:
        if name in cache:
            raise ReadyError(f"{name} owner argv changed during barrier")
        return False
    matches = ([found for item in allowed for found in exact_processes(item, binary)]
               if unique or name not in cache else [pid])
    if matches != [pid]:
        if name not in cache and not matches:
            return False
        raise ReadyError(f"{name} unique owner mismatch")
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
def require_position_lock_held(path=POSITION_LOCK, flock_fn=None):
    with path.open("r") as stream:
        if flock_fn is None:
            flock_fn = ctypes.CDLL(None, use_errno=True).flock; flock_fn.argtypes = (ctypes.c_int, ctypes.c_int); flock_fn.restype = ctypes.c_int
        ctypes.set_errno(0); result = flock_fn(stream.fileno(), 2 | 4); error = ctypes.get_errno()
        if result == -1 and error in (errno.EACCES, errno.EAGAIN, errno.EWOULDBLOCK): return
        if result == 0: flock_fn(stream.fileno(), 8); raise ReadyError("position owner lock is not held")
        raise ReadyError(f"position owner lock probe failed errno={error}")
def publisher_identities(cache=None, unique=False, expected_ini=""):
    cache, owners = ({} if cache is None else cache), {}
    record, lock_record = read_record(POSITION_PID, 3), read_record(POSITION_LOCK, 3)
    if record is not None:
        if record != lock_record:
            raise ReadyError("position owner record mismatch")
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_position_status_publisher.py", "--path",
                    str(POSITION_PATH), "--bus-path", str(BUS_STATUS_PATH),
                    "--interval-ms", "33"]
        if accept_identity("position", pid, record[1], proc_argv(pid)[1:],
                           [expected], cache, unique, extra=(record[2],)):
            require_position_lock_held()
            owners["position"] = pid
    record = read_record(WCS_PID, 2)
    if record is not None:
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_wcs_status_publisher.py", "--path", str(WCS_PATH),
                    "--modal-tool-path", str(MODAL_PATH), "--operator-error-path",
                    "/dev/shm/v5_native_operator_error_status.bin", "--operator-error-map",
                    "/opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv", "--ini", expected_ini,
                    "--interval-ms", "200"]
        if accept_identity("wcs", pid, record[1], proc_argv(pid)[1:],
                           [expected], cache, unique):
            owners["wcs"] = pid
    record = read_record(STATE_PID, 2)
    if record is not None:
        pid = int(record[0])
        expected = ["/usr/libexec/8ax/v5_state_publisher", "--path", str(STATE_PATH),
                    "--interval-ms", "33"]
        if accept_identity("state", pid, record[1], proc_argv(pid),
                           [expected], cache, unique, True):
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
    position_values = None
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
            markers["position"], position_values = pos[9], tuple(pos[10:20])
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
        if (valid & 0x203 == 0x203 and not (typed & 7 or header[5] & 7) and
                position_values is not None and tuple(values) == position_values):
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
def build_input_barrier_snapshot(result, expected_ini):
    if expected_ini != BUS_INI:
        raise ReadyError("pre-UI snapshot requires the canonical BUS INI")
    if not isinstance(result, dict):
        raise ReadyError("pre-UI barrier result malformed")
    tokens = result.get("owner_identities")
    identity_cache_from_tokens(tokens)
    markers = result.get("markers")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise ReadyError("pre-UI publisher marker set incomplete")
    try:
        normalized_markers = {name: int(value or 0) for name, value in markers.items()}
    except (TypeError, ValueError) as exc:
        raise ReadyError("pre-UI publisher markers malformed") from exc
    if not all(normalized_markers.values()):
        raise ReadyError("pre-UI publisher markers unavailable")
    return {
        "schema": INPUT_BARRIER_SCHEMA,
        "boot_id": read_kernel_boot_id(),
        "expected_ini": expected_ini,
        "owner_identities": tokens,
        "markers": normalized_markers,
        "created_monotonic_ns": time.monotonic_ns(),
    }
def read_input_barrier_snapshot(path, expected_ini):
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError) as exc:
        raise ReadyError(f"cannot read pre-UI publisher snapshot path={path}") from exc
    if (not isinstance(payload, dict) or payload.get("schema") != INPUT_BARRIER_SCHEMA or
            payload.get("boot_id") != read_kernel_boot_id() or
            payload.get("expected_ini") != expected_ini):
        raise ReadyError("pre-UI publisher snapshot identity mismatch")
    identity_cache_from_tokens(payload.get("owner_identities"))
    markers = payload.get("markers")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise ReadyError("pre-UI publisher snapshot marker set incomplete")
    return payload
def validate_final_publisher_barrier(snapshot_path, expected_ini,
                                     checker=current_pre_ui_inputs):
    try:
        snapshot = read_input_barrier_snapshot(snapshot_path, expected_ini)
        identities = identity_cache_from_tokens(snapshot["owner_identities"])
    except ReadyError as exc:
        raise FinalBarrierError(str(exc)) from exc
    try:
        markers, owners = (checker(identities, True, expected_ini)
                           if checker is current_pre_ui_inputs else checker(identities))
    except ReadyError as exc:
        if any(token in str(exc) for token in (
                "owner changed", "PID/start mismatch", "unique owner mismatch",
                "owner argv changed", "owner record mismatch")):
            raise FinalBarrierError(str(exc)) from exc
        raise
    if set(owners) != set(PUBLISHER_OWNER_NAMES):
        raise FinalBarrierError("final publisher owner set incomplete")
    if not isinstance(markers, dict) or set(markers) != {"position", "state", "wcs", "modal"}:
        raise FinalBarrierError("final publisher marker set incomplete")
    try:
        final_markers = {name: int(value or 0) for name, value in markers.items()}
        baseline_markers = {name: int(value or 0) for name, value in snapshot["markers"].items()}
    except (TypeError, ValueError) as exc:
        raise ReadyError("final publisher markers malformed") from exc
    if (not all(final_markers.values()) or
            any(final_markers[name] < baseline_markers[name] for name in final_markers)):
        raise FinalBarrierError("final publisher generation/freshness regressed")
    return {
        "owner_identities": identity_tokens(identities),
        "baseline_markers": baseline_markers,
        "final_markers": final_markers,
    }
def parse_cache_rows(text: str) -> list[dict]:
    rows: list[dict] = []
    for line in text.splitlines():
        match = CACHE_LINE_RE.match(line.strip())
        if not match:
            continue
        rows.append({
            "page": match.group("page"),
            "completed": int(match.group("completed")),
            "total": int(match.group("total")),
            "elapsed_us": int(match.group("elapsed")),
            "create_us": int(match.group("create")),
            "prepare_us": int(match.group("prepare")),
            "yield_us": int(match.group("yield_us")),
            "cpu_pct_x100": int(match.group("cpu_pct_x100")),
            "peak_cpu_pct_x100": int(match.group("peak_cpu_pct_x100")),
            "worker_id": int(match.group("worker_id")),
            "cache_valid": int(match.group("cache_valid")),
            "invalidation_clean": int(match.group("invalidation_clean")),
            "budget_bytes": int(match.group("budget")),
        })
    return rows
def validate_cache_rows(rows: list[dict]) -> None:
    pages = tuple(row["page"] for row in rows)
    observed_peak = 0
    if pages != EXPECTED_QUEUE:
        completed = len(rows)
        failure_page = EXPECTED_QUEUE[completed] if completed < len(EXPECTED_QUEUE) else "queue_duplicate_or_extra"
        raise ReadyError(
            f"cache queue mismatch pages={pages!r} expected={EXPECTED_QUEUE!r} failure_page={failure_page}"
        )
    for index, row in enumerate(rows, 1):
        if row["completed"] != index or row["total"] != len(EXPECTED_QUEUE):
            raise ReadyError(f"cache queue counters invalid page={row['page']} row={row!r}")
        if row["elapsed_us"] <= 0:
            raise ReadyError(f"cache page elapsed time missing page={row['page']}")
        if row["create_us"] + row["prepare_us"] + row["yield_us"] > row["elapsed_us"]:
            raise ReadyError(f"cache page timing counters exceed elapsed page={row['page']} row={row!r}")
        if row["yield_us"] <= 0 or row["worker_id"] != 0:
            raise ReadyError(f"cache page was not serialized on worker 0 page={row['page']} row={row!r}")
        if row["cache_valid"] != 1 or row["invalidation_clean"] != 1:
            raise ReadyError(f"cache page retained dirty/incomplete state page={row['page']} row={row!r}")
        observed_peak = max(observed_peak, row["cpu_pct_x100"])
        if row["peak_cpu_pct_x100"] != observed_peak:
            raise ReadyError(
                f"cache CPU peak sequence invalid page={row['page']} "
                f"actual={row['peak_cpu_pct_x100']} expected={observed_peak}"
            )
        if row["budget_bytes"] != EXPECTED_CACHE_BUDGET_BYTES:
            raise ReadyError(
                f"cache budget mismatch page={row['page']} actual={row['budget_bytes']} "
                f"expected={EXPECTED_CACHE_BUDGET_BYTES}"
            )
def read_status_field(pid: int, field: str) -> str:
    try:
        lines = Path(f"/proc/{pid}/status").read_text(encoding="ascii", errors="replace").splitlines()
    except OSError:
        return ""
    prefix = field + ":"
    for line in lines:
        if line.startswith(prefix):
            return line.split(":", 1)[1].strip()
    return ""
def read_diagnostics(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=1.0) as response:
        payload = json.load(response)
    if not isinstance(payload, dict):
        raise ReadyError("relay diagnostics is not an object")
    return payload
def validate_first_event(diagnostics: dict, width: int, height: int) -> dict:
    event = diagnostics.get("first_dirty_event")
    if not isinstance(event, dict):
        raise ReadyError("relay has not captured the formal first frame")
    expected_rect = (0, 0, width, height)
    actual_rect = tuple(int(event.get(key, -1)) for key in ("x", "y", "w", "h"))
    if actual_rect != expected_rect:
        raise ReadyError(f"first frame is not full main blit actual={actual_rect} expected={expected_rect}")
    frame_id = int(event.get("frame_id") or 0)
    base_frame_id = int(event.get("base_frame_id") or -1)
    if frame_id <= 1 or base_frame_id != 1:
        raise ReadyError(f"first frame identity invalid event={event!r}")
    return dict(event)
def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp_name, 0o600)
        os.replace(temp_name, path)
    finally:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass
def build_failure_payload(owner_pid, owner_start_ticks, owner_role,
                          startup_instance_id, stage, error):
    try:
        owner_pid = int(owner_pid)
        owner_start_ticks = int(owner_start_ticks)
    except (TypeError, ValueError) as exc:
        raise ReadyError("failure owner identity malformed") from exc
    if owner_pid <= 0 or owner_start_ticks <= 0:
        raise ReadyError("failure owner identity invalid")
    try:
        actual_start_ticks = int(proc_start_ticks(owner_pid))
    except (OSError, ValueError, IndexError) as exc:
        raise ReadyError(f"failure owner unavailable pid={owner_pid}") from exc
    if actual_start_ticks != owner_start_ticks:
        raise ReadyError(f"failure owner PID/start mismatch pid={owner_pid}")
    try:
        startup_instance_id = str(uuid.UUID(str(startup_instance_id))).lower()
    except (ValueError, AttributeError) as exc:
        raise ReadyError("failure startup instance identity invalid") from exc
    if owner_role not in ("relay", "supervisor"):
        raise ReadyError(f"failure owner role invalid role={owner_role!r}")
    stage = str(stage or "").strip()
    error = str(error or "").strip()
    if not re.fullmatch(r"[a-z0-9_]{1,64}", stage):
        raise ReadyError(f"failure stage invalid stage={stage!r}")
    if not error:
        raise ReadyError("failure error text invalid")
    error = error[:512]
    return {
        "schema": FAILURE_SCHEMA,
        "ready": False,
        "failed": True,
        "boot_id": read_kernel_boot_id(),
        "startup_instance_id": startup_instance_id,
        "owner_role": owner_role,
        "owner_pid": owner_pid,
        "owner_start_ticks": owner_start_ticks,
        "stage": stage,
        "error": error,
        "created_realtime_ns": time.time_ns(),
        "created_monotonic_ns": time.monotonic_ns(),
    }
def publish_failure(path, owner_pid, owner_start_ticks, owner_role,
                    startup_instance_id, stage, error):
    payload = build_failure_payload(
        owner_pid,
        owner_start_ticks,
        owner_role,
        startup_instance_id,
        stage,
        error,
    )
    atomic_write_json(path, payload)
    return payload
def build_ready_payload(
    ui_pid: int,
    ui_log: Path,
    diagnostics: dict,
    width: int,
    height: int,
    required_cpu_list: str,
) -> dict:
    try:
        text = ui_log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ReadyError(f"cannot read UI boot log: {exc}") from exc
    try:
        validate_queue_trace(text.splitlines())
    except ValueError as exc:
        raise ReadyError(f"cache queue evidence invalid: {exc}") from exc
    rows = parse_cache_rows(text)
    validate_cache_rows(rows)
    if "v5 UI remote framebuffer IPC ready:" not in text:
        raise ReadyError("UI process has not crossed the post-cache IPC-ready boundary")
    cpus_allowed = read_status_field(ui_pid, "Cpus_allowed_list")
    if cpus_allowed != required_cpu_list:
        raise ReadyError(
            f"UI pre-render affinity invalid pid={ui_pid} Cpus_allowed_list={cpus_allowed!r} "
            f"expected={required_cpu_list!r}"
        )
    first_event = validate_first_event(diagnostics, width, height)
    current_frame_id = int(diagnostics.get("frame_id") or 0)
    if current_frame_id < int(first_event["frame_id"]):
        raise ReadyError(
            f"relay frame identity regressed first={first_event['frame_id']} current={current_frame_id}"
        )
    if diagnostics.get("ui_ready"):
        raise ReadyError("stale ready metadata was visible before this boot completed")
    ui_start_ticks = int(proc_start_ticks(ui_pid))
    if ui_start_ticks <= 0:
        raise ReadyError(f"UI process start ticks invalid pid={ui_pid} value={ui_start_ticks}")
    return {
        "schema": "v5.ui_ready.v1",
        "ready": True,
        "boot_id": read_kernel_boot_id(),
        "ui_instance_id": str(uuid.uuid4()),
        "ui_pid": ui_pid,
        "ui_start_ticks": ui_start_ticks,
        "cpus_allowed_list": cpus_allowed,
        "width": width,
        "height": height,
        "cache_page_count": len(EXPECTED_QUEUE),
        "cache_slot_count": 13,
        "cache_budget_bytes": EXPECTED_CACHE_BUDGET_BYTES,
        "cache_queue": rows,
        "cache_peak_cpu_pct_x100": max(row["peak_cpu_pct_x100"] for row in rows),
        "first_frame": first_event,
        "current_frame_id": current_frame_id,
        "created_realtime_ns": time.time_ns(),
        "created_monotonic_ns": time.monotonic_ns(),
    }
def wait_and_publish(args: argparse.Namespace) -> dict:
    deadline = time.monotonic() + args.timeout
    last_error = "not_checked"
    while time.monotonic() < deadline:
        if not Path(f"/proc/{args.ui_pid}/status").exists():
            raise ReadyError(f"UI process exited before ready pid={args.ui_pid}")
        try:
            diagnostics = read_diagnostics(args.diagnostics_url)
            payload = build_ready_payload(
                args.ui_pid,
                args.ui_log,
                diagnostics,
                args.width,
                args.height,
                args.require_cpu_list,
            )
            publisher_barrier = validate_final_publisher_barrier(
                args.publisher_snapshot_path,
                args.expected_ini,
            )
            if int(proc_start_ticks(args.ui_pid)) != int(payload["ui_start_ticks"]):
                raise ReadyError(f"UI process identity changed before ready commit pid={args.ui_pid}")
            payload["publisher_barrier"] = publisher_barrier
            atomic_write_json(args.ready_path, payload)
            return payload
        except FinalBarrierError:
            raise
        except (OSError, ReadyError, urllib.error.URLError) as exc:
            last_error = str(exc)
            time.sleep(0.05)
    rows: list[dict] = []
    try:
        rows = parse_cache_rows(args.ui_log.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        pass
    failure_page = EXPECTED_QUEUE[len(rows)] if len(rows) < len(EXPECTED_QUEUE) else "post_cache_ready"
    raise ReadyError(
        f"UI ready timeout seconds={args.timeout:.1f} completed={len(rows)}/9 "
        f"failure_page={failure_page} last_error={last_error}"
    )
def main() -> int:
    parser = argparse.ArgumentParser(description="Validate v5 UI boot cache queue and publish ui_ready metadata."); parser.add_argument("--pre-ui-inputs", action="store_true"); parser.add_argument("--publish-failure", action="store_true"); parser.add_argument("--expected-ini")
    parser.add_argument("--ui-pid", type=int); parser.add_argument("--ui-log", type=Path)
    parser.add_argument("--ready-path", type=Path); parser.add_argument("--diagnostics-url")
    parser.add_argument("--publisher-snapshot-path", type=Path)
    parser.add_argument("--startup-instance-id"); parser.add_argument("--failure-owner-pid", type=int)
    parser.add_argument("--failure-owner-start-ticks", type=int); parser.add_argument("--failure-owner-role")
    parser.add_argument("--failure-stage"); parser.add_argument("--failure-error")
    parser.add_argument("--timeout", type=float, default=30.0); parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=600); parser.add_argument("--require-cpu-list", default="1"); args = parser.parse_args()
    try:
        if args.publish_failure:
            if not all((args.ready_path, args.startup_instance_id,
                        args.failure_owner_pid, args.failure_owner_start_ticks,
                        args.failure_owner_role, args.failure_stage, args.failure_error)):
                raise ReadyError("failure mode requires ready path, startup and owner identity, stage and error")
            failure = publish_failure(
                args.ready_path,
                args.failure_owner_pid,
                args.failure_owner_start_ticks,
                args.failure_owner_role,
                args.startup_instance_id,
                args.failure_stage,
                args.failure_error,
            )
            print("v5_ui_boot_failure PASS " + json.dumps(failure, sort_keys=True, separators=(",", ":")))
            return 0
        if args.pre_ui_inputs:
            if args.expected_ini != BUS_INI: raise ReadyError("pre-UI inputs require the canonical BUS INI")
            result = wait_pre_ui_inputs(args.timeout, expected_ini=args.expected_ini)
            if args.publisher_snapshot_path is None:
                raise ReadyError("pre-UI inputs require publisher snapshot path")
            atomic_write_json(
                args.publisher_snapshot_path,
                build_input_barrier_snapshot(result, args.expected_ini),
            )
            print("v5_ui_boot_inputs PASS " + json.dumps(result, sort_keys=True, separators=(",", ":")))
            return 0
        if not all((args.ui_pid, args.ui_log, args.ready_path, args.diagnostics_url,
                    args.publisher_snapshot_path, args.expected_ini)):
            raise ReadyError("UI ready mode requires UI, diagnostics and publisher snapshot identity")
        payload = wait_and_publish(args)
    except ReadyError as exc:
        if all((args.ready_path, args.startup_instance_id,
                args.failure_owner_pid, args.failure_owner_start_ticks,
                args.failure_owner_role, args.failure_stage)):
            try:
                publish_failure(
                    args.ready_path,
                    args.failure_owner_pid,
                    args.failure_owner_start_ticks,
                    args.failure_owner_role,
                    args.startup_instance_id,
                    args.failure_stage,
                    str(exc),
                )
            except ReadyError as failure_exc:
                print(f"v5_ui_boot_failure FAIL {failure_exc}", file=os.sys.stderr)
        print(f"v5_ui_boot_ready FAIL {exc}", file=os.sys.stderr)
        return 1
    print(
        "v5_ui_boot_ready PASS "
        f"boot_id={payload['boot_id']} ui_instance_id={payload['ui_instance_id']} "
        f"ui_pid={payload['ui_pid']} ui_start_ticks={payload['ui_start_ticks']} "
        f"frame_id={payload['current_frame_id']} cache_budget_bytes={payload['cache_budget_bytes']}",
        flush=True,
    )
    return 0
if __name__ == "__main__": raise SystemExit(main())
