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
    assert not (STATE_ROOT / "v5_wcs_startup_smoke.py").exists(), "WCS_EVENT_SMOKE_RESURRECTED"
    relay = texts[str(UI_ROOT / "init.d/v5-ui-relay")]
    assert '"$BOOT_READY" --pre-ui-inputs --expected-ini "$expected_ini" --timeout 120' in relay, "UI_BARRIER_NOT_CANONICAL"
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
    assert "import fcntl" not in boot, "PYTHON_FCNTL_RESURRECTED"
    assert "ctypes.CDLL(None, use_errno=True).flock" in boot, "LIBC_FLOCK_GATE_MISSING"
    assert 'if ready and checker is current_pre_ui_inputs' in boot, "FINAL_UNIQUE_RECHECK_MISSING"
    assert "publisher_identities(cache, unique, expected_ini)" in boot, "EXPECTED_INI_CHAIN_MISSING"


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

    for marker in ("publisher exited before UI barrier pid=12", "pre-UI actual barrier timeout"):
        failure = boot.ReadyError(marker)
        watcher = FakeWatcher(failure)
        try:
            boot.wait_pre_ui_inputs(1.0, sequence_checker([ordered[0]]), lambda: watcher)
        except boot.ReadyError as exc:
            assert marker in str(exc) and watcher.closed
        else:
            raise AssertionError("FAIL_CLOSED_EVENT_MISSING:" + marker)


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
            position[:7] = [0x56504F53, 2, 152, 3, 5, 77, now]
            position[7:17] = [float(index) for index in range(10)]
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))

            wcs = list(boot.WCS_FMT.unpack(bytes(boot.WCS_FMT.size)))
            wcs[:11] = [0x56574353, 2, 416, 1, 0, 9, 5, 1, 1, 0, now]
            wcs[11:56] = [float(index) for index in range(45)]
            boot.WCS_PATH.write_bytes(with_fnv_crc(boot, boot.WCS_FMT, wcs, -2, 8))

            projection.write_modal_tool_status(str(boot.MODAL_PATH), "G54", 1, 1, 15.0)
            valid_modal = list(boot.MODAL_FMT.unpack(boot.MODAL_PATH.read_bytes()))
            assert valid_modal[3] == 1 and valid_modal[4:7] == [1, 1, 1]

            state = bytearray(840)
            struct.pack_into("<8I", state, 0, 0x56355348, 2, 840, 840, 808, 0, 2, 0)
            struct.pack_into("<QII10d", state, 32, time.monotonic_ns(), 3, 0,
                             *([float(i) for i in range(10)]))
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

            zero = bytes(840); boot.STATE_PATH.write_bytes(zero)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            mismatch = bytearray(state); struct.pack_into("<d", mismatch, 48, 99.0)
            struct.pack_into("<I", mismatch, 28, 0)
            crc = binascii.crc32(mismatch[:24]); crc = binascii.crc32(mismatch[32:], crc) & 0xFFFFFFFF
            struct.pack_into("<I", mismatch, 28, crc); boot.STATE_PATH.write_bytes(mismatch)
            assert boot.current_pre_ui_inputs()[0]["state"] is None
            boot.STATE_PATH.write_bytes(state)

            bad_count = bytearray(state); struct.pack_into("<I", bad_count, 768, 17)
            struct.pack_into("<I", bad_count, 28, 0)
            crc = binascii.crc32(bad_count[:24]); crc = binascii.crc32(bad_count[32:], crc) & 0xFFFFFFFF
            struct.pack_into("<I", bad_count, 28, crc); boot.STATE_PATH.write_bytes(bad_count)
            stable_reader = boot.read_state_seqlock
            boot.read_state_seqlock = lambda _path: bytes(bad_count)
            try:
                try: boot.current_pre_ui_inputs()
                except boot.ReadyError as exc: assert "trajectory" in str(exc)
                else: raise AssertionError("STATE_TRAJECTORY_COUNT_FALSE_READY")
            finally: boot.read_state_seqlock = stable_reader
            boot.STATE_PATH.write_bytes(state)

            corrupt = bytearray(boot.POSITION_PATH.read_bytes()); corrupt[-9] ^= 1
            boot.POSITION_PATH.write_bytes(corrupt)
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "CRC" in str(exc)
            else: raise AssertionError("CRC_CORRUPTION_FALSE_READY")

            position[6] = now - 2_000_000_000
            boot.POSITION_PATH.write_bytes(with_fnv_crc(boot, boot.POSITION_FMT, position, -2, 8))
            try: boot.current_pre_ui_inputs()
            except boot.ReadyError as exc: assert "stale" in str(exc)
            else: raise AssertionError("STALE_BLOCK_FALSE_READY")

            position[6] = now; position[5] = 78
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
                stream.truncate(840)
                page = mmap.mmap(stream.fileno(), 840)
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
    position_lock_smoke(boot)
    block_validation_smoke(boot)
    posix_inotify_smoke(boot)
    mutation_smoke(texts)
    print("V5_PUBLISHER_PARALLEL_STARTUP_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
