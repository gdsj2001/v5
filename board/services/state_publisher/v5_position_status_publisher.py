#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import errno
import math
import os
import resource
import secrets
import sys
import time

from v5_machine_status_projection import (
    DISPLAY_COORDINATE_SCALE,
    NativeRotaryDisplayProjection,
    write_mock_position_status,
    write_position_status,
)
from v5_polling_cadence import StartToStartPollingCadence
from v5_wcs_status_codec import (
    DEFAULT_INTERVAL_MS,
    DEFAULT_POSITION_PATH,
    POSITION_AXIS_COUNT,
)

HAL_POSITION_COMPONENT = 'v5-position-display'
POSITION_HEARTBEAT_SECONDS = 0.1
DISPLAY_BOUNDARY_HYSTERESIS = 0.0001
POSITION_LOCK_PATH = '/run/8ax/v5_position_status_publisher.lock'
POSITION_PIDFILE_PATH = '/run/8ax/v5_position_status_publisher.pid'


class PosixFlock:
    LOCK_EX = 2
    LOCK_NB = 4
    LOCK_UN = 8

    def __init__(self):
        self._libc = ctypes.CDLL('libc.so.6', use_errno=True)
        self._libc.flock.argtypes = (ctypes.c_int, ctypes.c_int)
        self._libc.flock.restype = ctypes.c_int

    def flock(self, fd, operation):
        while self._libc.flock(int(fd), int(operation)) != 0:
            error_number = ctypes.get_errno()
            if error_number != errno.EINTR:
                raise OSError(error_number, os.strerror(error_number))


DEFAULT_LOCK_MODULE = PosixFlock() if os.name == 'posix' else None


def process_start_ticks(pid=None) -> str:
    pid = os.getpid() if pid is None else int(pid)
    fields = open(f'/proc/{pid}/stat', encoding='ascii').read().split()
    if len(fields) < 22:
        raise RuntimeError('position_publisher_proc_start_unavailable')
    return fields[21]


class PositionLifecycleLock:
    def __init__(self, lock_path, pidfile_path, lock_module=None):
        self.lock_path = lock_path
        self.pidfile_path = pidfile_path
        self._lock_module = (
            DEFAULT_LOCK_MODULE if lock_module is None else lock_module)
        self._fd = None
        self.writer_identity = 0
        self.record = ''

    def acquire(self):
        if self._lock_module is None:
            raise RuntimeError('position_publisher_flock_unavailable')
        os.makedirs(os.path.dirname(self.lock_path), exist_ok=True)
        fd = os.open(self.lock_path, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            self._lock_module.flock(
                fd, self._lock_module.LOCK_EX | self._lock_module.LOCK_NB)
        except OSError as exc:
            os.close(fd)
            if exc.errno in (errno.EACCES, errno.EAGAIN, errno.EWOULDBLOCK):
                raise RuntimeError('position_publisher_already_running') from exc
            raise RuntimeError(
                f'position_publisher_flock_failed:{exc.errno}') from exc
        self._fd = fd
        self.writer_identity = secrets.randbits(32) or 1
        self.record = (
            f'{os.getpid()} {process_start_ticks()} '
            f'{self.writer_identity}\n')
        os.ftruncate(fd, 0)
        os.write(fd, self.record.encode('ascii'))
        os.fsync(fd)
        self._write_pidfile()
        return self

    def _write_pidfile(self):
        os.makedirs(os.path.dirname(self.pidfile_path), exist_ok=True)
        temporary = f'{self.pidfile_path}.{os.getpid()}.tmp'
        with open(temporary, 'w', encoding='ascii') as stream:
            stream.write(self.record)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, self.pidfile_path)

    def release(self):
        if self._fd is None:
            return
        try:
            try:
                with open(self.pidfile_path, encoding='ascii') as stream:
                    owned = stream.read() == self.record
            except OSError:
                owned = False
            if owned:
                os.unlink(self.pidfile_path)
        finally:
            self._lock_module.flock(self._fd, self._lock_module.LOCK_UN)
            os.close(self._fd)
            self._fd = None


class PositionPublishCadence:
    def __init__(self, heartbeat_seconds=POSITION_HEARTBEAT_SECONDS):
        self.heartbeat_seconds = max(0.0, float(heartbeat_seconds))
        self._last_signature = None
        self._last_time = 0.0

    def should_publish(self, name, signature, now=None, heartbeat_seconds=None):
        del name
        now = time.monotonic() if now is None else float(now)
        heartbeat = (
            self.heartbeat_seconds if heartbeat_seconds is None
            else max(0.0, float(heartbeat_seconds)))
        return (
            signature != self._last_signature or
            now - self._last_time >= heartbeat)

    def mark_published(self, name, signature, now=None):
        del name
        self._last_signature = signature
        self._last_time = time.monotonic() if now is None else float(now)


