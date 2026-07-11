from __future__ import annotations

import os
import struct
from typing import Tuple

MAGIC = 0x56574353
POSITION_MAGIC = 0x56504F53
MODAL_TOOL_MAGIC = 0x564D544C
WCS_VERSION = 2
POSITION_STATUS_VERSION = 2
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
POSITION_BLOCK_STRUCT = struct.Struct('<IIIIIIQ' + ('d' * (POSITION_AXIS_COUNT * 2)) + ('d' * 4) + 'II')
MODAL_TOOL_BLOCK_STRUCT = struct.Struct('<IIIIIIIIIiIIQ128sdIIIiIiIIi128sIII')
DEFAULT_PATH = '/dev/shm/v5_native_wcs_status.bin'
DEFAULT_POSITION_PATH = '/dev/shm/v5_native_position_status.bin'
DEFAULT_MODAL_TOOL_PATH = '/dev/shm/v5_native_modal_tool_status.bin'
DEFAULT_INTERVAL_MS = 33
DEFAULT_T0_TOOL_HOLDER_LENGTH_MM = 15.0

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
        fp.flush()
        os.fsync(fp.fileno())
    os.replace(tmp, path)
