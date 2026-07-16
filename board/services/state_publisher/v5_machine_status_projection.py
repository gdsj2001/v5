from __future__ import annotations

import math
import struct
import time
from typing import Iterable

from v5_wcs_status_codec import (
    DEFAULT_T0_TOOL_HOLDER_LENGTH_MM,
    MODAL_TOOL_BLOCK_STRUCT,
    MODAL_TOOL_MAGIC,
    MODAL_TOOL_VERSION,
    POSITION_AXIS_COUNT,
    POSITION_BLOCK_STRUCT,
    POSITION_MAGIC,
    POSITION_STATUS_VERSION,
    V5_STATUS_VALID_CMD_MCS,
    V5_STATUS_VALID_FEED_OVERRIDE,
    V5_STATUS_VALID_LINEAR_VELOCITY,
    V5_STATUS_VALID_MCS,
    V5_STATUS_VALID_SPINDLE_OVERRIDE,
    V5_STATUS_VALID_SPINDLE_SPEED,
    atomic_write,
    crc32_like,
    wcs_from_g5x,
)

V5_HOME_JOINT_COUNT = 5
V5_ROTARY_AXIS_CODES = {ord('A'): 'a', ord('B'): 'b', ord('C'): 'c'}
POSITION_STATUS_HEARTBEAT_SECONDS = 0.1


def exact_native_count(value):
    number = finite_float(value)
    if number is None:
        return None
    rounded = int(round(number))
    return rounded if abs(number - rounded) <= 1.0e-6 else None


def rotary_phase_degrees(relative_counts, counts_per_rev, counts_per_unit):
    period = int(counts_per_rev)
    relative = finite_float(relative_counts)
    if (relative is None or period <= 0 or
            not math.isfinite(float(counts_per_unit)) or float(counts_per_unit) == 0.0):
        raise RuntimeError('native_rotary_projection_count_domain_invalid')
    rounded = round(relative)
    if abs(relative - rounded) <= 1.0e-6:
        relative = float(rounded)
    phase_counts = relative % period
    return float(phase_counts) / abs(float(counts_per_unit))