class PositionDisplayStabilizer:
    """Hold one-count boundary noise and confirm adjacent display changes."""

    def __init__(self, coordinate_scale=DISPLAY_COORDINATE_SCALE,
                 boundary_hysteresis=DISPLAY_BOUNDARY_HYSTERESIS):
        self._scale = float(coordinate_scale)
        self._hysteresis = max(0.0, float(boundary_hysteresis)) * self._scale
        self.reset()

    def reset(self):
        field_count = POSITION_AXIS_COUNT * 2
        self._stable = [None] * field_count
        self._candidate = [None] * field_count
        self._last_generation = None

    def _values(self):
        values = [bucket / self._scale for bucket in self._stable]
        return values[:POSITION_AXIS_COUNT], values[POSITION_AXIS_COUNT:]

    def stabilize(self, mcs, cmd, source_generation,
                  source_mcs=None, source_cmd=None):
        if mcs is None or cmd is None:
            self.reset()
            return mcs, cmd
        values = list(mcs) + list(cmd)
        sources = list(source_mcs or mcs) + list(source_cmd or cmd)
        if (len(values) != POSITION_AXIS_COUNT * 2 or
                len(sources) != POSITION_AXIS_COUNT * 2):
            raise RuntimeError('native_position_display_sample_invalid')
        try:
            generation = int(source_generation)
            buckets = [int(round(float(value) * self._scale)) for value in values]
        except (TypeError, ValueError, OverflowError) as exc:
            raise RuntimeError('native_position_source_generation_invalid') from exc
        if generation <= 0 or not all(
                math.isfinite(float(value)) for value in values + sources):
            raise RuntimeError('native_position_display_sample_invalid')
        if self._last_generation is not None:
            if generation == self._last_generation:
                return self._values()
            if generation < self._last_generation:
                self.reset()
            elif generation != self._last_generation + 1:
                self._candidate = [None] * len(self._candidate)
        for index, bucket in enumerate(buckets):
            stable = self._stable[index]
            if stable is None or abs(bucket - stable) > 1:
                self._stable[index] = bucket
                self._candidate[index] = None
            elif bucket == stable:
                self._candidate[index] = None
            elif abs(
                    float(sources[index]) * self._scale -
                    (min(stable, bucket) if max(stable, bucket) <= 0
                     else max(stable, bucket))) + 1.0e-9 < self._hysteresis:
                self._candidate[index] = None
            elif self._candidate[index] == bucket:
                self._stable[index] = bucket
                self._candidate[index] = None
            else:
                self._candidate[index] = bucket
        self._last_generation = generation
        return self._values()


