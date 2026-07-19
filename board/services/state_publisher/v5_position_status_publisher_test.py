#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import subprocess
import time
import tempfile
import types
from pathlib import Path

import v5_machine_status_projection as projection
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

import v5_position_status_publisher as publisher
from v5_polling_cadence import StartToStartPollingCadence


class CaptureWriter:
    def __init__(self, payloads):
        self.payloads = payloads
        self.sequence = 0

    def next_sequence(self):
        self.sequence += 2
        return self.sequence

    def publish(self, payload, sequence):
        assert sequence == self.sequence and sequence > 0 and sequence & 1 == 0
        self.payloads.append(payload)


class IdentityDisplayProjection:
    def __init__(self, unit_per_count=None):
        self._unit_per_count = list(
            unit_per_count or [0.001] * codec.POSITION_AXIS_COUNT)

    @staticmethod
    def project(mcs, cmd):
        return (
            list(mcs) if mcs is not None else None,
            list(cmd) if cmd is not None else None)

    def display_metadata(self):
        return list(self._unit_per_count), [3] * codec.POSITION_AXIS_COUNT


class FakeHalPin:
    def __init__(self, component, name):
        self.component = component
        self.name = name

    def get(self):
        self.component.pin_get_count += 1
        return self.component.values[self.name]


class FakeHalComponent:
    def __init__(self):
        self.is_ready = False
        self.values = {}
        self.getitem_calls = []
        self.pin_get_count = 0
        self.direct_value_reads = 0

    def newpin(self, name, _pin_type, _direction):
        assert not self.is_ready
        self.values[name] = 0

    def ready(self):
        self.is_ready = True

    def __getitem__(self, name):
        assert self.is_ready
        self.direct_value_reads += 1
        return self.values[name]

    def getitem(self, name):
        assert self.is_ready
        assert name in self.values
        self.getitem_calls.append(name)
        return FakeHalPin(self, name)


class FakeHalModule:
    HAL_BIT = 1
    HAL_U32 = 2
    HAL_FLOAT = 3
    HAL_IN = 4

    def __init__(self):
        self.created_name = None
        self.component_instance = FakeHalComponent()
        self.connections = {}
        self.signals = {}

    def component(self, name):
        self.created_name = name
        return self.component_instance

    def connect(self, pin_name, signal_name):
        assert not self.component_instance.is_ready
        self.connections[pin_name] = signal_name
        signal = self.signals.get(signal_name)
        if signal is not None and not pin_name.startswith(
                f'{publisher.HAL_POSITION_COMPONENT}.'):
            signal['DRIVER'] = pin_name

    def get_info_signals(self):
        return list(self.signals.values())

    def new_sig(self, name, signal_type):
        assert name not in self.signals
        self.signals[name] = {
            'NAME': name,
            'TYPE': signal_type,
            'DRIVER': None,
        }


def check_hal_position_binding_and_units() -> None:
    module = FakeHalModule()
    access = publisher.HalPositionReadAccess(module)
    assert module.created_name == publisher.HAL_POSITION_COMPONENT
    component = module.component_instance
    for joint in range(codec.POSITION_AXIS_COUNT):
        component.values[access._joint_actual_names[joint]] = joint + 0.25
        component.values[access._joint_command_names[joint]] = joint + 0.75
        assert module.signals[f'v5-position-j{joint}-pos-fb']['DRIVER'] == (
            f'joint.{joint}.pos-fb')
        assert module.signals[f'v5-position-j{joint}-pos-cmd']['DRIVER'] == (
            f'joint.{joint}.pos-cmd')
    component.values[access._scalar_names['spindle_speed_rps']] = 2.0
    component.values[access._scalar_names['linear_velocity_per_second']] = 3.0
    component.values[access._scalar_names['feed_override_ratio']] = 1.25
    component.values[access._scalar_names['spindle_override_ratio']] = 0.8
    cached_pin_count = len(component.getitem_calls)
    assert cached_pin_count == len(component.values)
    assert set(component.getitem_calls) == set(component.values)
    assert access.read_joint_positions() == (
        [0.25, 1.25, 2.25, 3.25, 4.25],
        [0.75, 1.75, 2.75, 3.75, 4.75])
    assert access.read_display_scalars() == (120.0, 180.0, 125.0, 80.0)
    assert access.get_value(
        'v5-native-hal-owner.home-table-mapping-valid') == 0
    assert len(component.getitem_calls) == cached_pin_count
    assert component.direct_value_reads == 0
    assert component.pin_get_count == 15
    assert module.signals['v5-position-spindle-speed-rps']['DRIVER'] == (
        'spindle.0.speed-cmd-rps')
    assert module.signals['v5-position-current-vel']['DRIVER'] == (
        'motion.current-vel')
    assert module.signals['v5-position-feed-override']['DRIVER'] == (
        'motion.feed-override')
    assert module.signals['v5-position-spindle-override']['DRIVER'] == (
        'spindle.0.override')