class NativeRotaryDisplayProjection:
    """Resident native zero-relative rotary display projection.

    The BUS axis router has already subtracted the registered physical zero
    anchor before LinuxCNC and wcheckpoint see these coordinates.  Fold that
    zero-relative logical position once; subtracting the anchor here again
    would reproduce the physical anchor as a false A/C display offset.
    """

    def __init__(self, hal_module):
        self._hal = hal_module
        self._mapping_generation = None
        self._mapping_key = None
        self._records = {}
        self._wcheckpoint_metadata = {}

    def _get(self, name):
        return self._hal.get_value(name)

    def _invalidate_mapping_cache(self):
        self._mapping_generation = None
        self._mapping_key = None
        self._records = {}
        self._wcheckpoint_metadata = {}

    def _invalidate_wcheckpoint_cache(self, axis):
        self._wcheckpoint_metadata.pop(axis, None)

    def _load_mapping(self):
        valid = bool(self._get('v5-native-hal-owner.home-table-mapping-valid'))
        generation = int(self._get('v5-native-hal-owner.home-table-map-gen'))
        if not valid or not generation:
            self._invalidate_mapping_cache()
            raise RuntimeError('native_rotary_projection_mapping_invalid')
        commit_seq = int(self._get('v5-native-hal-owner.home-table-commit-seq'))
        if not commit_seq:
            self._invalidate_mapping_cache()
            raise RuntimeError('native_rotary_projection_mapping_invalid')
        if (self._mapping_key is not None and
                generation == self._mapping_key[0] and
                commit_seq == self._mapping_key[2]):
            return

        # A new generation is an atomic cache boundary.  Drop the old records
        # before reading any of the new metadata so an incomplete native commit
        # can never fall back to the previous mapping.
        self._invalidate_mapping_cache()
        active_mask = int(self._get('v5-native-hal-owner.home-table-active-mask'))
        mapping_key = (generation, active_mask, commit_seq)
        if not active_mask:
            raise RuntimeError('native_rotary_projection_mapping_invalid')

        records = {}
        for joint in range(V5_HOME_JOINT_COUNT):
            if not (active_mask & (1 << joint)):
                continue
            suffix = f'{joint:02d}'
            if not bool(self._get(f'v5-native-hal-owner.home-config-valid-{suffix}')):
                raise RuntimeError('native_rotary_projection_joint_config_invalid')
            if int(self._get(f'v5-native-hal-owner.home-mapping-generation-{suffix}')) != generation:
                raise RuntimeError('native_rotary_projection_generation_mismatch')
            axis_code = int(self._get(f'v5-native-hal-owner.home-axis-code-{suffix}'))
            axis_name = V5_ROTARY_AXIS_CODES.get(axis_code)
            if axis_name is None:
                continue
            status_slot = int(self._get(f'v5-native-hal-owner.home-status-slot-{suffix}'))
            counts_per_unit = finite_float(
                self._get(f'v5-native-hal-owner.home-counts-per-unit-{suffix}'))
            counts_per_rev = exact_native_count(
                abs(float(counts_per_unit or 0.0)) * 360.0)
            if (status_slot < 0 or status_slot >= POSITION_AXIS_COUNT or
                    counts_per_unit is None or counts_per_unit <= 0.0 or
                    counts_per_rev is None or counts_per_rev <= 0 or status_slot in records):
                raise RuntimeError('native_rotary_projection_joint_mapping_invalid')
            records[status_slot] = {
                'axis': axis_name,
                'counts_per_unit': abs(float(counts_per_unit)),
                'counts_per_rev': counts_per_rev,
            }
        if not records:
            raise RuntimeError('native_rotary_projection_axis_missing')
        mapping_key_after = (
            int(self._get('v5-native-hal-owner.home-table-map-gen')),
            int(self._get('v5-native-hal-owner.home-table-active-mask')),
            int(self._get('v5-native-hal-owner.home-table-commit-seq')))
        if (not bool(self._get('v5-native-hal-owner.home-table-mapping-valid')) or
                mapping_key_after != mapping_key):
            raise RuntimeError('native_rotary_projection_mapping_changed')
        self._records = records
        self._mapping_generation = generation
        self._mapping_key = mapping_key

    def _read_wcheckpoint(self, axis):
        for _attempt in range(3):
            valid_before = bool(self._get(f'v5-native-hal-owner.wcp-{axis}-valid'))
            generation_before = int(self._get(f'v5-native-hal-owner.wcp-{axis}-generation'))
            if not valid_before or not generation_before:
                self._invalidate_wcheckpoint_cache(axis)
                continue
            logical_counts = exact_native_count(
                self._get(f'v5-native-hal-owner.wcp-{axis}-logical-counts'))
            metadata = self._wcheckpoint_metadata.get(axis)
            needs_metadata_reload = (
                metadata is None or
                metadata['generation'] != generation_before)
            base_counts = None
            runtime_counts = None
            if needs_metadata_reload:
                # Invalidate first: if the generation is mid-commit, none of the
                # previous base/window metadata may be reused on this sample.
                self._invalidate_wcheckpoint_cache(axis)
                base_counts = exact_native_count(
                    self._get(f'v5-native-hal-owner.wcp-{axis}-base-counts'))
                runtime_counts = exact_native_count(
                    self._get(f'v5-native-hal-owner.wcp-{axis}-runtime-counts'))
            generation_after = int(self._get(f'v5-native-hal-owner.wcp-{axis}-generation'))
            valid_after = bool(self._get(f'v5-native-hal-owner.wcp-{axis}-valid'))
            if (not valid_after or generation_after != generation_before or
                    logical_counts is None):
                self._invalidate_wcheckpoint_cache(axis)
                continue
            if needs_metadata_reload:
                if (base_counts is None or runtime_counts is None or
                        runtime_counts != logical_counts - base_counts):
                    continue
                metadata = {
                    'generation': generation_before,
                    'base_counts': base_counts,
                }
                self._wcheckpoint_metadata[axis] = metadata
            return logical_counts, metadata['base_counts']
        self._invalidate_wcheckpoint_cache(axis)
        raise RuntimeError('native_rotary_projection_readback_invalid')

    def project(self, raw_mcs=None, raw_cmd=None):
        self._load_mapping()
        mcs = list(raw_mcs) if raw_mcs is not None else None
        cmd = list(raw_cmd) if raw_cmd is not None else None
        for status_slot, record in self._records.items():
            axis = record['axis']
            logical_counts, base_counts = self._read_wcheckpoint(axis)
            if mcs is not None:
                mcs[status_slot] = rotary_phase_degrees(
                    logical_counts,
                    record['counts_per_rev'],
                    record['counts_per_unit'])
            if cmd is not None:
                runtime_cmd_counts = float(cmd[status_slot]) * record['counts_per_unit']
                if not math.isfinite(runtime_cmd_counts):
                    raise RuntimeError('native_rotary_projection_command_invalid')
                logical_cmd_counts = runtime_cmd_counts + base_counts
                cmd[status_slot] = rotary_phase_degrees(
                    logical_cmd_counts,
                    record['counts_per_rev'],
                    record['counts_per_unit'])
        return mcs, cmd

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


