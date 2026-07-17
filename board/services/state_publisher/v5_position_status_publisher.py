#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import math
import os
import resource
import sys
import time

from v5_machine_status_projection import (
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
        return self._component[self._pin_names[name]]

    def read_joint_positions(self):
        actual = [
            float(self._component[name]) for name in self._joint_actual_names]
        command = [
            float(self._component[name]) for name in self._joint_command_names]
        if not all(math.isfinite(value) for value in actual + command):
            raise RuntimeError('native_joint_position_readback_invalid')
        return actual, command

    def read_display_scalars(self):
        values = {
            key: float(self._component[local_name])
            for key, local_name in self._scalar_names.items()
        }
        if not all(math.isfinite(value) for value in values.values()):
            raise RuntimeError('native_position_scalar_readback_invalid')
        return (
            values['spindle_speed_rps'] * 60.0,
            values['linear_velocity_per_second'] * 60.0,
            values['feed_override_ratio'] * 100.0,
            values['spindle_override_ratio'] * 100.0,
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
    lock_process_memory('v5_position_status_publisher')
    parser = argparse.ArgumentParser(
        description='Publish the 30 Hz native position display projection.')
    parser.add_argument('--path', default=DEFAULT_POSITION_PATH)
    parser.add_argument('--interval-ms', type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--mock-mcs', default='')
    parser.add_argument('--mock-cmd-mcs', default='')
    args = parser.parse_args()

    if args.mock_mcs:
        write_mock_position_status(
            args.path,
            parse_offsets(args.mock_mcs),
            parse_offsets(args.mock_cmd_mcs))
        return 0

    hal_access = load_hal()
    rotary_projection = NativeRotaryDisplayProjection(hal_access)
    interval = max(args.interval_ms, 20) / 1000.0
    polling_cadence = StartToStartPollingCadence(interval)
    publish_cadence = PositionPublishCadence()
    consecutive_failures = 0
    next_status_log = 0.0
    sample_count = 0
    published_count = 0
    missed_slots = 0
    while True:
        sample_now = time.monotonic()
        try:
            published = write_position_status(
                args.path,
                None,
                rotary_projection=rotary_projection,
                publish_cadence=publish_cadence,
                now_monotonic=sample_now,
                native_positions=hal_access.read_joint_positions(),
                native_scalars=hal_access.read_display_scalars())
            consecutive_failures = 0
            sample_count += 1
            published_count += 1 if published else 0
            if args.once:
                return 0
        except Exception as exc:
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


if __name__ == '__main__':
    raise SystemExit(main())