def check_hal_position_reuses_only_source_owned_signal() -> None:
    module = FakeHalModule()
    module.signals['existing-j0-fb'] = {
        'NAME': 'existing-j0-fb',
        'TYPE': module.HAL_FLOAT,
        'DRIVER': 'joint.0.pos-fb',
    }
    access = publisher.HalPositionReadAccess(module)
    assert module.connections[
        f'{publisher.HAL_POSITION_COMPONENT}.'
        f'{access._joint_actual_names[0]}'] == 'existing-j0-fb'
    assert 'v5-position-j0-pos-fb' not in module.signals

    wrong_driver = FakeHalModule()
    wrong_driver.signals['v5-position-j0-pos-fb'] = {
        'NAME': 'v5-position-j0-pos-fb',
        'TYPE': wrong_driver.HAL_FLOAT,
        'DRIVER': 'other-owner.position',
    }
    try:
        publisher.HalPositionReadAccess(wrong_driver)
    except RuntimeError as exc:
        assert str(exc).startswith(
            'native_position_source_signal_driver_invalid:')
    else:
        raise AssertionError('wrong HAL signal driver was accepted')


def check_bus_status_uses_committed_axis_mapping_once() -> None:
    module = FakeHalModule()
    access = publisher.HalPositionReadAccess(module)
    component = module.component_instance
    local_names = access._bus_status._local_names
    assert all(
        len(f'{publisher.HAL_POSITION_COMPONENT}.{local_name}') <= 47
        for local_name in local_names.values())

    def set_value(key, value):
        component.values[local_names[key]] = value

    set_value('table_valid', 1)
    set_value('table_generation', 23)
    set_value('table_active_mask', 0x1f)
    set_value('router_valid', 1)
    set_value('router_generation', 23)
    set_value('router_active_mask', 0x1f)
    set_value('master_link_up', 1)
    set_value('master_state_op', 1)
    set_value('master_all_op', 1)
    set_value('slaves_responding', 5)
    for joint, axis in enumerate('XYZBC'):
        set_value(f'joint_{joint}_valid', 1)
        set_value(f'joint_{joint}_generation', 23)
        set_value(f'joint_{joint}_axis_code', ord(axis))
        set_value(f'joint_{joint}_slave_position', joint)
        set_value(f'slave_{joint}_statusword', 0x16b7)

    snapshot = access.read_bus_status()
    assert snapshot['valid']
    assert snapshot['mapping_generation'] == 23
    assert snapshot['active_mask'] == 0x1f
    assert snapshot['master_flags'] == (
        codec.BUS_MASTER_LINK_UP |
        codec.BUS_MASTER_STATE_OP |
        codec.BUS_MASTER_ALL_OP)
    assert snapshot['slaves_responding'] == 5
    assert ''.join(chr(entry['axis_code']) for entry in snapshot['entries']) == (
        'XYZBC')
    assert [entry['slave_position'] for entry in snapshot['entries']] == (
        [0, 1, 2, 3, 4])
    assert [entry['statusword'] for entry in snapshot['entries']] == (
        [0x16b7] * 5)
    assert all(
        entry['flags'] == codec.BUS_JOINT_SLAVE_OP
        for entry in snapshot['entries'])

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'bus.bin'
        writer = codec.BusStatusMmapWriter(str(path)).open()
        try:
            writer.publish(snapshot, 17, 9, time.monotonic_ns())
            first_inode = path.stat().st_ino
            payload = path.read_bytes()
            values = codec.BUS_STATUS_STRUCT.unpack(payload)
            assert values[0] == codec.BUS_STATUS_MAGIC
            assert values[1] == codec.BUS_STATUS_VERSION
            assert values[4] == 2
            assert values[5] == 17
            assert values[6] == 23
            assert values[11] == 9
            assert values[-2] == codec.crc32_like(
                payload[:codec.BUS_STATUS_CRC_OFFSET])
            writer.publish(snapshot, 17, 10, time.monotonic_ns())
            assert path.stat().st_ino == first_inode
            assert codec.BUS_STATUS_STRUCT.unpack(
                path.read_bytes())[4] == 4
        finally:
            writer.close()

    set_value('router_generation', 24)
    try:
        access.read_bus_status()
    except RuntimeError as exc:
        assert str(exc) == 'native_bus_status_mapping_not_committed'
    else:
        raise AssertionError('mismatched bus mapping generation was accepted')


