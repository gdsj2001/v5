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
    atomic_write,
    crc32_like,
    wcs_from_g5x,
)

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
