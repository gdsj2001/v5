#!/usr/bin/env python3
import argparse
import math
import os
import struct
import sys
import time
import ctypes
import resource
from typing import Iterable, Tuple



def lock_process_memory(process_name: str) -> None:
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_MEMLOCK)
        target = hard if hard != resource.RLIM_INFINITY else resource.RLIM_INFINITY
        if soft != target:
            resource.setrlimit(resource.RLIMIT_MEMLOCK, (target, hard))
    except Exception:
        pass
    libc = ctypes.CDLL("libc.so.6", use_errno=True)
    if libc.mlockall(1 | 2) != 0:
        err = ctypes.get_errno()
        raise SystemExit(f"{process_name} mlockall(MCL_CURRENT|MCL_FUTURE) failed: errno={err}")

MAGIC = 0x56574353
POSITION_MAGIC = 0x56504F53
MODAL_TOOL_MAGIC = 0x564D544C
WCS_VERSION = 2
POSITION_STATUS_VERSION = 2
MODAL_TOOL_VERSION = 3
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
MODAL_TOOL_BLOCK_STRUCT = struct.Struct('<IIIIIIIIiIQ128sdIIII')
DEFAULT_PATH = '/dev/shm/v5_native_wcs_status.bin'
DEFAULT_POSITION_PATH = '/dev/shm/v5_native_position_status.bin'
DEFAULT_MODAL_TOOL_PATH = '/dev/shm/v5_native_modal_tool_status.bin'
DEFAULT_INTERVAL_MS = 100
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


def normalized_offsets(values: Iterable[float]):
    out = [0.0] * OFFSET_COUNT
    for i, value in enumerate(values):
        if i >= OFFSET_COUNT:
            break
        out[i] = float(value)
    return out