def check_start_to_start_polling_cadence() -> None:
    class FakeClock:
        def __init__(self, overshoot=0.0):
            self.now = 0.0
            self.sleeps = []
            self.overshoot = overshoot

        def monotonic(self):
            return self.now

        def sleep(self, seconds):
            assert seconds > 0.0
            self.sleeps.append(seconds)
            self.now += seconds + self.overshoot

    clock = FakeClock()
    cadence = StartToStartPollingCadence(
        1.0 / 30.0, clock=clock.monotonic, sleeper=clock.sleep)
    starts = []
    for _ in range(31):
        starts.append(clock.now)
        clock.now += 0.005
        cadence.wait_next()
    assert abs(starts[-1] - 1.0) <= 1.0e-9

    overrun_clock = FakeClock()
    overrun = StartToStartPollingCadence(
        1.0 / 30.0,
        clock=overrun_clock.monotonic,
        sleeper=overrun_clock.sleep)
    overrun_clock.now = 0.05
    _, missed = overrun.wait_next()
    assert missed == 1
    assert not overrun_clock.sleeps
    overrun_clock.now += 0.005
    overrun.wait_next()
    assert abs(overrun_clock.now - (0.05 + 1.0 / 30.0)) <= 1.0e-9

    oversleep_clock = FakeClock(overshoot=0.004)
    oversleep = StartToStartPollingCadence(
        1.0 / 30.0,
        clock=oversleep_clock.monotonic,
        sleeper=oversleep_clock.sleep)
    oversleep_starts = []
    for _ in range(3):
        oversleep_starts.append(oversleep_clock.now)
        oversleep_clock.now += 0.005
        oversleep.wait_next()
    assert abs(
        (oversleep_starts[2] - oversleep_starts[1]) -
        (1.0 / 30.0 + 0.004)) <= 1.0e-9


def check_display_stabilizer_generation_rules() -> None:
    def point(y, c):
        return [0.0, y, 0.0, 0.0, c]

    stabilizer = publisher.PositionDisplayStabilizer()
    mcs, cmd = stabilizer.stabilize(
        point(29.990, 359.217), point(29.990, 359.217), 1)
    assert mcs == point(29.990, 359.217)
    assert cmd == point(29.990, 359.217)

    mcs, _ = stabilizer.stabilize(
        point(29.991, 359.218), point(29.991, 359.218), 2,
        point(29.9905, 359.2175), point(29.9905, 359.2175))
    assert mcs == point(29.990, 359.217)
    mcs, _ = stabilizer.stabilize(
        point(29.991, 359.218), point(29.991, 359.218), 2,
        point(29.9905, 359.2175), point(29.9905, 359.2175))
    assert mcs == point(29.990, 359.217)
    stabilizer.stabilize(
        point(29.990, 359.217), point(29.990, 359.217), 3)
    stabilizer.stabilize(
        point(29.991, 359.218), point(29.991, 359.218), 4,
        point(29.9906, 359.2176), point(29.9906, 359.2176))
    mcs, _ = stabilizer.stabilize(
        point(29.991, 359.218), point(29.991, 359.218), 5,
        point(29.9907, 359.2177), point(29.9907, 359.2177))
    assert mcs == point(29.990, 359.217)
    mcs, _ = stabilizer.stabilize(
        point(29.991, 359.218), point(29.991, 359.218), 6,
        point(29.9907, 359.2177), point(29.9907, 359.2177))
    assert mcs == point(29.991, 359.218)

    mcs, _ = stabilizer.stabilize(
        point(29.994, 0.0), point(29.994, 0.0), 7)
    assert mcs == point(29.994, 0.0)
    stabilizer.stabilize(None, None, 7)
    mcs, _ = stabilizer.stabilize(
        point(-1.001, 359.999), point(-1.001, 359.999), 8)
    assert mcs == point(-1.001, 359.999)
    mcs, _ = stabilizer.stabilize(
        point(-1.002, 359.998), point(-1.002, 359.998), 1)
    assert mcs == point(-1.002, 359.998)
    mcs, _ = stabilizer.stabilize(
        point(-1.003, 359.997), point(-1.003, 359.997), 2,
        point(-1.0025, 359.9975), point(-1.0025, 359.9975))
    assert mcs == point(-1.002, 359.998)
    stabilizer.stabilize(
        point(-1.003, 359.997), point(-1.003, 359.997), 3,
        point(-1.0026, 359.9974), point(-1.0026, 359.9974))
    mcs, _ = stabilizer.stabilize(
        point(-1.003, 359.997), point(-1.003, 359.997), 4,
        point(-1.0027, 359.9973), point(-1.0027, 359.9973))
    assert mcs == point(-1.002, 359.998)
    mcs, _ = stabilizer.stabilize(
        point(-1.003, 359.997), point(-1.003, 359.997), 5,
        point(-1.0027, 359.9973), point(-1.0027, 359.9973))
    assert mcs == point(-1.003, 359.997)


