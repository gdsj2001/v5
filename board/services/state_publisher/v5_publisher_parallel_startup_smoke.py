#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import binascii
import ctypes
import errno
import math
import mmap
import os
import subprocess
import struct
import sys
import tempfile
import time
from pathlib import Path

import v5_machine_status_projection as projection

ROOT = Path(__file__).resolve().parents[2]
STATE_ROOT = ROOT / "services/state_publisher"
UI_ROOT = ROOT / "services/ui"
INIT_FILES = (
    STATE_ROOT / "init.d/v5-position-status-publisher",
    STATE_ROOT / "init.d/v5-wcs-status-publisher",
    STATE_ROOT / "init.d/v5-state-publisher",
)
RETIRED = (
    "StartupNotifier", "--startup-notify-fd", "startup_notifier", "WCS_READY",
    "wait_startup_notify", "read_start_ticks", "terminate_start_child",
    "cleanup_failed_start", "notify_path", "mkfifo", "exec 3", "pidfd",
)


def load_boot_module():
    path = UI_ROOT / "v5_ui_boot_ready.py"
    sys.path.insert(0, str(UI_ROOT))
    spec = importlib.util.spec_from_file_location("v5_ui_boot_ready_parallel_smoke", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


def start_body(text: str) -> str:
    return text.split("start_service() {", 1)[1].split("\n}\n\nstop_service()", 1)[0]


def audit_sources(texts: dict[str, str]) -> None:
    for path in INIT_FILES:
        body = start_body(texts[str(path)])
        assert "setsid taskset -c 1 nice -n" in body, f"PARALLEL_SPAWN_MISSING:{path.name}"
        assert "spawned pid=" in body, f"SPAWN_ACTUAL_MISSING:{path.name}"
        assert "sleep " not in body, f"START_SLEEP_RESURRECTED:{path.name}"
        assert "while " not in body, f"START_POLL_RESURRECTED:{path.name}"
    for path in INIT_FILES[1:]:
        assert "printf '%s %s\\n' \"$pid\" \"$pid_start\"" in texts[str(path)], f"PID_START_TICKS_MISSING:{path.name}"
    position = texts[str(INIT_FILES[0])]
    assert "wait_position_hal_ready" not in position, "POSITION_HAL_PREPROBE_RESURRECTED"
    assert "position_block_matches_owner" not in start_body(position), "POSITION_BLOCK_WAIT_RESURRECTED"
    wcs = texts[str(INIT_FILES[1])] + texts[str(STATE_ROOT / "v5_wcs_status_publisher.py")]
    for token in RETIRED:
        assert token not in wcs, f"WCS_EVENT_RESURRECTED:{token}"
    for token in (
        "LINUXCNC_RUNTIME_USER=${V5_LINUXCNC_RUNTIME_USER:-petalinux}",
        'TOOL_MMAP_PATH=$LINUXCNC_PY_HOME/.tool.mmap',
        "require_tool_mmap() {",
        '[ ! -f "$TOOL_MMAP_PATH" ]',
        '[ ! -r "$TOOL_MMAP_PATH" ]',
        '[ ! -w "$TOOL_MMAP_PATH" ]',
    ):
        assert token in wcs, f"WCS_TOOL_MMAP_OWNER_GATE_MISSING:{token}"
    assert '$1 == "root"' not in wcs, "WCS_ROOT_HOME_RESURRECTED"
    assert not (STATE_ROOT / "v5_wcs_startup_smoke.py").exists(), "WCS_EVENT_SMOKE_RESURRECTED"
    relay = texts[str(UI_ROOT / "init.d/v5-ui-relay")]
    assert '"$BOOT_READY" --pre-ui-inputs --expected-ini "$expected_ini"' in relay, "UI_BARRIER_NOT_CANONICAL"
    assert relay.count('--publisher-snapshot-path "$PUBLISHER_SNAPSHOT_PATH"') == 2, "UI_FINAL_PUBLISHER_SNAPSHOT_GATE_MISSING"
    assert '--expected-ini "$PROJECT_ROOT/linuxcnc/ini/v5_bus.ini"' in relay, "UI_FINAL_EXPECTED_INI_GATE_MISSING"
    for token in (
        "process_identity_matches() {",
        'process_has_exact_arg "$identity_pid" "$identity_expected_arg"',
        '"$RELAY_STARTFILE" "$RELAY_DAEMON"',
        '"$UI_STARTFILE" "$UI_DAEMON"',
        "--failure-owner-start-ticks",
        "--failure-stage ui_ready_handshake",
    ):
        assert token in relay, f"UI_LIFECYCLE_IDENTITY_GATE_MISSING:{token}"
    assert "time.sleep(0.1)" not in relay, "UI_STATUS_POLL_RESURRECTED"
    start = start_body(relay)
    assert start.index("wait_boot_inputs_ready") < start.index('"$UI_DAEMON" --serve'), "UI_SPAWNED_BEFORE_ACTUAL_BARRIER"
    assert "v5_bus.ini" in relay and "v5_pulse.ini" in relay, "BUS_PULSE_MODE_DETECTOR_MISSING"
    assert "active_ini=conflict" in relay, "BUS_PULSE_CONFLICT_GATE_MISSING"
    assert "v5_ui_relay rejects disabled Pulse runtime mode" in relay, "PULSE_RUNTIME_FAIL_CLOSED_MISSING"
    assert 'expected_ini="$PROJECT_ROOT/linuxcnc/ini/v5_pulse.ini"' not in relay, "PULSE_RUNTIME_BARRIER_RESURRECTED"
    record = relay.split("  record_active_ini() {", 1)[1].split("\n  }", 1)[0]
    command = "active_ini=\nrecord_active_ini() {" + record + "\n}\nrecord_active_ini bus\nrecord_active_ini pulse\ntest \"$active_ini\" = conflict"
    assert subprocess.run(["sh", "-c", command], check=False).returncode == 0, "BUS_PULSE_CONFLICT_BEHAVIOR_MISSING"
    boot = texts[str(UI_ROOT / "v5_ui_boot_ready.py")]
    for token in ("inotify_init1", "inotify_add_watch", "select.poll", "publisher exited before UI barrier"):
        assert token in boot, f"UI_EVENT_GATE_MISSING:{token}"
    assert 'rsplit(")", 1)[1].split()[19]' in boot, "PID_REUSE_START_TICKS_GATE_MISSING"
    assert 'accept_identity("position"' in boot, "POSITION_UNIQUE_WRITER_GATE_MISSING"
    assert '"--bus-path", str(BUS_STATUS_PATH)' in boot, "POSITION_BUS_PATH_IDENTITY_MISSING"
    assert "import fcntl" not in boot, "PYTHON_FCNTL_RESURRECTED"
    assert "ctypes.CDLL(None, use_errno=True).flock" in boot, "LIBC_FLOCK_GATE_MISSING"
    assert 'if ready and checker is current_pre_ui_inputs' in boot, "FINAL_UNIQUE_RECHECK_MISSING"
    assert "publisher_identities(cache, unique, expected_ini)" in boot, "EXPECTED_INI_CHAIN_MISSING"
    assert "validate_final_publisher_barrier(" in boot, "FINAL_PUBLISHER_BARRIER_MISSING"
    assert '"schema": FAILURE_SCHEMA' in boot, "STRUCTURED_UI_FAILURE_MISSING"


class FakeWatcher:
    def __init__(self, fail=None):
        self.fail = fail
        self.wait_count = 0
        self.watched = set()
        self.closed = False

    def watch_pids(self, pids):
        self.watched.update(pids)

    def wait(self, timeout_ms):
        assert timeout_ms > 0
        self.wait_count += 1
        if self.fail:
            raise self.fail

    def close(self):
        self.closed = True


def sequence_checker(sequence):
    cursor = {"index": 0}
    def check():
        index = min(cursor["index"], len(sequence) - 1)
        cursor["index"] += 1
        return sequence[index], {"position": 11, "wcs": 12, "state": 13}
    return check


def behavior_smoke(boot) -> None:
    for rejected in ("", "/opt/8ax/v5/linuxcnc/ini/v5_pulse.ini"):
        factories = []
        try:
            boot.wait_pre_ui_inputs(120.0, watcher_factory=lambda: factories.append(1),
                                    expected_ini=rejected)
        except boot.ReadyError as exc:
            assert "canonical BUS INI" in str(exc) and not factories
        else:
            raise AssertionError("NON_BUS_MODE_ENTERED_ACTUAL_BARRIER:" + rejected)

    ordered = [
        {"position": 1, "state": None, "wcs": None, "modal": None},
        {"position": 2, "state": 2, "wcs": None, "modal": None},
        {"position": 3, "state": 2, "wcs": 10, "modal": None},
        {"position": 4, "state": 4, "wcs": 10, "modal": 30},
        {"position": 5, "state": 6, "wcs": 20, "modal": 40},
    ]
    watcher = FakeWatcher()
    result = boot.wait_pre_ui_inputs(1.0, sequence_checker(ordered), lambda: watcher)
    assert result["markers"] == ordered[-1]
    assert watcher.wait_count == 4 and watcher.closed and watcher.watched == {11, 12, 13}

    already_ready = [
        {"position": 1, "state": 2, "wcs": 10, "modal": 20},
        {"position": 2, "state": 4, "wcs": 20, "modal": 30},
    ]
    watcher = FakeWatcher()
    boot.wait_pre_ui_inputs(1.0, sequence_checker(already_ready), lambda: watcher)
    assert watcher.wait_count == 1, "REGISTER_THEN_RECHECK_RACE"

    unavailable = [
        {"position": 1, "state": None, "wcs": 10, "modal": 20},
        {"position": 2, "state": None, "wcs": 20, "modal": 30},
        {"position": 3, "state": 2, "wcs": 30, "modal": 40},
        {"position": 4, "state": 4, "wcs": 40, "modal": 50},
    ]
    watcher = FakeWatcher()
    boot.wait_pre_ui_inputs(1.0, sequence_checker(unavailable), lambda: watcher)
    assert watcher.wait_count == 3, "STATE_UNAVAILABLE_FALSE_READY"

    transient_attempt = {"count": 0}
    def transient_then_ready():
        transient_attempt["count"] += 1
        if transient_attempt["count"] == 1:
            raise boot.ReadyError("State coordinate/trajectory values invalid")
        generation = transient_attempt["count"]
        return ({
            "position": generation,
            "state": generation,
            "wcs": generation,
            "modal": generation,
        }, {"position": 11, "wcs": 12, "state": 13})
    watcher = FakeWatcher()
    result = boot.wait_pre_ui_inputs(1.0, transient_then_ready, lambda: watcher)
    assert result["markers"]["state"] == 3
    assert watcher.wait_count == 2, "TRANSIENT_STATE_FRAME_NOT_RETRIED"

    for marker in ("publisher exited before UI barrier pid=12", "pre-UI actual barrier timeout"):
        failure = boot.ReadyError(marker)
        watcher = FakeWatcher(failure)
        try:
            boot.wait_pre_ui_inputs(1.0, sequence_checker([ordered[0]]), lambda: watcher)
        except boot.ReadyError as exc:
            assert marker in str(exc) and watcher.closed
        else:
            raise AssertionError("FAIL_CLOSED_EVENT_MISSING:" + marker)


def final_barrier_smoke(boot) -> None:
    boot_id = "11111111-1111-4111-8111-111111111111"
    identities = {
        "position": {
            "pid": 11,
            "start_ticks": 101,
            "argv": ["position"],
            "writer_identity": 77,
        },
        "wcs": {"pid": 12, "start_ticks": 102, "argv": ["wcs"]},
        "state": {"pid": 13, "start_ticks": 103, "argv": ["state"]},
    }
    baseline = {"position": 5, "state": 6, "wcs": 7, "modal": 8}
    result = {
        "owners": {"position": 11, "wcs": 12, "state": 13},
        "markers": baseline,
        "owner_identities": identities,
    }
    original_boot_id = boot.read_kernel_boot_id
    boot.read_kernel_boot_id = lambda path=boot.KERNEL_BOOT_ID_PATH: boot_id
    try:
        with tempfile.TemporaryDirectory() as directory:
            snapshot_path = Path(directory) / "publisher_snapshot.json"
            snapshot = boot.build_input_barrier_snapshot(result, boot.BUS_INI)
            boot.atomic_write_json(snapshot_path, snapshot)

            def current(cache):
                assert cache == {
                    "position": (11, "101", ("position",), "77"),
                    "wcs": (12, "102", ("wcs",)),
                    "state": (13, "103", ("state",)),
                }
                return ({"position": 9, "state": 10, "wcs": 11, "modal": 12},
                        {"position": 11, "wcs": 12, "state": 13})

            barrier = boot.validate_final_publisher_barrier(
                snapshot_path, boot.BUS_INI, checker=current)
            assert barrier["baseline_markers"] == baseline
            assert barrier["final_markers"]["state"] == 10
            assert barrier["owner_identities"] == identities

            def regressed(_cache):
                return ({"position": 4, "state": 10, "wcs": 11, "modal": 12},
                        {"position": 11, "wcs": 12, "state": 13})

            try:
                boot.validate_final_publisher_barrier(
                    snapshot_path, boot.BUS_INI, checker=regressed)
            except boot.ReadyError as exc:
                assert "regressed" in str(exc)
            else:
                raise AssertionError("FINAL_PUBLISHER_GENERATION_REGRESSION_ACCEPTED")

            def changed(_cache):
                raise boot.ReadyError("position owner changed during barrier")

            try:
                boot.validate_final_publisher_barrier(
                    snapshot_path, boot.BUS_INI, checker=changed)
            except boot.ReadyError as exc:
                assert "owner changed" in str(exc)
            else:
                raise AssertionError("FINAL_PUBLISHER_IDENTITY_CHANGE_ACCEPTED")
    finally:
        boot.read_kernel_boot_id = original_boot_id


def failure_payload_smoke(boot) -> None:
    original_start_ticks = boot.proc_start_ticks
    original_boot_id = boot.read_kernel_boot_id
    boot.proc_start_ticks = lambda pid: "500" if pid == 42 else "0"
    boot.read_kernel_boot_id = lambda path=boot.KERNEL_BOOT_ID_PATH: "11111111-1111-4111-8111-111111111111"
    try:
        payload = boot.build_failure_payload(
            42,
            500,
            "relay",
            "22222222-2222-4222-8222-222222222222",
            "ui_ready_handshake",
            "cache queue failed",
        )
        assert payload["schema"] == boot.FAILURE_SCHEMA
        assert payload["ready"] is False and payload["failed"] is True
        assert payload["owner_pid"] == 42 and payload["owner_start_ticks"] == 500
        try:
            boot.build_failure_payload(
                42,
                501,
                "relay",
                "22222222-2222-4222-8222-222222222222",
                "ui_ready_handshake",
                "cache queue failed",
            )
        except boot.ReadyError as exc:
            assert "PID/start mismatch" in str(exc)
        else:
            raise AssertionError("UI_FAILURE_PID_REUSE_ACCEPTED")
    finally:
        boot.proc_start_ticks = original_start_ticks
        boot.read_kernel_boot_id = original_boot_id


def with_fnv_crc(boot, fmt, values, crc_index, suffix):
    values[crc_index] = 0
    raw = fmt.pack(*values)
    values[crc_index] = boot.fnv32(raw[:-suffix])
    return fmt.pack(*values)


def position_lock_smoke(boot) -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "position.lock"; path.write_text("owner\n", encoding="ascii")
        calls = []
        def held(fd, operation):
            calls.append(operation); ctypes.set_errno(errno.EAGAIN); return -1
        boot.require_position_lock_held(path, held)
        assert calls == [2 | 4], "CONTENDED_POSITION_LOCK_NOT_ACCEPTED"
        calls.clear()
        def unheld(fd, operation):
            calls.append(operation); ctypes.set_errno(0); return 0
        try: boot.require_position_lock_held(path, unheld)
        except boot.ReadyError as exc: assert "not held" in str(exc)
        else: raise AssertionError("UNHELD_POSITION_LOCK_FALSE_READY")
        assert calls == [2 | 4, 8], "UNHELD_POSITION_LOCK_NOT_RELEASED"
        def broken(_fd, _operation): ctypes.set_errno(errno.EPERM); return -1
        try: boot.require_position_lock_held(path, broken)
        except boot.ReadyError as exc: assert f"errno={errno.EPERM}" in str(exc)
        else: raise AssertionError("POSITION_LOCK_BACKEND_ERROR_FALSE_READY")


def block_validation_smoke(boot) -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        original = {name: getattr(boot, name) for name in
                    ("POSITION_PATH", "WCS_PATH", "MODAL_PATH", "STATE_PATH", "POSITION_PID")}
        original_identities = boot.publisher_identities
        original_monotonic_ns = boot.time.monotonic_ns
        try:
            boot.POSITION_PATH = root / "position.bin"
            boot.WCS_PATH = root / "wcs.bin"
            boot.MODAL_PATH = root / "modal.bin"
            boot.STATE_PATH = root / "state.bin"
            boot.POSITION_PID = root / "position.pid"
            boot.POSITION_PID.write_text("11 22 77\n", encoding="ascii")
            boot.publisher_identities = lambda *args, **kwargs: {"position": 11, "wcs": 12, "state": 13}
            now = time.monotonic_ns()
            boot.time.monotonic_ns = lambda: now

            position = list(boot.POSITION_FMT.unpack(bytes(boot.POSITION_FMT.size)))
            position[:10] = [
                0x56504F53, 3, boot.POSITION_FMT.size, 3, 5, 77,
                2, 0, now, 1]
            position[10:20] = [float(index) for index in range(10)]
            position[20:25] = [0.001] * 5
            position[25:30] = [0.0] * 5
            position[30:35] = [3] * 5
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))

            wcs = list(boot.WCS_FMT.unpack(bytes(boot.WCS_FMT.size)))
            wcs[:11] = [0x56574353, 2, 416, 1, 0, 9, 5, 1, 1, 0, now]
            wcs[11:56] = [float(index) for index in range(45)]
            boot.WCS_PATH.write_bytes(with_fnv_crc(boot, boot.WCS_FMT, wcs, -2, 8))

            projection.write_modal_tool_status(str(boot.MODAL_PATH), "G54", 1, 1, 15.0)
            valid_modal = list(boot.MODAL_FMT.unpack(boot.MODAL_PATH.read_bytes()))
            assert valid_modal[3] == 1 and valid_modal[4:7] == [1, 1, 1]

            state = bytearray(boot.STATE_FRAME_SIZE)
            struct.pack_into(
                "<8I", state, 0, 0x56355348, 3,
                boot.STATE_FRAME_SIZE, boot.STATE_FRAME_SIZE,
                boot.STATE_PAYLOAD_SIZE, 0, 2, 0)
            struct.pack_into("<QII", state, 32, time.monotonic_ns(), 0x203, 0)
            struct.pack_into("<IIQQQ", state, 48, 77, 0, now, 1, 1)
            struct.pack_into(
                "<10d", state, 80, *([float(i) for i in range(10)]))
            struct.pack_into("<5d", state, 160, *([0.001] * 5))
            struct.pack_into("<5d", state, 200, *([0.0] * 5))
            struct.pack_into("<5B", state, 240, *([3] * 5))
            struct.pack_into("<Q", state, 976, 1)
            struct.pack_into("<Q", state, 1008, 1)
            struct.pack_into("<Q", state, 1016, 1)
            struct.pack_into("<Q", state, 1024, 1)
            struct.pack_into("<Q", state, 1040, 1)
            struct.pack_into("<I", state, 1052, 1)
            struct.pack_into("<I", state, 1084, 3)
            crc = binascii.crc32(state[:24]); crc = binascii.crc32(state[32:], crc) & 0xFFFFFFFF
            struct.pack_into("<I", state, 28, crc)
            boot.STATE_PATH.write_bytes(state)

            markers, owners = boot.current_pre_ui_inputs()
            assert owners == {"position": 11, "wcs": 12, "state": 13}
            assert all(markers[name] is not None for name in markers), markers

            position[3] = 0
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))
            invalid_markers, _ = boot.current_pre_ui_inputs()
            assert invalid_markers["position"] is None and invalid_markers["wcs"] is not None
            position[3] = 3
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))

            wcs[3] = wcs[7] = 0
            boot.WCS_PATH.write_bytes(with_fnv_crc(boot, boot.WCS_FMT, wcs, -2, 8))
            assert boot.current_pre_ui_inputs()[0]["wcs"] is None
            wcs[3] = wcs[7] = 1
            boot.WCS_PATH.write_bytes(with_fnv_crc(boot, boot.WCS_FMT, wcs, -2, 8))

            projection.write_modal_tool_status(str(boot.MODAL_PATH), "", None, 0, 0.0)
            assert boot.current_pre_ui_inputs()[0]["modal"] is None
            projection.write_modal_tool_status(str(boot.MODAL_PATH), "G54", 1, 1, 15.0)

            for index in (4, 5, 6):
                invalid_modal = list(valid_modal); invalid_modal[index] = 0
                boot.MODAL_PATH.write_bytes(with_fnv_crc(
                    boot, boot.MODAL_FMT, invalid_modal, -3, 12))
                assert boot.current_pre_ui_inputs()[0]["modal"] is None, index
            for index, value in ((9, -1), (13, b""), (14, math.nan)):
                invalid_modal = list(valid_modal); invalid_modal[index] = value
                boot.MODAL_PATH.write_bytes(with_fnv_crc(
                    boot, boot.MODAL_FMT, invalid_modal, -3, 12))
                try: boot.current_pre_ui_inputs()
                except boot.ReadyError as exc: assert "modal/tool actual invalid" in str(exc)
                else: raise AssertionError("INVALID_MODAL_TOOL_ACTUAL_FALSE_READY")
            boot.MODAL_PATH.write_bytes(with_fnv_crc(
                boot, boot.MODAL_FMT, valid_modal, -3, 12))

            unavailable = bytearray(state); struct.pack_into("<I", unavailable, 44, 4)
            struct.pack_into("<I", unavailable, 28, 0)
            crc = binascii.crc32(unavailable[:24]); crc = binascii.crc32(unavailable[32:], crc) & 0xFFFFFFFF
            struct.pack_into("<I", unavailable, 28, crc); boot.STATE_PATH.write_bytes(unavailable)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            odd = bytearray(state); struct.pack_into("<I", odd, 24, 3)
            boot.STATE_PATH.write_bytes(odd)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            zero = bytes(boot.STATE_FRAME_SIZE); boot.STATE_PATH.write_bytes(zero)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            mismatch = bytearray(state); struct.pack_into("<d", mismatch, 80, 99.0)
            struct.pack_into("<I", mismatch, 28, 0)
            crc = binascii.crc32(mismatch[:24]); crc = binascii.crc32(mismatch[32:], crc) & 0xFFFFFFFF
            struct.pack_into("<I", mismatch, 28, crc); boot.STATE_PATH.write_bytes(mismatch)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            stable_reader = boot.read_state_seqlock
            try:
                for offset, value, name in (
                    (boot.STATE_TRAJECTORY_COUNT_OFFSET, 17, "trajectory"),
                    (boot.STATE_SCENE_POINT_COUNT_OFFSET, 513, "scene_point"),
                    (boot.STATE_SCENE_SEGMENT_COUNT_OFFSET, 49, "scene_segment"),
                    (boot.STATE_SCENE_MARKER_COUNT_OFFSET, 17, "scene_marker"),
                ):
                    bad_count = bytearray(state)
                    struct.pack_into("<I", bad_count, offset, value)
                    struct.pack_into("<I", bad_count, 28, 0)
                    crc = binascii.crc32(bad_count[:24])
                    crc = binascii.crc32(bad_count[32:], crc) & 0xFFFFFFFF
                    struct.pack_into("<I", bad_count, 28, crc)
                    boot.read_state_seqlock = lambda _path, raw=bytes(bad_count): raw
                    try: boot.current_pre_ui_inputs()
                    except boot.ReadyError as exc:
                        assert "contract" in str(exc) or "counts" in str(exc), name
                    else: raise AssertionError(f"STATE_{name.upper()}_COUNT_FALSE_READY")
            finally: boot.read_state_seqlock = stable_reader
            boot.STATE_PATH.write_bytes(state)

            corrupt = bytearray(boot.POSITION_PATH.read_bytes()); corrupt[-9] ^= 1
            boot.POSITION_PATH.write_bytes(corrupt)
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "CRC" in str(exc)
            else: raise AssertionError("CRC_CORRUPTION_FALSE_READY")

            position[8] = now - 2_000_000_000
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "stale" in str(exc)
            else: raise AssertionError("STALE_BLOCK_FALSE_READY")

            position[8] = now; position[5] = 78
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "owner" in str(exc)
            else: raise AssertionError("WRONG_WRITER_FALSE_READY")

            position[5] = 77
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))
            wcs[11] = math.nan
            boot.WCS_PATH.write_bytes(with_fnv_crc(boot, boot.WCS_FMT, wcs, -2, 8))
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "value" in str(exc)
            else: raise AssertionError("NONFINITE_WCS_FALSE_READY")
        finally:
            for name, value in original.items(): setattr(boot, name, value)
            boot.publisher_identities = original_identities
            boot.time.monotonic_ns = original_monotonic_ns

    assert boot.cpu_lists_are_cpu1(["1", "1"])
    assert not boot.cpu_lists_are_cpu1(["1", "0"])
    originals = (boot.proc_start_ticks, boot.require_cpu1, boot.exact_processes)
    try:
        boot.proc_start_ticks = lambda pid: "101"
        boot.require_cpu1 = lambda pid: None
        boot.exact_processes = lambda argv, binary=False: [7]
        assert boot.accept_identity("test", 7, "101", ["daemon"], [["daemon"]], {}, True)
        try: boot.accept_identity("test", 7, "100", ["daemon"], [["daemon"]], {}, True)
        except boot.ReadyError as exc: assert "start" in str(exc)
        else: raise AssertionError("PID_REUSE_FALSE_READY")
        assert not boot.accept_identity("test", 7, "101", ["wrong"], [["daemon"]], {}, True)
        position_cache = {}
        assert boot.accept_identity("position", 7, "101", ["daemon"], [["daemon"]],
                                    position_cache, True, extra=("77",))
        try: boot.accept_identity("position", 7, "101", ["daemon"], [["daemon"]],
                                  position_cache, False, extra=("78",))
        except boot.ReadyError as exc: assert "changed" in str(exc)
        else: raise AssertionError("POSITION_WRITER_IDENTITY_CHANGE_FALSE_READY")
        bus = ["publisher", "--ini", "/opt/8ax/v5/linuxcnc/ini/v5_bus.ini"]
        pulse = ["publisher", "--ini", "/opt/8ax/v5/linuxcnc/ini/v5_pulse.ini"]
        boot.exact_processes = lambda argv, binary=False: [7]
        assert boot.accept_identity("wcs", 7, "101", bus, [bus], {}, True)
        assert not boot.accept_identity("wcs", 7, "101", pulse, [bus], {}, True)
        try: boot.accept_identity("wcs", 7, "101", pulse, [pulse], {"wcs": (7, "101", tuple(bus))}, False)
        except boot.ReadyError as exc: assert "changed" in str(exc)
        else: raise AssertionError("BUS_PULSE_OWNER_CONFLICT_FALSE_READY")
        boot.exact_processes = lambda argv, binary=False: [7, 8]
        try: boot.accept_identity("test", 7, "101", ["daemon"], [["daemon"]], {}, True)
        except boot.ReadyError as exc: assert "unique" in str(exc)
        else: raise AssertionError("DUPLICATE_WRITER_FALSE_READY")
    finally:
        boot.proc_start_ticks, boot.require_cpu1, boot.exact_processes = originals