def display_position_projection(values):
    projected = list(values)
    if len(projected) < POSITION_AXIS_COUNT:
        projected += [0.0] * (POSITION_AXIS_COUNT - len(projected))
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
    interpreter_paused_valid: int = 0,
    interpreter_paused: int = 0,
    all_homed_valid: int = 0,
    all_homed: int = 0,
    current_line_valid: int = 0,
    current_line: int = 0,
    motion_line_valid: int = 0,
    motion_line: int = 0,
    mdi_run_valid: int = 0,
    mdi_run_active: int = 0,
    mdi_run_line: int = 0,
    mdi_run_command: str = '') -> None:
    modal_text = modal if modal and modal != 'UNAVAILABLE' else ''
    tool_valid = tool_number is not None and int(tool_number) >= 0
    tool_no = int(tool_number) if tool_valid else -1
    length_valid = 1 if tool_valid and tool_length_valid and math.isfinite(float(tool_length_mm)) else 0
    length_value = float(tool_length_mm) if length_valid else 0.0
    idle_valid = 1 if interpreter_idle_valid else 0
    idle_value = 1 if idle_valid and interpreter_idle else 0
    paused_valid = 1 if interpreter_paused_valid else 0
    paused_value = 1 if paused_valid and interpreter_paused else 0
    homed_valid = 1 if all_homed_valid else 0
    homed_value = 1 if homed_valid and all_homed else 0
    current_valid = 1 if current_line_valid else 0
    current_value = max(0, int(current_line)) if current_valid else 0
    motion_valid = 1 if motion_line_valid else 0
    motion_value = max(0, int(motion_line)) if motion_valid else 0
    mdi_valid = 1 if mdi_run_valid else 0
    mdi_active = 1 if mdi_valid and mdi_run_active else 0
    mdi_line = max(0, int(mdi_run_line)) if mdi_valid else 0
    modal_bytes = modal_text.encode('utf-8', errors='replace')[:127]
    modal_bytes = modal_bytes + (b'\0' * (128 - len(modal_bytes)))
    mdi_command_bytes = (mdi_run_command or '').encode('utf-8', errors='replace')[:127]
    mdi_command_bytes = mdi_command_bytes + (b'\0' * (128 - len(mdi_command_bytes)))
    valid = 1 if (
        modal_text or tool_valid or idle_valid or paused_valid or homed_valid or
        current_valid or motion_valid or mdi_valid
    ) else 0
    monotonic_ns = time.monotonic_ns()
    prefix = struct.pack(
        '<IIIIIIIIIiIIQ128sdIIIiIiIIi128s',
        MODAL_TOOL_MAGIC, MODAL_TOOL_VERSION, MODAL_TOOL_BLOCK_STRUCT.size, valid,
        1 if modal_text else 0, 1 if tool_valid else 0, length_valid, idle_valid,
        paused_valid, tool_no, idle_value, paused_value, monotonic_ns, modal_bytes,
        length_value, homed_valid, homed_value, current_valid, current_value,
        motion_valid, motion_value, mdi_valid, mdi_active, mdi_line, mdi_command_bytes)
    crc = crc32_like(prefix)
    payload = MODAL_TOOL_BLOCK_STRUCT.pack(
        MODAL_TOOL_MAGIC, MODAL_TOOL_VERSION, MODAL_TOOL_BLOCK_STRUCT.size, valid,
        1 if modal_text else 0, 1 if tool_valid else 0, length_valid, idle_valid,
        paused_valid, tool_no, idle_value, paused_value, monotonic_ns, modal_bytes,
        length_value, homed_valid, homed_value, current_valid, current_value,
        motion_valid, motion_value, mdi_valid, mdi_active, mdi_line, mdi_command_bytes, crc, 0, 0)
    atomic_write(path, payload)