def check_display_stabilizer_uses_one_native_pulse() -> None:
    def point(y):
        return [0.0, y, 0.0, 0.0, 0.0]

    unit_per_count = [0.001, 0.010, 0.001, 0.001, 0.001]
    stabilizer = publisher.PositionDisplayStabilizer()
    mcs, _ = stabilizer.stabilize(
        point(29.990), point(29.990), 1,
        point(29.990), point(29.990), unit_per_count)
    assert mcs == point(29.990)

    for generation in (2, 3, 4):
        mcs, _ = stabilizer.stabilize(
            point(29.991), point(29.991), generation,
            point(29.995), point(29.995), unit_per_count)
        assert mcs == point(29.990)

    stabilizer.stabilize(
        point(29.991), point(29.991), 5,
        point(30.005), point(30.005), unit_per_count)
    mcs, _ = stabilizer.stabilize(
        point(29.991), point(29.991), 6,
        point(30.005), point(30.005), unit_per_count)
    assert mcs == point(29.991)


def check_display_stabilizer_suppresses_boundary_writes() -> None:
    cadence = publisher.PositionPublishCadence()
    stabilizer = publisher.PositionDisplayStabilizer()
    payloads = []
    writer = CaptureWriter(payloads)
    samples = (
        (29.9904, 359.2174),
        (29.9910, 359.2180),
        (29.9904, 359.2174),
        (29.9911, 359.2181),
        (29.9912, 359.2182),
        (29.9912, 359.2182),
        (29.9941, 0.0001),
    )
    for generation, (y, c) in enumerate(samples, 1):
        values = [0.0, y, 0.0, 0.0, c]
        projection.write_position_status(
            '/dev/null/position', None, writer,
            rotary_projection=IdentityDisplayProjection(),
            publish_cadence=cadence,
            now_monotonic=(generation - 1) * 0.01,
            native_positions=(values, values),
            native_scalars=(0.0, 0.0, 100.0, 100.0),
            display_stabilizer=stabilizer,
            source_generation=generation)
    assert len(payloads) == 2
    unpacked = [codec.POSITION_BLOCK_STRUCT.unpack(payload) for payload in payloads]
    assert (unpacked[0][11], unpacked[0][14]) == (29.990, 359.217)
    assert (unpacked[1][11], unpacked[1][14]) == (29.994, 0.0)


def check_display_stabilizer_filters_rotary_wrap_one_count() -> None:
    def point(c):
        return [0.0, 0.0, 0.0, 0.0, c]

    stabilizer = publisher.PositionDisplayStabilizer()
    mcs, cmd = stabilizer.stabilize(
        point(360.0), point(360.0), 1,
        point(359.9999), point(359.9999))
    assert mcs == point(0.0) and cmd == point(0.0)