def mutation_smoke(texts: dict[str, str]) -> None:
    mutations = (
        (str(INIT_FILES[2]), "start_service() {", "start_service() {\n  sleep 1"),
        (str(INIT_FILES[0]), "start_service() {", "wait_position_hal_ready() { :; }\nstart_service() {"),
        (str(STATE_ROOT / "v5_wcs_status_publisher.py"), "def main()", "class StartupNotifier: pass\n\ndef main()"),
        (str(UI_ROOT / "init.d/v5-ui-relay"), "wait_boot_inputs_ready() {", "# time.sleep(0.1)\nwait_boot_inputs_ready() {"),
        (str(UI_ROOT / "v5_ui_boot_ready.py"), "import argparse", "import fcntl\nimport argparse"),
    )
    for path, needle, replacement in mutations:
        changed = dict(texts); changed[path] = changed[path].replace(needle, replacement, 1)
        try:
            audit_sources(changed)
        except AssertionError:
            continue
        raise AssertionError("ANTI_RESURRECTION_MUTATION_SURVIVED:" + replacement)


def posix_inotify_smoke(boot) -> None:
    if os.name != "posix":
        return
    with tempfile.TemporaryDirectory(dir="/dev/shm") as directory:
        watcher = boot.ShmEventWatcher(directory, {"state.bin"})
        try:
            path = Path(directory) / "state.bin"
            with path.open("w+b") as stream:
                stream.truncate(boot.STATE_FRAME_SIZE)
                page = mmap.mmap(stream.fileno(), boot.STATE_FRAME_SIZE)
                page[:4] = b"V5SH"
                page.flush(); page.close()
            watcher.wait(1000)
        finally:
            watcher.close()


def main() -> int:
    paths = list(INIT_FILES) + [
        STATE_ROOT / "v5_wcs_status_publisher.py",
        UI_ROOT / "init.d/v5-ui-relay",
        UI_ROOT / "v5_ui_boot_ready.py",
    ]
    texts = {str(path): path.read_text(encoding="utf-8") for path in paths}
    audit_sources(texts)
    boot = load_boot_module()
    behavior_smoke(boot)
    final_barrier_smoke(boot)
    failure_payload_smoke(boot)
    position_lock_smoke(boot)
    block_validation_smoke(boot)
    posix_inotify_smoke(boot)
    mutation_smoke(texts)
    print("V5_PUBLISHER_PARALLEL_STARTUP_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
