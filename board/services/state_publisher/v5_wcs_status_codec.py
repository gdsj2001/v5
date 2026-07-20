from __future__ import annotations

import mmap
import os
import struct
import time
from typing import Tuple

MAGIC = 0x56574353
POSITION_MAGIC = 0x56504F53
MODAL_TOOL_MAGIC = 0x564D544C
WCS_VERSION = 2
POSITION_STATUS_VERSION = 3
MODAL_TOOL_VERSION = 4
WCS_COUNT = 9
WCS_AXIS_COUNT = 5
WCS_TABLE_VALUE_COUNT = WCS_COUNT * WCS_AXIS_COUNT
OFFSET_COUNT = WCS_AXIS_COUNT
POSITION_AXIS_COUNT = 5
ROTARY_AXIS_INDICES = (3, 4)
ROTARY_FULL_TURN_DEG = 360.0
WCS_PARAM_BASES = (5221, 5241, 5261, 5281, 5301, 5321, 5341, 5361, 5381)
WCS_PARAM_AXIS_OFFSETS = (0, 1, 2, 3, 5)
BLOCK_STRUCT = struct.Struct('<IIIIiIIIIIQ' + ('d' * WCS_TABLE_VALUE_COUNT) + 'II')
POSITION_BLOCK_STRUCT = struct.Struct(
    '<IIIIIIIIQQ' +
    ('d' * (POSITION_AXIS_COUNT * 4)) +
    ('B' * POSITION_AXIS_COUNT) + '3x' +
    ('d' * 4) + 'II')
POSITION_SEQ_OFFSET = 24
MODAL_TOOL_BLOCK_STRUCT = struct.Struct('<IIIIIIIIIiIIQ128sdIIIiIiIIi128sIII')
DEFAULT_PATH = '/dev/shm/v5_native_wcs_status.bin'
DEFAULT_POSITION_PATH = '/dev/shm/v5_native_position_status.bin'
DEFAULT_MODAL_TOOL_PATH = '/dev/shm/v5_native_modal_tool_status.bin'
DEFAULT_INTERVAL_MS = 33
DEFAULT_T0_TOOL_HOLDER_LENGTH_MM = 15.0

BUS_STATUS_MAGIC = 0x56425553
BUS_STATUS_VERSION = 1
BUS_JOINT_COUNT = 5
BUS_STATUS_INTERVAL_SECONDS = 0.2
BUS_STATUS_DEFAULT_PATH = '/dev/shm/v5_native_bus_status.bin'
BUS_STATUS_STRUCT = struct.Struct(
    '<12IQ' + ('IIIII' * BUS_JOINT_COUNT) + 'II')
BUS_STATUS_SEQUENCE_OFFSET = 16
BUS_STATUS_CRC_OFFSET = BUS_STATUS_STRUCT.size - 8

BUS_MASTER_LINK_UP = 1 << 0
BUS_MASTER_STATE_OP = 1 << 1
BUS_MASTER_ALL_OP = 1 << 2
BUS_JOINT_SLAVE_OP = 1 << 0

_VALID_BUS_AXIS_CODES = frozenset(ord(axis) for axis in 'XYZABC')

V5_STATUS_VALID_MCS = 1 << 0
V5_STATUS_VALID_CMD_MCS = 1 << 1
V5_STATUS_VALID_SPINDLE_SPEED = 1 << 4
V5_STATUS_VALID_LINEAR_VELOCITY = 1 << 5
V5_STATUS_VALID_FEED_OVERRIDE = 1 << 6
V5_STATUS_VALID_SPINDLE_OVERRIDE = 1 << 7

def crc32_like(prefix: bytes) -> int:
    value = 2166136261
    for b in prefix:
        value ^= b
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def wcs_from_g5x(g5x_index: int) -> Tuple[int, int]:
    if 1 <= g5x_index <= 9:
        return 1, g5x_index - 1
    return 0, -1