def check_display_stabilizer_does_not_mask_multibucket_changes() -> None:
    def point(b):
        return [0.0, 0.0, 0.0, b, 0.0]

    quanta = [0.0001] * codec.POSITION_AXIS_COUNT
    stabilizer = publisher.PositionDisplayStabilizer()
    mcs, cmd = stabilizer.stabilize(
        point(36.578), point(36.578), 1,
        point(36.5775), point(36.5775), quanta)
    assert mcs == point(36.578) and cmd == point(36.578)

    mcs, cmd = stabilizer.stabilize(
        point(36.580), point(36.580), 2,
        point(36.5803), point(36.5803), quanta)
    assert mcs == point(36.580) and cmd == point(36.580)
    mcs, cmd = stabilizer.stabilize(
        point(36.578), point(36.578), 3,
        point(36.5775), point(36.5775), quanta)
    assert mcs == point(36.578) and cmd == point(36.578)

    stabilizer.reset()
    mcs, cmd = stabilizer.stabilize(
        point(0.0), point(0.0), 1, point(0.0), point(0.0))
    assert mcs == point(0.0) and cmd == point(0.0)

    mcs, cmd = stabilizer.stabilize(
        point(360.0), point(360.0), 2,
        point(359.9999), point(359.9999))
    assert mcs == point(0.0) and cmd == point(0.0)
    mcs, cmd = stabilizer.stabilize(
        point(0.0), point(0.0), 3, point(0.0), point(0.0))
    assert mcs == point(0.0) and cmd == point(0.0)

    mcs, cmd = stabilizer.stabilize(
        point(360.0), point(360.0), 4,
        point(359.9995), point(359.9995))
    assert mcs == point(0.0) and cmd == point(0.0)
    mcs, cmd = stabilizer.stabilize(
        point(0.0), point(0.0), 5, point(0.0), point(0.0))
    assert mcs == point(0.0) and cmd == point(0.0)

    stabilizer.stabilize(
        point(359.999), point(359.999), 6,
        point(359.9993), point(359.9993))
    mcs, cmd = stabilizer.stabilize(
        point(359.999), point(359.999), 7,
        point(359.9993), point(359.9993))
    assert mcs == point(359.999) and cmd == point(359.999)


def check_native_position_and_scalars_are_packed() -> None:
    payloads = []
    writer = CaptureWriter(payloads)
    projection.write_position_status(
        '/dev/null/position', None, writer,
        native_positions=(
            [0.25, 1.25, 2.25, 3.25, 4.25],
            [0.75, 1.75, 2.75, 3.75, 4.75]),
        native_scalars=(120.0, 180.0, 125.0, 80.0))
    unpacked = codec.POSITION_BLOCK_STRUCT.unpack(payloads[0])
    assert unpacked[10:15] == (0.25, 1.25, 2.25, 3.25, 4.25)
    assert unpacked[15:20] == (0.75, 1.75, 2.75, 3.75, 4.75)
    assert unpacked[20:25] == (0.001,) * 5
    assert unpacked[25:30] == (-0.5,) * 5
    assert unpacked[30:35] == (3,) * 5
    assert unpacked[35:39] == (120.0, 180.0, 125.0, 80.0)

    payloads.clear()
    projection.write_position_status(
        '/dev/null/position', None, writer,
        native_positions=([0.0] * 5, [0.0] * 5),
        native_scalars=(0.0, 0.0, 100.0, 100.0),
        writer_identity=0x1234abcd)
    assert codec.POSITION_BLOCK_STRUCT.unpack(payloads[0])[5] == 0x1234abcd

    try:
        projection.write_position_status(
            '/dev/null/position', None, writer,
            native_positions=([0.0] * 5, [0.0] * 5),
            native_scalars=(0.0, float('nan'), 100.0, 100.0))
    except RuntimeError as exc:
        assert str(exc) == 'native_position_scalar_sample_invalid'
    else:
        raise AssertionError('non-finite native scalar was accepted')


def check_persistent_writer_keeps_inode_and_seqlock() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'position.bin'
        writer = publisher.PositionStatusMmapWriter(str(path)).open()
        try:
            first_inode = path.stat().st_ino
            for generation in (1, 2):
                projection.write_position_status(
                    str(path), None,
                    native_positions=([float(generation)] * 5,) * 2,
                    native_scalars=(0.0, 0.0, 100.0, 100.0),
                    writer_identity=17,
                    source_generation=generation,
                    source_acquired_mono_ns=time.monotonic_ns(),
                    position_writer=writer)
                raw = path.read_bytes()
                values = codec.POSITION_BLOCK_STRUCT.unpack(raw)
                assert values[6] > 0 and values[6] & 1 == 0
                assert values[9] == generation
                assert values[-2] == codec.crc32_like(raw[:-8])
            assert path.stat().st_ino == first_inode
            assert writer.open_count == 1
        finally:
            writer.close()