def atomic_write(path: str, payload: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f'{path}.tmp.{os.getpid()}'
    with open(tmp, 'wb') as fp:
        fp.write(payload)
        fp.flush()
        os.fsync(fp.fileno())
    os.replace(tmp, path)


def empty_wcs_table():
    return [[0.0 for _ in range(WCS_AXIS_COUNT)] for _ in range(WCS_COUNT)]


def flatten_wcs_table(table):
    values = []
    for wcs in range(WCS_COUNT):
        row = table[wcs] if table and wcs < len(table) else ()
        for axis in range(WCS_AXIS_COUNT):
            try:
                value = float(row[axis])
            except Exception:
                value = math.nan
            values.append(value)
    return values


def wcs_table_epoch(table) -> int:
    values = flatten_wcs_table(table)
    packed = struct.pack('<' + ('d' * WCS_TABLE_VALUE_COUNT), *values)
    epoch = crc32_like(packed)
    return epoch or 1


def mock_wcs_table(wcs_index: int, offsets):
    table = empty_wcs_table()
    row = normalized_offsets(offsets)
    if 0 <= wcs_index < WCS_COUNT:
        for axis in range(WCS_AXIS_COUNT):
            table[wcs_index][axis] = row[axis]
    return table


def write_status(path: str, valid: int, wcs_index: int, table, table_valid: int, epoch: int) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    values = flatten_wcs_table(table)
    finite = all(math.isfinite(value) for value in values)
    valid = 1 if valid and table_valid and finite and 0 <= wcs_index < WCS_COUNT else 0
    wcs_index = int(wcs_index) if valid else -1
    table_valid = 1 if valid else 0
    epoch = int(epoch) if valid and int(epoch) != 0 else 0
    monotonic_ns = time.monotonic_ns()
    prefix = struct.pack(
        '<IIIIiIIIIIQ' + ('d' * WCS_TABLE_VALUE_COUNT),
        MAGIC, WCS_VERSION, BLOCK_STRUCT.size, valid, wcs_index,
        WCS_COUNT, WCS_AXIS_COUNT, table_valid, epoch, 0, monotonic_ns, *values)
    crc = crc32_like(prefix)
    payload = BLOCK_STRUCT.pack(
        MAGIC, WCS_VERSION, BLOCK_STRUCT.size, valid, wcs_index,
        WCS_COUNT, WCS_AXIS_COUNT, table_valid, epoch, 0, monotonic_ns, *values, crc, 0)
    atomic_write(path, payload)


def parse_linuxcnc_ini_parameter_file(ini_path: str):
    if not ini_path:
        return ''
    section = ''
    try:
        with open(ini_path, 'r', encoding='utf-8', errors='replace') as fp:
            for raw in fp:
                line = raw.strip()
                if not line or line.startswith(('#', ';')):
                    continue
                if line.startswith('[') and line.endswith(']'):
                    section = line[1:-1].strip().upper()
                    continue
                if section != 'RS274NGC' or '=' not in line:
                    continue
                key, value = line.split('=', 1)
                if key.strip().upper() != 'PARAMETER_FILE':
                    continue
                param_path = value.strip()
                if not param_path:
                    return ''
                if os.path.isabs(param_path):
                    return param_path
                return os.path.abspath(os.path.join(os.path.dirname(ini_path), param_path))
    except OSError:
        return ''
    return ''


def resolve_parameter_file(ini_path: str, parameter_file: str) -> str:
    for candidate in (parameter_file, os.environ.get('V5_LINUXCNC_PARAMETER_FILE', ''), os.environ.get('LINUXCNC_PARAMETER_FILE', '')):
        if candidate:
            return candidate
    for ini_candidate in (ini_path, os.environ.get('V5_LINUXCNC_INI', ''), os.environ.get('INI_FILE_NAME', '')):
        resolved = parse_linuxcnc_ini_parameter_file(ini_candidate)
        if resolved:
            return resolved
    return ''


def read_linuxcnc_parameter_values(path: str):
    values = {}
    if not path:
        raise FileNotFoundError('linuxcnc_parameter_file_unset')
    with open(path, 'r', encoding='utf-8', errors='replace') as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith(('#', ';')):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                key = int(float(parts[0]))
                value = float(parts[1])
            except ValueError:
                continue
            if math.isfinite(value):
                values[key] = value
    return values


def read_wcs_table(parameter_file: str):
    params = read_linuxcnc_parameter_values(parameter_file)
    table = empty_wcs_table()
    for wcs_index, base in enumerate(WCS_PARAM_BASES):
        for axis_index, param_offset in enumerate(WCS_PARAM_AXIS_OFFSETS):
            param_no = base + param_offset
            if param_no not in params:
                raise KeyError(f'wcs_parameter_missing_{param_no}')
            table[wcs_index][axis_index] = params[param_no]
    return table


class ResidentWcsParameterOwner:
    def __init__(self):
        self.table = None
        self.epoch = 0
        self.source_path = ''

    def load_from_parameter_file(self, parameter_file: str) -> None:
        table = read_wcs_table(parameter_file)
        self.table = [[float(value) for value in row] for row in table]
        self.epoch = wcs_table_epoch(self.table)
        self.source_path = parameter_file

    def loaded(self) -> bool:
        return self.table is not None and self.epoch != 0

    def snapshot(self):
        if self.table is None:
            raise RuntimeError('wcs_resident_owner_unloaded')
        return [row[:] for row in self.table]

    def update_active_from_stat(self, wcs_index: int, offsets) -> bool:
        if self.table is None or wcs_index < 0 or wcs_index >= WCS_COUNT or offsets is None:
            return False
        row = normalized_offsets(offsets)
        changed = False
        for axis in range(WCS_AXIS_COUNT):
            if not math.isfinite(row[axis]):
                return False
            if self.table[wcs_index][axis] != row[axis]:
                changed = True
            self.table[wcs_index][axis] = row[axis]
        if changed or self.epoch == 0:
            self.epoch = wcs_table_epoch(self.table)
        return True


def active_wcs_offsets_from_stat(stat):
    values = getattr(stat, 'g5x_offset', ()) or ()
    offsets = []
    for source_index in WCS_PARAM_AXIS_OFFSETS:
        if source_index >= len(values):
            return None
        value = finite_float(values[source_index])
        if value is None:
            return None
        offsets.append(value)
    return offsets


def normalized_axis(values: Iterable[float]):
    out = [0.0] * POSITION_AXIS_COUNT
    for i, value in enumerate(values):
        if i >= POSITION_AXIS_COUNT:
            break
        out[i] = float(value)
    return out


def normalized_axis_with_presence(values: Iterable[float]):
    out = [0.0] * POSITION_AXIS_COUNT
    present = False
    for i, value in enumerate(values):
        if i >= POSITION_AXIS_COUNT:
            break
        out[i] = float(value)
        present = True
    return out, present


def normalized_joint_output_with_presence(joints):
    out = [0.0] * POSITION_AXIS_COUNT
    present = False
    if not joints:
        return out, present
    for i, item in enumerate(joints):
        if i >= POSITION_AXIS_COUNT:
            break
        try:
            value = item.get('output') if isinstance(item, dict) else getattr(item, 'output')
            out[i] = float(value)
        except Exception:
            return [0.0] * POSITION_AXIS_COUNT, False
        present = True
    return out, present


def rotary_display_phase_deg(value: float) -> float:
    value = float(value)
    if not math.isfinite(value):
        return value
    phase = math.fmod(value, ROTARY_FULL_TURN_DEG)
    if phase < 0.0:
        phase += ROTARY_FULL_TURN_DEG
    if phase < 0.0005 or (ROTARY_FULL_TURN_DEG - phase) < 0.0005:
        return 0.0
    return phase


def display_position_projection(values):
    projected = list(values)
    if len(projected) < POSITION_AXIS_COUNT:
        projected += [0.0] * (POSITION_AXIS_COUNT - len(projected))
    for axis_i in ROTARY_AXIS_INDICES:
        projected[axis_i] = rotary_display_phase_deg(projected[axis_i])
    return projected[:POSITION_AXIS_COUNT]


def finite_float(value):
    try:
        number = float(value)
    except Exception:
        return None
    return number if math.isfinite(number) else None


def gcode_to_text(code: int) -> str:
    if code == 0:
        return 'G0'
    if code < 0:
        return ''
    whole = code // 10
    tenth = code % 10
    if tenth:
        return f'G{whole}.{tenth}'
    return f'G{whole}'


MODAL_GROUPS = (
    (0, 10, 20, 30, 382, 383, 384, 385),
    (170, 180, 190),
    (200, 210),
    (400, 410, 411, 420, 421),
    (430, 431, 490),
    (610, 611, 640),
    tuple(range(800, 900, 10)),
    (900, 910),
    (901, 911),
    (930, 940, 950),
    (960, 970),
    (980, 990),
)


def append_modal_token(tokens, token: str) -> None:
    if token and token not in tokens:
        tokens.append(token)


def modal_text_from_gcodes(gcodes: Iterable[int], g5x_index: int) -> str:
    raw_codes = [int(code) for code in (gcodes or ())]
    active = {code for code in raw_codes if code > 0}
    motion_group = MODAL_GROUPS[0]
    if 0 in raw_codes and not any(code in active for code in motion_group if code != 0):
        active.add(0)
    wanted = []
    for group in MODAL_GROUPS:
        for code in group:
            if code in active:
                append_modal_token(wanted, gcode_to_text(code))
                break
    valid, wcs_index = wcs_from_g5x(int(g5x_index))
    if valid:
        wcs_texts = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59', 'G59.1', 'G59.2', 'G59.3']
        insert_at = min(5, len(wanted))
        wanted.insert(insert_at, wcs_texts[wcs_index])
    if not wanted:
        return 'UNAVAILABLE'
    text = ''
    for token in wanted:
        candidate = token if not text else f'{text} {token}'
        if len(candidate.encode('utf-8')) >= 128:
            break
        text = candidate
    return text or 'UNAVAILABLE'


def current_tool_number(stat):
    value = getattr(stat, 'tool_in_spindle', None)
    try:
        number = int(value)
    except Exception:
        return None
    return number if number >= 0 else None


def tool_entry_value(entry, *names):
    if isinstance(entry, dict):
        for name in names:
            if name in entry:
                return entry.get(name)
        return None
    for name in names:
        try:
            return getattr(entry, name)
        except Exception:
            pass
    return None


def current_tool_length(stat, tool_number, t0_tool_holder_length_mm=DEFAULT_T0_TOOL_HOLDER_LENGTH_MM):
    if tool_number is None:
        return 0, 0.0
    if tool_number == 0:
        value = finite_float(t0_tool_holder_length_mm)
        return (1, value) if value is not None else (0, 0.0)
    for entry in getattr(stat, 'tool_table', ()) or ():
        raw_id = tool_entry_value(entry, 'id', 'toolno', 'tool')
        try:
            entry_id = int(raw_id)
        except Exception:
            continue
        if entry_id != tool_number:
            continue
        value = finite_float(tool_entry_value(entry, 'zoffset', 'length', 'tool_length'))
        return (1, value) if value is not None else (0, 0.0)
    return 0, 0.0


def write_modal_tool_status(
    path: str,
    modal: str,
    tool_number,
    tool_length_valid: int,
    tool_length_mm: float,
    interpreter_idle_valid: int = 0,
    interpreter_idle: int = 0,
    all_homed_valid: int = 0,
    all_homed: int = 0) -> None:
    modal_text = modal if modal and modal != 'UNAVAILABLE' else ''
    tool_valid = tool_number is not None and int(tool_number) >= 0
    tool_no = int(tool_number) if tool_valid else -1
    length_valid = 1 if tool_valid and tool_length_valid and math.isfinite(float(tool_length_mm)) else 0
    length_value = float(tool_length_mm) if length_valid else 0.0
    idle_valid = 1 if interpreter_idle_valid else 0
    idle_value = 1 if idle_valid and interpreter_idle else 0
    homed_valid = 1 if all_homed_valid else 0
    homed_value = 1 if homed_valid and all_homed else 0
    modal_bytes = modal_text.encode('utf-8', errors='replace')[:127]
    modal_bytes = modal_bytes + (b'\0' * (128 - len(modal_bytes)))
    valid = 1 if modal_text or tool_valid or idle_valid or homed_valid else 0
    monotonic_ns = time.monotonic_ns()
    prefix = struct.pack(
        '<IIIIIIIIiIQ128sdII',
        MODAL_TOOL_MAGIC, MODAL_TOOL_VERSION, MODAL_TOOL_BLOCK_STRUCT.size, valid,
        1 if modal_text else 0, 1 if tool_valid else 0, length_valid, idle_valid,
        tool_no, idle_value, monotonic_ns, modal_bytes, length_value, homed_valid, homed_value)
    crc = crc32_like(prefix)
    payload = MODAL_TOOL_BLOCK_STRUCT.pack(
        MODAL_TOOL_MAGIC, MODAL_TOOL_VERSION, MODAL_TOOL_BLOCK_STRUCT.size, valid,
        1 if modal_text else 0, 1 if tool_valid else 0, length_valid, idle_valid,
        tool_no, idle_value, monotonic_ns, modal_bytes, length_value, homed_valid, homed_value, crc, 0)
    atomic_write(path, payload)


def interpreter_idle_from_stat(stat, idle_value):
    try:
        state = int(getattr(stat, 'interp_state'))
    except Exception:
        return 0, 0
    return 1, 1 if idle_value is not None and state == int(idle_value) else 0


def all_homed_from_stat(stat):
    homed = getattr(stat, 'homed', None)
    if homed is None:
        return 0, 0
    try:
        values = [int(v) for v in list(homed)[:POSITION_AXIS_COUNT]]
    except Exception:
        return 0, 0
    if len(values) < POSITION_AXIS_COUNT:
        return 0, 0
    return 1, 1 if all(value != 0 for value in values) else 0


def first_spindle(stat):
    spindles = getattr(stat, 'spindle', ())
    if spindles:
        return spindles[0]
    return {}


def write_position_status(path: str, stat) -> None:
    raw_mcs, mcs_present = normalized_axis_with_presence(getattr(stat, 'joint_actual_position', ()))
    mcs = display_position_projection(raw_mcs)
    raw_cmd, cmd_present = normalized_joint_output_with_presence(getattr(stat, 'joint', ()))
    if not cmd_present:
        raw_cmd, cmd_present = normalized_axis_with_presence(getattr(stat, 'position', ()))
    cmd = display_position_projection(raw_cmd)
    spindle = first_spindle(stat)
    spindle_speed = float(spindle.get('speed', 0.0)) if isinstance(spindle, dict) else 0.0
    spindle_override = float(spindle.get('override', 1.0)) * 100.0 if isinstance(spindle, dict) else 100.0
    valid_mask = (
        V5_STATUS_VALID_SPINDLE_SPEED |
        V5_STATUS_VALID_LINEAR_VELOCITY |
        V5_STATUS_VALID_FEED_OVERRIDE |
        V5_STATUS_VALID_SPINDLE_OVERRIDE
    )
    if mcs_present:
        valid_mask |= V5_STATUS_VALID_MCS
    if cmd_present:
        valid_mask |= V5_STATUS_VALID_CMD_MCS
    feed_override = float(getattr(stat, 'feedrate', 1.0)) * 100.0
    linear_velocity = float(getattr(stat, 'current_vel', 0.0)) * 60.0
    monotonic_ns = time.monotonic_ns()
    prefix = struct.pack(
        '<IIIIIIQ' + ('d' * (POSITION_AXIS_COUNT * 2)) + ('d' * 4),
        POSITION_MAGIC, POSITION_STATUS_VERSION, POSITION_BLOCK_STRUCT.size, valid_mask, POSITION_AXIS_COUNT, 0, monotonic_ns,
        *(mcs + cmd), spindle_speed, linear_velocity, feed_override, spindle_override)
    crc = crc32_like(prefix)
    payload = POSITION_BLOCK_STRUCT.pack(
        POSITION_MAGIC, POSITION_STATUS_VERSION, POSITION_BLOCK_STRUCT.size, valid_mask, POSITION_AXIS_COUNT, 0, monotonic_ns,
        *(mcs + cmd), spindle_speed, linear_velocity, feed_override, spindle_override, crc, 0)
    atomic_write(path, payload)


def write_mock_position_status(path: str, mcs_values, cmd_values, modal: str = 'G90 G17 G54') -> None:
    class MockStat:
        pass
    stat = MockStat()
    stat.actual_position = normalized_axis(mcs_values)
    stat.joint_actual_position = normalized_axis(mcs_values)
    stat.position = normalized_axis(cmd_values or mcs_values)
    stat.gcodes = ()
    stat.g5x_index = 1
    stat.spindle = ({'speed': 0.0, 'override': 1.0},)
    stat.feedrate = 1.0
    stat.current_vel = 0.0
    write_position_status(path, stat)

def parse_offsets(text: str):
    if not text:
        return [0.0] * OFFSET_COUNT
    return [float(part.strip()) for part in text.split(',') if part.strip()]


def load_linuxcnc():
    dist = '/usr/lib/python3/dist-packages'
    if os.path.isdir(dist) and dist not in sys.path:
        sys.path.insert(0, dist)
    import linuxcnc  # type: ignore
    return linuxcnc



def poll_once(
    stat,
    resident_wcs: ResidentWcsParameterOwner,
    path: str,
    position_path: str,
    modal_tool_path: str,
    t0_tool_holder_length_mm=DEFAULT_T0_TOOL_HOLDER_LENGTH_MM,
    interpreter_idle_value=None) -> Tuple[int, int]:
    stat.poll()
    valid, wcs_index = wcs_from_g5x(int(getattr(stat, 'g5x_index', 0)))
    if not resident_wcs.loaded():
        raise RuntimeError('wcs_resident_owner_unloaded')
    table_valid = 0
    if valid:
        active_offsets = active_wcs_offsets_from_stat(stat)
        if resident_wcs.update_active_from_stat(wcs_index, active_offsets):
            table_valid = 1
    table = resident_wcs.snapshot()
    modal = modal_text_from_gcodes(getattr(stat, 'gcodes', ()), int(getattr(stat, 'g5x_index', 0)))
    tool_number = current_tool_number(stat)
    tool_length_valid, tool_length_mm = current_tool_length(stat, tool_number, t0_tool_holder_length_mm)
    idle_valid, idle_value = interpreter_idle_from_stat(stat, interpreter_idle_value)
    all_homed_valid, all_homed = all_homed_from_stat(stat)
    write_status(path, valid, wcs_index, table, table_valid, resident_wcs.epoch)
    write_position_status(position_path, stat)
    write_modal_tool_status(
        modal_tool_path,
        modal,
        tool_number,
        tool_length_valid,
        tool_length_mm,
        idle_valid,
        idle_value,
        all_homed_valid,
        all_homed)
    return valid if table_valid else 0, wcs_index if table_valid else -1


lock_process_memory("v5_wcs_status_publisher")

def main() -> int:
    parser = argparse.ArgumentParser(description='Publish v5 WCS actual status from boot-resident native WCS memory.')
    parser.add_argument('--path', default=DEFAULT_PATH)
    parser.add_argument('--position-path', default=DEFAULT_POSITION_PATH)
    parser.add_argument('--modal-tool-path', default=DEFAULT_MODAL_TOOL_PATH)
    parser.add_argument('--ini', default=os.environ.get('V5_LINUXCNC_INI', os.environ.get('INI_FILE_NAME', '')))
    parser.add_argument('--parameter-file', default=os.environ.get('V5_LINUXCNC_PARAMETER_FILE', ''))
    parser.add_argument('--interval-ms', type=int, default=DEFAULT_INTERVAL_MS)
    parser.add_argument('--t0-tool-holder-length-mm', type=float, default=float(os.environ.get('V5_T0_TOOL_HOLDER_LENGTH_MM', DEFAULT_T0_TOOL_HOLDER_LENGTH_MM)))
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--mock-g5x-index', type=int, default=0)
    parser.add_argument('--mock-offsets', default='')
    parser.add_argument('--mock-mcs', default='')
    parser.add_argument('--mock-cmd-mcs', default='')
    args = parser.parse_args()

    if args.mock_g5x_index or args.mock_mcs:
        valid, wcs_index = wcs_from_g5x(args.mock_g5x_index or 1)
        write_status(args.path, valid, wcs_index, mock_wcs_table(wcs_index, parse_offsets(args.mock_offsets)), 1 if valid else 0, 1)
        if args.mock_mcs:
            write_mock_position_status(args.position_path, parse_offsets(args.mock_mcs), parse_offsets(args.mock_cmd_mcs))
        write_modal_tool_status(args.modal_tool_path, 'G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97', 0, 1, args.t0_tool_holder_length_mm, 1, 1, 1, 1)
        print(f'v5_wcs_status_publisher mock valid={valid} wcs_index={wcs_index} path={args.path} position_path={args.position_path} modal_tool_path={args.modal_tool_path}')
        return 0

    resident_wcs = ResidentWcsParameterOwner()
    resolved_parameter_file = resolve_parameter_file(args.ini, args.parameter_file)
    resident_wcs.load_from_parameter_file(resolved_parameter_file)
    print(
        f'v5_wcs_status_publisher resident_wcs_loaded source={resolved_parameter_file} epoch={resident_wcs.epoch}',
        flush=True)

    linuxcnc = load_linuxcnc()
    interpreter_idle_value = getattr(linuxcnc, 'INTERP_IDLE', 1)
    consecutive_failures = 0
    interval = max(args.interval_ms, 20) / 1000.0
    invalid_after_failures = max(1, int(math.ceil(1.0 / interval)))
    stat = None
    while True:
        try:
            if stat is None:
                stat = linuxcnc.stat()
            valid, wcs_index = poll_once(stat, resident_wcs, args.path, args.position_path, args.modal_tool_path, args.t0_tool_holder_length_mm, interpreter_idle_value)
            consecutive_failures = 0
            print(f'v5_wcs_status_publisher valid={valid} wcs_index={wcs_index}', flush=True)
            if args.once:
                return 0
        except Exception as exc:
            consecutive_failures += 1
            stat = None
            if consecutive_failures >= invalid_after_failures:
                write_status(args.path, 0, -1, empty_wcs_table(), 0, 0)
                write_modal_tool_status(args.modal_tool_path, '', None, 0, 0.0)
            if consecutive_failures == 1 or consecutive_failures >= invalid_after_failures:
                print(f'v5_wcs_status_publisher reconnecting after unavailable poll: {exc}', file=sys.stderr, flush=True)
            if args.once and consecutive_failures >= invalid_after_failures:
                return 1
        time.sleep(interval)


if __name__ == '__main__':
    raise SystemExit(main())
