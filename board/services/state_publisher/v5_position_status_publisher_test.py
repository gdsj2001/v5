#!/usr/bin/env python3
from __future__ import annotations

import sys
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


class FakeHalComponent:
    def __init__(self):
        self.is_ready = False
        self.values = {}

    def newpin(self, name, _pin_type, _direction):
        assert not self.is_ready
        self.values[name] = 0

    def ready(self):
        self.is_ready = True

    def __getitem__(self, name):
        assert self.is_ready
        return self.values[name]


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
    assert access.read_joint_positions() == (
        [0.25, 1.25, 2.25, 3.25, 4.25],
        [0.75, 1.75, 2.75, 3.75, 4.75])
    assert access.read_display_scalars() == (120.0, 180.0, 125.0, 80.0)
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


def check_native_position_and_scalars_are_packed() -> None:
    payloads = []
    original_atomic_write = projection.atomic_write
    projection.atomic_write = lambda _path, payload: payloads.append(payload)
    try:
        projection.write_position_status(
            '/dev/null/position',
            None,
            native_positions=(
                [0.25, 1.25, 2.25, 3.25, 4.25],
                [0.75, 1.75, 2.75, 3.75, 4.75]),
            native_scalars=(120.0, 180.0, 125.0, 80.0))
    finally:
        projection.atomic_write = original_atomic_write
    unpacked = codec.POSITION_BLOCK_STRUCT.unpack(payloads[0])
    assert unpacked[7:12] == (0.25, 1.25, 2.25, 3.25, 4.25)
    assert unpacked[12:17] == (0.75, 1.75, 2.75, 3.75, 4.75)
    assert unpacked[17:21] == (120.0, 180.0, 125.0, 80.0)

    try:
        projection.write_position_status(
            '/dev/null/position',
            None,
            native_positions=([0.0] * 5, [0.0] * 5),
            native_scalars=(0.0, float('nan'), 100.0, 100.0))
    except RuntimeError as exc:
        assert str(exc) == 'native_position_scalar_sample_invalid'
    else:
        raise AssertionError('non-finite native scalar was accepted')


def check_moving_sample_publishes_at_each_30hz_generation() -> None:
    cadence = publisher.PositionPublishCadence()
    writes = []
    original_atomic_write = projection.atomic_write
    projection.atomic_write = lambda _path, _payload: writes.append(1)
    try:
        for index in range(31):
            projection.write_position_status(
                '/dev/null/position',
                None,
                publish_cadence=cadence,
                now_monotonic=index / 30.0,
                native_positions=(
                    [float(index), 0.0, 0.0, 0.0, 0.0],
                    [float(index), 0.0, 0.0, 0.0, 0.0]),
                native_scalars=(0.0, 0.0, 100.0, 100.0))
    finally:
        projection.atomic_write = original_atomic_write
    assert len(writes) == 31


def check_fast_owner_has_no_linuxcnc_or_error_channel() -> None:
    text = Path(publisher.__file__).read_text(encoding='utf-8')
    for forbidden in ('import linuxcnc', 'linuxcnc.stat', 'error_channel'):
        assert forbidden not in text
    assert 'motion.feed-override' in text
    assert 'spindle.0.override' in text
    assert 'halui.' not in text


def main() -> int:
    check_hal_position_binding_and_units()
    check_hal_position_reuses_only_source_owned_signal()
    check_start_to_start_polling_cadence()
    check_native_position_and_scalars_are_packed()
    check_moving_sample_publishes_at_each_30hz_generation()
    check_fast_owner_has_no_linuxcnc_or_error_channel()
    print('V5_POSITION_STATUS_PUBLISHER_TEST_OK')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