def check_moving_sample_publishes_at_each_30hz_generation() -> None:
    cadence = publisher.PositionPublishCadence()
    stabilizer = publisher.PositionDisplayStabilizer()
    writes = []
    writer = CaptureWriter(writes)
    for index in range(31):
        projection.write_position_status(
            '/dev/null/position', None, writer,
            rotary_projection=IdentityDisplayProjection(),
            publish_cadence=cadence,
            now_monotonic=index / 30.0,
            native_positions=(
                [float(index), 0.0, 0.0, 0.0, 0.0],
                [float(index), 0.0, 0.0, 0.0, 0.0]),
            native_scalars=(0.0, 0.0, 100.0, 100.0),
            display_stabilizer=stabilizer,
            source_generation=index + 1)
    assert len(writes) == 31


def check_fast_owner_has_no_linuxcnc_or_error_channel() -> None:
    text = Path(publisher.__file__).read_text(encoding='utf-8')
    for forbidden in ('import linuxcnc', 'linuxcnc.stat', 'error_channel'):
        assert forbidden not in text
    assert 'motion.feed-override' in text
    assert 'spindle.0.override' in text
    assert 'halui.' not in text


class FakeFlock:
    LOCK_EX = 1
    LOCK_NB = 2
    LOCK_UN = 4

    def __init__(self):
        self.locked_inodes = set()

    def flock(self, fd, operation):
        inode = __import__('os').fstat(fd).st_ino
        if operation == self.LOCK_UN:
            self.locked_inodes.discard(inode)
            return
        if inode in self.locked_inodes:
            raise BlockingIOError(__import__('errno').EAGAIN, 'busy')
        self.locked_inodes.add(inode)


def check_lifecycle_lock_owns_singleton_not_pidfile() -> None:
    original_start_ticks = publisher.process_start_ticks
    publisher.process_start_ticks = lambda pid=None: '101'
    try:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            lock_path = str(root / 'position.lock')
            pidfile = root / 'position.pid'
            backend = FakeFlock()
            first = publisher.PositionLifecycleLock(
                lock_path, str(pidfile), backend).acquire()
            assert first.writer_identity != 0
            assert pidfile.read_text(encoding='ascii') == first.record

            for diagnostic in (
                    None,
                    '999999 1 7\n',       # stale PID
                    f'{__import__("os").getpid()} 1 7\n'):  # PID reuse
                if diagnostic is None:
                    pidfile.unlink(missing_ok=True)  # PIDFILE missing/orphan
                else:
                    pidfile.write_text(diagnostic, encoding='ascii')
                second = publisher.PositionLifecycleLock(
                    lock_path, str(pidfile), backend)
                try:
                    second.acquire()
                except RuntimeError as exc:
                    assert str(exc) == 'position_publisher_already_running'
                else:
                    raise AssertionError('concurrent publisher acquired lock')
                assert first._fd is not None

            pidfile.write_text(first.record, encoding='ascii')
            first.release()
            assert not pidfile.exists()
            recovered = publisher.PositionLifecycleLock(
                lock_path, str(pidfile), backend).acquire()
            recovered.release()
    finally:
        publisher.process_start_ticks = original_start_ticks


def check_lock_backend_has_no_python_fcntl_dependency() -> None:
    text = Path(publisher.__file__).read_text(encoding='utf-8')
    assert 'import fcntl' not in text
    assert "ctypes.CDLL('libc.so.6'" in text
    assert 'DEFAULT_LOCK_MODULE = PosixFlock()' in text


def check_lock_backend_error_is_not_reported_as_contention() -> None:
    class BrokenFlock(FakeFlock):
        def flock(self, fd, operation):
            del fd, operation
            raise OSError(__import__('errno').EIO, 'backend failure')

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        lifecycle = publisher.PositionLifecycleLock(
            str(root / 'position.lock'), str(root / 'position.pid'),
            BrokenFlock())
        try:
            lifecycle.acquire()
        except RuntimeError as exc:
            assert str(exc) == 'position_publisher_flock_failed:5'
        else:
            raise AssertionError('flock backend failure was accepted')


