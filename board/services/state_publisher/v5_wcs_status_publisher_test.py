#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
import types
from pathlib import Path

import v5_wcs_status_codec as codec

if 'resource' not in sys.modules:
    try:
        import resource  # noqa: F401
    except ModuleNotFoundError:
        sys.modules['resource'] = types.SimpleNamespace(
            RLIMIT_MEMLOCK=0,
            RLIM_INFINITY=-1,
            getrlimit=lambda _resource: (0, 0),
            setrlimit=lambda _resource, _limits: None)

import v5_wcs_status_publisher as publisher


class FakeStat:
    def __init__(self):
        self.g5x_index = 1
        self.g5x_offset = [0.0] * 9
        self.gcodes = ()
        self.spindle = ({'speed': 0.0, 'override': 1.0},)
        self.feedrate = 1.0
        self.current_vel = 0.0
        self.poll_count = 0

    def poll(self):
        self.poll_count += 1

    @property
    def joint(self):
        raise AssertionError('retired heavy joint dictionary path was accessed')


def check_resident_epoch_changes_only_with_table() -> None:
    owner = publisher.ResidentWcsParameterOwner()
    owner.table = publisher.empty_wcs_table()
    owner.epoch = 17
    assert owner.update_active_from_stat(0, [0.0] * publisher.WCS_AXIS_COUNT)
    assert owner.epoch == 17
    assert owner.update_active_from_stat(0, [1.0, 0.0, 0.0, 0.0, 0.0])
    assert owner.epoch == 18
    assert owner.update_active_from_stat(0, [1.0, 0.0, 0.0, 0.0, 0.0])
    assert owner.epoch == 18


def check_slow_status_cadence() -> None:
    counts = {'wcs': 0, 'modal': 0, 'modal_compute': 0}
    publisher.write_status = lambda *args, **kwargs: counts.__setitem__(
        'wcs', counts['wcs'] + 1)
    publisher.write_modal_tool_status = lambda *args, **kwargs: counts.__setitem__(
        'modal', counts['modal'] + 1)

    def modal_text(_gcodes, g5x):
        counts['modal_compute'] += 1
        return f'G{g5x}'

    publisher.modal_text_from_gcodes = modal_text
    publisher.current_tool_number = lambda _stat: 0
    publisher.current_tool_length = lambda *_args: (1, 15.0)
    publisher.interpreter_idle_from_stat = lambda *_args: (1, 1)
    publisher.interpreter_paused_from_stat = lambda *_args: (1, 0)
    publisher.all_homed_from_stat = lambda _stat: (1, 1)
    publisher.line_from_stat = lambda _stat, _name: (1, 0)
    publisher.mdi_run_from_stat = lambda _stat, _line: (1, 0, 0, '')

    stat = FakeStat()
    owner = publisher.ResidentWcsParameterOwner()
    owner.table = publisher.empty_wcs_table()
    owner.epoch = 23
    cadence = publisher.StatusPublishCadence(0.2)
    for index in range(31):
        publisher.poll_once(
            stat,
            owner,
            '/dev/null/wcs',
            '/dev/null/modal',
            publish_cadence=cadence,
            now_monotonic=index * 0.033)
    assert stat.poll_count == 31
    assert 5 <= counts['wcs'] <= 6
    assert 5 <= counts['modal'] <= 6
    assert counts['modal_compute'] == counts['modal']

    epoch_before_active_change = owner.epoch
    wcs_before_active_change = counts['wcs']
    stat.g5x_index = 2
    publisher.poll_once(
        stat,
        owner,
        '/dev/null/wcs',
        '/dev/null/modal',
        publish_cadence=cadence,
        now_monotonic=1.024)
    assert counts['wcs'] == wcs_before_active_change + 1
    assert owner.epoch == epoch_before_active_change


def check_wcs_owner_has_no_position_hot_path() -> None:
    text = Path(publisher.__file__).read_text(encoding='utf-8')
    for retired in (
            'write_position_status',
            'NativeRotaryDisplayProjection',
            'HalReadAccess',
            '--position-path',
            '--mock-mcs'):
        assert retired not in text
    assert 'DEFAULT_WCS_INTERVAL_MS = 200' in text


def check_atomic_shm_write_needs_no_fsync() -> None:
    with tempfile.TemporaryDirectory(prefix='v5_wcs_codec_') as temporary:
        path = Path(temporary) / 'status.bin'
        original_fsync = getattr(codec.os, 'fsync', None)
        codec.os.fsync = lambda _fd: (_ for _ in ()).throw(
            AssertionError('fsync called'))
        try:
            codec.atomic_write(str(path), b'fresh-native-status')
        finally:
            if original_fsync is not None:
                codec.os.fsync = original_fsync
        assert path.read_bytes() == b'fresh-native-status'


def check_lifecycle_lock_owns_singleton() -> None:
    with tempfile.TemporaryDirectory(prefix='v5_wcs_lock_') as temporary:
        lock_path = str(Path(temporary) / 'wcs.lock')
        pidfile_path = str(Path(temporary) / 'wcs.pid')
        operations = []
        original_start_ticks = publisher.process_start_ticks
        publisher.process_start_ticks = lambda pid=None: '101'
        try:
            def flock_fn(_fd, operation):
                operations.append(operation)
                return 0
            owner = publisher.WcsLifecycleLock(
                lock_path, pidfile_path, flock_fn).acquire()
            assert Path(lock_path).read_text(encoding='ascii') == owner.record
            assert Path(pidfile_path).read_text(encoding='ascii') == owner.record
            owner.release()
            assert operations == [2 | 4, 8]
            assert not Path(pidfile_path).exists()
        finally:
            publisher.process_start_ticks = original_start_ticks


def main() -> int:
    check_resident_epoch_changes_only_with_table()
    check_slow_status_cadence()
    check_wcs_owner_has_no_position_hot_path()
    check_atomic_shm_write_needs_no_fsync()
    check_lifecycle_lock_owns_singleton()
    print('V5_WCS_STATUS_PUBLISHER_TEST_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