def interpreter_idle_from_stat(stat, idle_value):
    try:
        state = int(getattr(stat, 'interp_state'))
    except Exception:
        return 0, 0
    return 1, 1 if idle_value is not None and state == int(idle_value) else 0


def interpreter_paused_from_stat(stat, paused_value):
    try:
        state = int(getattr(stat, 'interp_state'))
    except Exception:
        state = None
    try:
        paused_attr = bool(getattr(stat, 'paused'))
    except Exception:
        paused_attr = False
    paused = paused_attr or (state is not None and paused_value is not None and state == int(paused_value))
    return 1 if state is not None or paused_attr else 0, 1 if paused else 0


def line_from_stat(stat, name: str):
    try:
        return 1, max(0, int(getattr(stat, name)))
    except Exception:
        return 0, 0


def mdi_run_from_stat(stat, current_line: int):
    try:
        queued = int(getattr(stat, 'queued_mdi_commands'))
    except Exception:
        return 0, 0, 0, ''
    active = 1 if queued > 0 else 0
    return 1, active, current_line if current_line > 0 else 0, ''


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


def write_position_status(
        path: str,
        stat,
        rotary_projection=None,
        publish_cadence=None,
        now_monotonic=None) -> bool:
    raw_mcs, mcs_present = normalized_axis_with_presence(getattr(stat, 'joint_actual_position', ()))
    mcs = display_position_projection(raw_mcs)
    raw_cmd, cmd_present = normalized_axis_with_presence(
        getattr(stat, 'joint_position', ()))
    cmd = display_position_projection(raw_cmd)
    if rotary_projection is not None:
        try:
            projected_mcs, projected_cmd = rotary_projection.project(
                mcs if mcs_present else None,
                cmd if cmd_present else None)
            if projected_mcs is not None:
                mcs = projected_mcs
            if projected_cmd is not None:
                cmd = projected_cmd
        except RuntimeError:
            mcs = [0.0] * POSITION_AXIS_COUNT
            cmd = [0.0] * POSITION_AXIS_COUNT
            mcs_present = False
            cmd_present = False
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
    signature = (
        valid_mask,
        tuple(mcs),
        tuple(cmd),
        spindle_speed,
        linear_velocity,
        feed_override,
        spindle_override,
    )
    if publish_cadence is not None and not publish_cadence.should_publish(
            'position', signature, now_monotonic, POSITION_STATUS_HEARTBEAT_SECONDS):
        return False
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
    if publish_cadence is not None:
        publish_cadence.mark_published('position', signature, now_monotonic)
    return True


def write_mock_position_status(path: str, mcs_values, cmd_values, modal: str = 'G90 G17 G54') -> None:
    class MockStat:
        pass
    stat = MockStat()
    stat.actual_position = normalized_axis(mcs_values)
    stat.joint_actual_position = normalized_axis(mcs_values)
    stat.joint_position = normalized_axis(cmd_values or mcs_values)
    stat.gcodes = ()
    stat.g5x_index = 1
    stat.spindle = ({'speed': 0.0, 'override': 1.0},)
    stat.feedrate = 1.0
    stat.current_vel = 0.0
    write_position_status(path, stat)