def check_posix_kernel_releases_orphaned_flock() -> None:
    if __import__('os').name != 'posix':
        return
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        lock_path = root / 'position.lock'
        pidfile = root / 'position.pid'
        ready = root / 'ready'
        child_code = '''
import sys
import time
from pathlib import Path
import v5_position_status_publisher as publisher
lock = publisher.PositionLifecycleLock(sys.argv[1], sys.argv[2]).acquire()
Path(sys.argv[3]).write_text(lock.record, encoding="ascii")
while True:
    time.sleep(1)
'''
        environment = __import__('os').environ.copy()
        environment['PYTHONPATH'] = str(Path(__file__).parent)
        child = subprocess.Popen(
            [sys.executable, '-c', child_code,
             str(lock_path), str(pidfile), str(ready)],
            env=environment)
        try:
            deadline = time.monotonic() + 5.0
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert ready.exists() and child.poll() is None
            for diagnostic in (
                    None,
                    '999999 1 7\n',
                    f'{__import__("os").getpid()} 1 7\n'):
                if diagnostic is None:
                    pidfile.unlink(missing_ok=True)
                else:
                    pidfile.write_text(diagnostic, encoding='ascii')
                contender = publisher.PositionLifecycleLock(
                    str(lock_path), str(pidfile))
                try:
                    contender.acquire()
                except RuntimeError as exc:
                    assert str(exc) == 'position_publisher_already_running'
                else:
                    raise AssertionError('kernel flock allowed second process')
            child.terminate()
            child.wait(timeout=5)
            recovered = publisher.PositionLifecycleLock(
                str(lock_path), str(pidfile)).acquire()
            recovered.release()
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)


def check_init_uses_lock_identity_not_pidfile_authority() -> None:
    init_path = Path(__file__).parent / 'init.d' / 'v5-position-status-publisher'
    text = init_path.read_text(encoding='utf-8')
    assert 'owner_is_live()' in text
    assert 'RUNTIME_MODULE_ROOT=/usr/libexec/8ax' in text
    assert 'PYTHONPATH=$RUNTIME_MODULE_ROOT:' in text
    assert 'command -v python3' in text
    assert 'BUS_STATUS_PATH=${V5_BUS_STATUS_PATH:-/dev/shm/v5_native_bus_status.bin}' in text
    assert '--bus-path "$BUS_STATUS_PATH"' in text
    assert 'bus_block_matches_owner' in text
    assert 'flock -n "$LOCKFILE" true' in text
    assert '/proc/$OWNER_PID/stat' in text
    assert '/proc/$OWNER_PID/cmdline' in text
    assert 'kill -0' not in text
    assert 'echo "$!" >"$PIDFILE"' not in text
    assert 'rm -f "$PIDFILE" "$STATUS_PATH"' not in text