class HalPositionReadAccess:
    """One HAL component owns all 30 Hz display-source pin bindings."""

    def __init__(self, hal_module):
        self._hal = hal_module
        self._component = hal_module.component(HAL_POSITION_COMPONENT)
        self._pin_names = {}
        self._joint_actual_names = [
            f'joint-{joint}-pos-fb' for joint in range(POSITION_AXIS_COUNT)]
        self._joint_command_names = [
            f'joint-{joint}-pos-cmd' for joint in range(POSITION_AXIS_COUNT)]

        native_specs = [
            ('home-table-mapping-valid', 'home-table-valid', 'v5-home-table-valid', hal_module.HAL_BIT),
            ('home-table-map-gen', 'home-table-generation', 'v5-home-table-generation', hal_module.HAL_U32),
            ('home-table-active-mask', 'home-table-active-mask', 'v5-home-table-active-mask', hal_module.HAL_U32),
            ('home-table-commit-seq', 'home-table-commit', 'v5-home-table-commit', hal_module.HAL_U32),
        ]
        joint_fields = (
            ('home-config-valid', 'config-valid', hal_module.HAL_BIT),
            ('home-status-slot', 'status-slot', hal_module.HAL_U32),
            ('home-axis-code', 'axis-code', hal_module.HAL_U32),
            ('home-mapping-generation', 'generation', hal_module.HAL_U32),
            ('home-zero-counts', 'zero', hal_module.HAL_FLOAT),
            ('home-counts-per-unit', 'scale', hal_module.HAL_FLOAT),
        )
        for joint in range(POSITION_AXIS_COUNT):
            suffix = f'{joint:02d}'
            for owner_field, signal_field, pin_type in joint_fields:
                native_specs.append((
                    f'{owner_field}-{suffix}',
                    f'home-j{joint}-{signal_field}',
                    f'v5-home-j{joint}-{signal_field}',
                    pin_type))
        wcheckpoint_fields = (
            ('valid', hal_module.HAL_BIT),
            ('generation', hal_module.HAL_U32),
            ('logical-counts', hal_module.HAL_FLOAT),
            ('base-counts', hal_module.HAL_FLOAT),
            ('runtime-counts', hal_module.HAL_FLOAT),
        )
        for axis in ('a', 'b', 'c'):
            for owner_field, pin_type in wcheckpoint_fields:
                signal_field = (
                    owner_field[:-7]
                    if owner_field.endswith('-counts') else owner_field)
                native_specs.append((
                    f'wcp-{axis}-{owner_field}',
                    f'wcp-{axis}-{signal_field}',
                    f'v5-wcheckpoint-{axis}-{signal_field}',
                    pin_type))
        for owner_name, local_name, signal_name, pin_type in native_specs:
            self._component.newpin(local_name, pin_type, hal_module.HAL_IN)
            hal_module.connect(
                f'{HAL_POSITION_COMPONENT}.{local_name}', signal_name)
            self._pin_names[f'v5-native-hal-owner.{owner_name}'] = local_name

        signal_info = {
            str(item.get('NAME', '')): item
            for item in hal_module.get_info_signals()
        }
        for joint in range(POSITION_AXIS_COUNT):
            self._bind_source(
                signal_info,
                self._joint_actual_names[joint],
                f'joint.{joint}.pos-fb',
                f'v5-position-j{joint}-pos-fb',
                hal_module.HAL_FLOAT)
            self._bind_source(
                signal_info,
                self._joint_command_names[joint],
                f'joint.{joint}.pos-cmd',
                f'v5-position-j{joint}-pos-cmd',
                hal_module.HAL_FLOAT)

        self._scalar_names = {
            'spindle_speed_rps': 'spindle-speed-rps',
            'linear_velocity_per_second': 'linear-velocity-per-second',
            'feed_override_ratio': 'feed-override-ratio',
            'spindle_override_ratio': 'spindle-override-ratio',
        }
        scalar_specs = (
            ('spindle_speed_rps', 'spindle.0.speed-cmd-rps', 'v5-position-spindle-speed-rps'),
            ('linear_velocity_per_second', 'motion.current-vel', 'v5-position-current-vel'),
            ('feed_override_ratio', 'motion.feed-override', 'v5-position-feed-override'),
            ('spindle_override_ratio', 'spindle.0.override', 'v5-position-spindle-override'),
        )
        for key, source_pin, preferred_signal in scalar_specs:
            self._bind_source(
                signal_info,
                self._scalar_names[key],
                source_pin,
                preferred_signal,
                hal_module.HAL_FLOAT)
        self._component.ready()
        self._native_pins = {
            name: self._component.getitem(local_name)
            for name, local_name in self._pin_names.items()
        }
        self._joint_actual_pins = tuple(
            self._component.getitem(name)
            for name in self._joint_actual_names)
        self._joint_command_pins = tuple(
            self._component.getitem(name)
            for name in self._joint_command_names)
        self._scalar_pins = {
            key: self._component.getitem(local_name)
            for key, local_name in self._scalar_names.items()
        }

    def _bind_source(
            self, signal_info, local_name, source_pin, preferred_signal, pin_type):
        self._component.newpin(local_name, pin_type, self._hal.HAL_IN)
        candidates = [
            name for name, item in signal_info.items()
            if item.get('DRIVER') == source_pin]
        if preferred_signal in signal_info:
            signal_name = preferred_signal
        elif len(candidates) == 1:
            signal_name = candidates[0]
        elif not candidates:
            signal_name = preferred_signal
            self._hal.new_sig(signal_name, pin_type)
            self._hal.connect(source_pin, signal_name)
            signal_info[signal_name] = {
                'NAME': signal_name,
                'TYPE': pin_type,
                'DRIVER': source_pin,
            }
        else:
            raise RuntimeError(
                f'native_position_source_signal_ambiguous:{source_pin}')
        signal = signal_info[signal_name]
        if int(signal.get('TYPE', -1)) != int(pin_type):
            raise RuntimeError(
                f'native_position_source_signal_type_invalid:{signal_name}')
        if signal.get('DRIVER') != source_pin:
            raise RuntimeError(
                f'native_position_source_signal_driver_invalid:'
                f'{signal_name}:{signal.get("DRIVER")}')
        self._hal.connect(
            f'{HAL_POSITION_COMPONENT}.{local_name}', signal_name)

    def get_value(self, name):
        return self._native_pins[name].get()

    def read_joint_positions(self):
        actual = [float(pin.get()) for pin in self._joint_actual_pins]
        command = [float(pin.get()) for pin in self._joint_command_pins]
        if not all(math.isfinite(value) for value in actual + command):
            raise RuntimeError('native_joint_position_readback_invalid')
        return actual, command

    def read_display_scalars(self):
        values = tuple(float(self._scalar_pins[key].get()) for key in (
            'spindle_speed_rps',
            'linear_velocity_per_second',
            'feed_override_ratio',
            'spindle_override_ratio',
        ))
        if not all(math.isfinite(value) for value in values):
            raise RuntimeError('native_position_scalar_readback_invalid')
        return (
            values[0] * 60.0,
            values[1] * 60.0,
            values[2] * 100.0,
            values[3] * 100.0,
        )