def atomic_write(path: str, payload: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f'{path}.tmp.{os.getpid()}'
    with open(tmp, 'wb') as fp:
        fp.write(payload)
    os.replace(tmp, path)


def pack_bus_status(
        snapshot,
        sequence: int,
        writer_identity: int,
        source_generation: int,
        monotonic_ns: int | None = None) -> bytes:
    valid = bool(snapshot and snapshot.get('valid'))
    entries = list(snapshot.get('entries', ())) if valid else []
    if valid and len(entries) != BUS_JOINT_COUNT:
        raise RuntimeError('native_bus_status_joint_count_invalid')
    while len(entries) < BUS_JOINT_COUNT:
        entries.append({})

    values = [
        BUS_STATUS_MAGIC,
        BUS_STATUS_VERSION,
        BUS_STATUS_STRUCT.size,
        1 if valid else 0,
        int(sequence) & 0xffffffff,
        int(writer_identity) & 0xffffffff,
        int(snapshot.get('mapping_generation', 0)) & 0xffffffff
        if valid else 0,
        int(snapshot.get('active_mask', 0)) & 0xffffffff if valid else 0,
        int(snapshot.get('master_flags', 0)) & 0xffffffff if valid else 0,
        int(snapshot.get('slaves_responding', 0)) & 0xffffffff
        if valid else 0,
        BUS_JOINT_COUNT,
        int(source_generation) & 0xffffffff,
        int(time.monotonic_ns() if monotonic_ns is None else monotonic_ns),
    ]
    for entry in entries:
        values.extend((
            1 if valid and entry.get('valid') else 0,
            int(entry.get('axis_code', 0)) & 0xffffffff,
            int(entry.get('slave_position', 0)) & 0xffffffff,
            int(entry.get('flags', 0)) & 0xffffffff,
            int(entry.get('statusword', 0)) & 0xffffffff,
        ))
    values.extend((0, 0))
    payload = BUS_STATUS_STRUCT.pack(*values)
    values[-2] = crc32_like(payload[:BUS_STATUS_CRC_OFFSET])
    return BUS_STATUS_STRUCT.pack(*values)


class BusStatusMmapWriter:
    def __init__(self, path=BUS_STATUS_DEFAULT_PATH):
        self.path = path
        self.fd = -1
        self.mapping = None
        self.sequence = 0

    def open(self):
        if self.mapping is not None:
            return self
        directory = os.path.dirname(self.path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        self.fd = os.open(
            self.path,
            os.O_RDWR | os.O_CREAT | getattr(os, 'O_CLOEXEC', 0),
            0o600)
        os.ftruncate(self.fd, BUS_STATUS_STRUCT.size)
        self.mapping = mmap.mmap(
            self.fd, BUS_STATUS_STRUCT.size, access=mmap.ACCESS_WRITE)
        return self

    def publish(
            self,
            snapshot,
            writer_identity: int,
            source_generation: int,
            monotonic_ns: int | None = None) -> None:
        if self.mapping is None:
            raise RuntimeError('native_bus_status_mapping_unavailable')
        self.sequence = (self.sequence + 2) & 0xffffffff
        if self.sequence == 0:
            self.sequence = 2
        payload = pack_bus_status(
            snapshot,
            self.sequence,
            writer_identity,
            source_generation,
            monotonic_ns)
        odd_sequence = self.sequence - 1
        self.mapping[
            BUS_STATUS_SEQUENCE_OFFSET:BUS_STATUS_SEQUENCE_OFFSET + 4
        ] = struct.pack('<I', odd_sequence)
        self.mapping[:BUS_STATUS_SEQUENCE_OFFSET] = (
            payload[:BUS_STATUS_SEQUENCE_OFFSET])
        self.mapping[BUS_STATUS_SEQUENCE_OFFSET + 4:] = (
            payload[BUS_STATUS_SEQUENCE_OFFSET + 4:])
        self.mapping[
            BUS_STATUS_SEQUENCE_OFFSET:BUS_STATUS_SEQUENCE_OFFSET + 4
        ] = struct.pack('<I', self.sequence)

    def close(self) -> None:
        if self.mapping is not None:
            self.mapping.close()
            self.mapping = None
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1


class HalBusStatusAccess:
    """Bind one resident, read-only EtherCAT snapshot into the position owner."""

    def __init__(
            self, hal_module, component, signal_info, bind_source):
        self._component = component
        self._local_names = {}
        self._pins = {}

        specs = (
            ('table_valid', 'mapping-table-valid',
             'v5-native-hal-owner.home-table-mapping-valid',
             'v5-home-table-valid', hal_module.HAL_BIT),
            ('table_generation', 'mapping-table-generation',
             'v5-native-hal-owner.home-table-map-gen',
             'v5-home-table-generation', hal_module.HAL_U32),
            ('table_active_mask', 'mapping-table-active-mask',
             'v5-native-hal-owner.home-table-active-mask',
             'v5-home-table-active-mask', hal_module.HAL_U32),
            ('router_valid', 'mapping-router-valid',
             'v5-bus-axis-router.mapping-valid',
             'v5-router-valid', hal_module.HAL_BIT),
            ('router_generation', 'mapping-router-generation',
             'v5-bus-axis-router.latched-mapping-generation',
             'v5-router-generation', hal_module.HAL_U32),
            ('router_active_mask', 'mapping-router-active-mask',
             'v5-bus-axis-router.latched-active-mask',
             'v5-router-active-mask', hal_module.HAL_U32),
            ('master_link_up', 'bus-master-link-up',
             'lcec.0.link-up', 'v5-bus-master-link-up',
             hal_module.HAL_BIT),
            ('master_state_op', 'bus-master-state-op',
             'lcec.0.state-op', 'v5-bus-master-state-op',
             hal_module.HAL_BIT),
            ('master_all_op', 'bus-master-all-op',
             'lcec.0.all-op', 'v5-bus-master-all-op',
             hal_module.HAL_BIT),
            ('slaves_responding', 'bus-master-responding',
             'lcec.0.slaves-responding', 'v5-bus-master-slaves-responding',
             hal_module.HAL_U32),
        )
        for key, local_name, source_pin, signal_name, pin_type in specs:
            self._register(
                key, local_name, source_pin, signal_name, pin_type,
                signal_info, bind_source)

        for joint in range(BUS_JOINT_COUNT):
            suffix = f'{joint:02d}'
            self._register(
                f'joint_{joint}_generation', f'bus-j{joint}-mapping-generation',
                f'v5-native-hal-owner.home-mapping-generation-{suffix}',
                f'v5-home-j{joint}-generation', hal_module.HAL_U32,
                signal_info, bind_source)
            self._register(
                f'joint_{joint}_axis_code', f'bus-j{joint}-axis-code',
                f'v5-native-hal-owner.home-axis-code-{suffix}',
                f'v5-home-j{joint}-axis-code', hal_module.HAL_U32,
                signal_info, bind_source)
            self._register(
                f'joint_{joint}_slave_position', f'bus-j{joint}-slave-position',
                f'v5-native-hal-owner.home-slave-position-{suffix}',
                f'v5-home-j{joint}-slave', hal_module.HAL_U32,
                signal_info, bind_source)

        for slave in range(BUS_JOINT_COUNT):
            self._register(
                f'slave_{slave}_statusword', f'bus-s{slave}-statusword',
                f'lcec.0.s{slave}.statusword',
                f'bus-s{slave}-status', hal_module.HAL_U32,
                signal_info, bind_source)

    def _register(
            self, key, local_name, source_pin, signal_name, pin_type,
            signal_info, bind_source):
        bind_source(
            signal_info, local_name, source_pin, signal_name, pin_type)
        self._local_names[key] = local_name

    def ready(self) -> None:
        self._pins = {
            key: self._component.getitem(local_name)
            for key, local_name in self._local_names.items()
        }

    def _value(self, key):
        return self._pins[key].get()

    def read(self):
        if not self._pins:
            raise RuntimeError('native_bus_status_pins_not_ready')
        table_valid = bool(self._value('table_valid'))
        router_valid = bool(self._value('router_valid'))
        table_generation = int(self._value('table_generation'))
        router_generation = int(self._value('router_generation'))
        table_mask = int(self._value('table_active_mask'))
        router_mask = int(self._value('router_active_mask'))
        if (
                not table_valid or not router_valid or
                table_generation <= 0 or
                table_generation != router_generation or
                table_mask != router_mask or
                table_mask == 0 or
                table_mask & ~((1 << BUS_JOINT_COUNT) - 1)):
            raise RuntimeError('native_bus_status_mapping_not_committed')

        entries = []
        seen_axes = set()
        seen_slaves = set()
        for joint in range(BUS_JOINT_COUNT):
            active = bool(table_mask & (1 << joint))
            if not active:
                entries.append({'valid': False})
                continue
            if (
                    int(self._value(
                        f'joint_{joint}_generation')) != table_generation):
                raise RuntimeError('native_bus_status_joint_mapping_invalid')
            axis_code = int(self._value(f'joint_{joint}_axis_code'))
            slave_position = int(
                self._value(f'joint_{joint}_slave_position'))
            if (
                    axis_code not in _VALID_BUS_AXIS_CODES or
                    slave_position < 0 or
                    slave_position >= BUS_JOINT_COUNT or
                    axis_code in seen_axes or
                    slave_position in seen_slaves):
                raise RuntimeError('native_bus_status_joint_mapping_invalid')
            seen_axes.add(axis_code)
            seen_slaves.add(slave_position)
            statusword = int(
                self._value(f'slave_{slave_position}_statusword'))
            if statusword < 0 or statusword > 0xffff:
                raise RuntimeError('native_bus_status_statusword_invalid')
            entries.append({
                'valid': True,
                'axis_code': axis_code,
                'slave_position': slave_position,
                'flags': (
                    BUS_JOINT_SLAVE_OP
                    if (statusword & 0x006f) == 0x0027 else 0),
                'statusword': statusword,
            })

        master_flags = 0
        if bool(self._value('master_link_up')):
            master_flags |= BUS_MASTER_LINK_UP
        if bool(self._value('master_state_op')):
            master_flags |= BUS_MASTER_STATE_OP
        if bool(self._value('master_all_op')):
            master_flags |= BUS_MASTER_ALL_OP
        slaves_responding = int(self._value('slaves_responding'))
        if slaves_responding < 0:
            raise RuntimeError('native_bus_status_slave_count_invalid')

        if (
                int(self._value('table_generation')) != table_generation or
                int(self._value('router_generation')) != router_generation or
                int(self._value('table_active_mask')) != table_mask or
                int(self._value('router_active_mask')) != router_mask):
            raise RuntimeError('native_bus_status_mapping_changed_during_read')
        return {
            'valid': True,
            'mapping_generation': table_generation,
            'active_mask': table_mask,
            'master_flags': master_flags,
            'slaves_responding': slaves_responding,
            'entries': entries,
        }