def check_init_requires_matching_canonical_writer_identity() -> None:
    init_path = Path(__file__).parent / 'init.d' / 'v5-position-status-publisher'
    text = init_path.read_text(encoding='utf-8')
    start = text.index('position_block_matches_owner() {')
    end = text.index('\nstart_service() {', start)
    function = text[start:end]
    bus_start = text.index('bus_block_matches_owner() {', start)
    bus_function = text[bus_start:end]
    start_body = text[end:text.index('\nstop_service() {', end)]
    assert text.count('set_position_affinity') == 0
    assert 'position_block_matches_owner' not in start_body
    assert 'while ' not in start_body and 'sleep ' not in start_body
    with tempfile.TemporaryDirectory() as directory:
        status_path = Path(directory) / 'position.bin'
        writer = publisher.PositionStatusMmapWriter(str(status_path)).open()
        try:
            projection.write_position_status(
                str(status_path), None, writer,
                native_positions=([0.0] * 5, [0.0] * 5),
                native_scalars=(0.0, 0.0, 100.0, 100.0),
                writer_identity=17)
        finally:
            writer.close()
        environment = __import__('os').environ.copy()
        installed_modules = Path(directory) / 'usr' / 'libexec' / '8ax'
        installed_modules.mkdir(parents=True)
        __import__('shutil').copy2(
            Path(codec.__file__), installed_modules / 'v5_wcs_status_codec.py')
        environment['PYTHONPATH'] = str(installed_modules)
        python_shim = Path(directory) / 'python3'
        python_shim.write_text(
            f'#!/bin/sh\nexec "{Path(sys.executable).as_posix()}" "$@"\n',
            encoding='utf-8')
        python_shim.chmod(0o755)
        environment['PATH'] = (
            f'{Path(directory).as_posix()}:' + environment.get('PATH', ''))
        command = (
            f'STATUS_PATH="{status_path.as_posix()}"; OWNER_WRITER=18; '
            f'{function}\nposition_block_matches_owner')
        mismatch = subprocess.run(
            ['sh', '-c', command], env=environment, check=False)
        assert mismatch.returncode != 0
        command = (
            f'STATUS_PATH="{status_path.as_posix()}"; OWNER_WRITER=17; '
            f'{function}\nposition_block_matches_owner')
        match = subprocess.run(
            ['sh', '-c', command], env=environment, check=False)
        assert match.returncode == 0

        bus_path = Path(directory) / 'bus.bin'
        bus_writer = codec.BusStatusMmapWriter(str(bus_path)).open()
        try:
            bus_writer.publish({
                'valid': True,
                'mapping_generation': 3,
                'active_mask': 0x1f,
                'master_flags': (
                    codec.BUS_MASTER_LINK_UP |
                    codec.BUS_MASTER_STATE_OP |
                    codec.BUS_MASTER_ALL_OP),
                'slaves_responding': 5,
                'entries': [{
                    'valid': True,
                    'axis_code': ord(axis),
                    'slave_position': index,
                    'flags': codec.BUS_JOINT_SLAVE_OP,
                    'statusword': 0x16b7,
                } for index, axis in enumerate('XYZBC')],
            }, 17, 1, time.monotonic_ns())
        finally:
            bus_writer.close()
        command = (
            f'BUS_STATUS_PATH="{bus_path.as_posix()}"; OWNER_WRITER=18; '
            f'{bus_function}\nbus_block_matches_owner')
        mismatch = subprocess.run(
            ['sh', '-c', command], env=environment, check=False)
        assert mismatch.returncode != 0
        command = (
            f'BUS_STATUS_PATH="{bus_path.as_posix()}"; OWNER_WRITER=17; '
            f'{bus_function}\nbus_block_matches_owner')
        match = subprocess.run(
            ['sh', '-c', command], env=environment, check=False)
        assert match.returncode == 0


def check_runtime_policy_rejects_writer_identity_unbinding() -> None:
    board_root = Path(__file__).resolve().parents[2]
    policy_path = board_root / 'tools' / 'deploy' / 'check_v5_runtime_policy.py'
    spec = importlib.util.spec_from_file_location('v5_runtime_policy_position_test', policy_path)
    assert spec and spec.loader
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    init_text = (Path(__file__).parent / 'init.d' /
                 'v5-position-status-publisher').read_text(encoding='utf-8')
    assert policy.position_status_writer_identity_bound(init_text)
    assert policy.bus_status_writer_identity_bound(init_text)
    mutated = init_text.replace('values[5] != int(sys.argv[2])',
                                'values[5] == int(sys.argv[2])', 1)
    assert not policy.position_status_writer_identity_bound(mutated)
    bus_start = init_text.index('bus_block_matches_owner() {')
    bus_mutated = (
        init_text[:bus_start] +
        init_text[bus_start:].replace(
            'values[5] != int(sys.argv[2])',
            'values[5] == int(sys.argv[2])',
            1))
    assert not policy.bus_status_writer_identity_bound(bus_mutated)


def main() -> int:
    check_hal_position_binding_and_units()
    check_hal_position_reuses_only_source_owned_signal()
    check_bus_status_uses_committed_axis_mapping_once()
    check_start_to_start_polling_cadence()
    check_display_stabilizer_generation_rules()
    check_display_stabilizer_uses_one_native_pulse()
    check_display_stabilizer_suppresses_boundary_writes()
    check_display_stabilizer_filters_rotary_wrap_one_count()
    check_display_stabilizer_does_not_mask_multibucket_changes()
    check_native_position_and_scalars_are_packed()
    check_persistent_writer_keeps_inode_and_seqlock()
    check_moving_sample_publishes_at_each_30hz_generation()
    check_fast_owner_has_no_linuxcnc_or_error_channel()
    check_lifecycle_lock_owns_singleton_not_pidfile()
    check_lock_backend_has_no_python_fcntl_dependency()
    check_lock_backend_error_is_not_reported_as_contention()
    check_posix_kernel_releases_orphaned_flock()
    check_init_uses_lock_identity_not_pidfile_authority()
    check_init_requires_matching_canonical_writer_identity()
    check_runtime_policy_rejects_writer_identity_unbinding()
    print('V5_POSITION_STATUS_PUBLISHER_TEST_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