def lock_process_memory(process_name: str) -> None:
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_MEMLOCK)
        target = hard if hard != resource.RLIM_INFINITY else resource.RLIM_INFINITY
        if soft != target:
            resource.setrlimit(resource.RLIMIT_MEMLOCK, (target, hard))
    except Exception:
        pass
    libc = ctypes.CDLL('libc.so.6', use_errno=True)
    if libc.mlockall(1 | 2) != 0:
        raise SystemExit(
            f'{process_name} mlockall(MCL_CURRENT|MCL_FUTURE) failed: '
            f'errno={ctypes.get_errno()}')


def load_hal():
    dist = '/usr/lib/python3/dist-packages'
    if os.path.isdir(dist) and dist not in sys.path:
        sys.path.insert(0, dist)
    import hal  # type: ignore
    return HalPositionReadAccess(hal)


def parse_offsets(text: str):
    if not text:
        return [0.0] * POSITION_AXIS_COUNT
    return [float(part.strip()) for part in text.split(',') if part.strip()]


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Publish the 30 Hz native position display projection.')
    parser.add_argument('--path', default=DEFAULT_POSITION_PATH)
    parser.add_argument('--interval-ms', type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--mock-mcs', default='')
    parser.add_argument('--mock-cmd-mcs', default='')
    args = parser.parse_args()

    canonical = args.path == DEFAULT_POSITION_PATH
    lifecycle = PositionLifecycleLock(
        POSITION_LOCK_PATH if canonical else f'{args.path}.publisher.lock',
        POSITION_PIDFILE_PATH if canonical else f'{args.path}.publisher.pid')
    try:
        lifecycle.acquire()
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        return 3
    try:
        lock_process_memory('v5_position_status_publisher')

        if args.mock_mcs:
            write_mock_position_status(
                args.path,
                parse_offsets(args.mock_mcs),
                parse_offsets(args.mock_cmd_mcs),
                writer_identity=lifecycle.writer_identity)
            return 0

        hal_access = load_hal()
        rotary_projection = NativeRotaryDisplayProjection(hal_access)
        interval = max(args.interval_ms, 20) / 1000.0
        polling_cadence = StartToStartPollingCadence(interval)
        publish_cadence = PositionPublishCadence()
        display_stabilizer = PositionDisplayStabilizer()
        source_generation = 0
        consecutive_failures = 0
        next_status_log = 0.0
        sample_count = 0
        published_count = 0
        missed_slots = 0
        while True:
            sample_now = time.monotonic()
            try:
                native_positions = hal_access.read_joint_positions()
                native_scalars = hal_access.read_display_scalars()
                source_generation += 1
                published = write_position_status(
                    args.path,
                    None,
                    rotary_projection=rotary_projection,
                    publish_cadence=publish_cadence,
                    now_monotonic=sample_now,
                    native_positions=native_positions,
                    native_scalars=native_scalars,
                    writer_identity=lifecycle.writer_identity,
                    display_stabilizer=display_stabilizer,
                    source_generation=source_generation)
                consecutive_failures = 0
                sample_count += 1
                published_count += 1 if published else 0
                if args.once:
                    return 0
            except Exception as exc:
                display_stabilizer.reset()
                consecutive_failures += 1
                if consecutive_failures == 1 or time.monotonic() >= next_status_log:
                    print(
                    'v5_position_status_publisher sample unavailable: '
                    f'{exc}', file=sys.stderr, flush=True)
                if args.once:
                    return 1
            now = time.monotonic()
            if now >= next_status_log:
                print(
                'v5_position_status_publisher '
                f'samples={sample_count} published={published_count} '
                f'missed_slots={missed_slots}', flush=True)
                sample_count = 0
                published_count = 0
                missed_slots = 0
                next_status_log = now + 5.0
            _, missed = polling_cadence.wait_next()
            missed_slots += missed
    finally:
        lifecycle.release()


if __name__ == '__main__':
    raise SystemExit(main())
