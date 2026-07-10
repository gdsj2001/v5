#!/usr/bin/env python3
"""Boot-resident mapping from native controller errors to operator Chinese."""

from __future__ import annotations

import csv
import hashlib
import json
import os
import re
import struct
import time
from dataclasses import dataclass
from typing import Iterable, List, Pattern, Tuple


EXPECTED_SOURCE_COUNT = 555
EXPECTED_INVENTORY_SHA256 = "10ea2746943af0d98cf709cfe1f550006440169637322e6f639cfc32be517962"
OPERATOR_ERROR_MAGIC = 0x564F4552
OPERATOR_ERROR_VERSION = 1
OPERATOR_ERROR_PREFIX_STRUCT = struct.Struct('<IIIIIIQQ64s24s96s384s256s')
OPERATOR_ERROR_BLOCK_STRUCT = struct.Struct('<IIIIIIQQ64s24s96s384s256sII')
DEFAULT_OPERATOR_ERROR_PATH = '/dev/shm/v5_native_operator_error_status.bin'
DEFAULT_OPERATOR_ERROR_MAP = '/opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv'

GROUP_PRESENTATION = {
    "PROGRAM_SYNTAX": ("程序格式错误", "检查当前高亮程序行的指令、参数和值，修正后重新打开并运行程序"),
    "PROGRAM_GEOMETRY": ("程序几何错误", "检查当前高亮程序行的平面、圆弧、循环和终点参数，修正后重试"),
    "PROGRAM_FLOW": ("程序流程错误", "检查子程序、O字、调用、返回和重映射关系，修正后重新打开程序"),
    "PROGRAM_FILE": ("程序文件错误", "检查程序文件是否存在、可读且有正确结束标记，修正后重新打开"),
    "MOTION_HOME": ("需要回零", "选择机械全轴并完成本次开机回零，再重新执行当前动作"),
    "MOTION_LIMIT": ("轴限位阻止运动", "停止当前动作，检查目标位置、软限位和限位开关，确认后再运动"),
    "MOTION_JOG": ("点动未执行", "松开点动按钮，检查轴选择、锁定和运行状态后重新点动"),
    "MOTION_KINEMATICS": ("运动学状态错误", "停止运行并检查运动模型、旋转中心和运动学配置，修正并重启后再试"),
    "MOTION_PROBE": ("探测条件错误", "检查探针状态、探测方向和目标距离，恢复正确状态后重新探测"),
    "MOTION_PLANNER": ("运动轨迹无法执行", "检查当前程序段的目标、速度和轨迹连续性，修正后重新运行"),
    "SAFETY_ENABLE": ("机器状态不允许动作", "先排除现场风险，取消急停并确认机器已使能，再重新操作"),
    "MACHINE_MODE": ("运行模式不允许动作", "停止或暂停当前任务，切换到动作要求的手动、自动、MDI、关节或坐标模式后重试"),
    "DRIVE_FEEDBACK": ("轴反馈故障", "停止运动，检查驱动使能、编码器反馈和跟随误差，排除故障后重新使能"),
    "SPINDLE": ("主轴动作失败", "停止程序，检查主轴使能、定向、速度和反馈后重新执行"),
    "TOOL": ("刀具或刀补错误", "检查刀号、刀具参数、刀补模式和当前平面，修正后重新运行"),
    "CONFIG": ("系统配置错误", "保持机器停止，由维护人员检查对应配置并完成重启和回读确认"),
    "INTERNAL": ("控制系统内部错误", "停止当前运行并保留程序名和行号，联系维护人员处理"),
    "USER_PROGRAM": ("用户程序报告错误", "按用户程序给出的原因修改程序或现场条件后重新运行"),
    "PASSTHROUGH": ("控制系统报告错误", "停止当前动作并重新执行一次；若再次出现，联系维护人员"),
}

ALIAS_PRESENTATION = {
    "POWER_ON_HOME_REQUIRED": (
        "需要回零",
        "当前动作要求先完成本次开机机械全轴回零",
        "关闭提示，选择机械全轴并完成回零，再重新执行当前动作",
    ),
    "WORK_ZERO_NOT_HOMED": (
        "需要回零",
        "加工坐标归零前必须先完成本次开机机械全轴回零",
        "选择机械全轴并完成回零，再重新执行加工坐标归零",
    ),
    "NOT_HOMED": (
        "需要回零",
        "当前动作要求相关轴已经完成回零",
        "完成机械全轴回零后重新执行当前动作",
    ),
    "UNHOMED": (
        "需要回零",
        "当前动作要求相关轴已经完成回零",
        "完成机械全轴回零后重新执行当前动作",
    ),
    "POWER_ON_HOME_STATUS_UNAVAILABLE": (
        "回零状态不可用",
        "当前无法读取本次开机回零状态",
        "关闭提示，等待状态恢复并完成机械全轴回零后再操作",
    ),
    "HOME_STATUS_UNAVAILABLE": (
        "回零状态不可用",
        "当前无法读取回零状态",
        "等待状态恢复后重新执行回零",
    ),
    "ESTOP": GROUP_PRESENTATION["SAFETY_ENABLE"][:1] + (
        "机器当前处于急停状态",
        GROUP_PRESENTATION["SAFETY_ENABLE"][1],
    ),
    "HOME_PRECONDITION_ESTOP": GROUP_PRESENTATION["SAFETY_ENABLE"][:1] + (
        "机器当前处于急停状态，不能执行回零",
        GROUP_PRESENTATION["SAFETY_ENABLE"][1],
    ),
    "DISABLED": GROUP_PRESENTATION["SAFETY_ENABLE"][:1] + (
        "机器当前未上使能",
        GROUP_PRESENTATION["SAFETY_ENABLE"][1],
    ),
    "HOME_PRECONDITION_DISABLED": GROUP_PRESENTATION["SAFETY_ENABLE"][:1] + (
        "机器当前未上使能，不能执行回零",
        GROUP_PRESENTATION["SAFETY_ENABLE"][1],
    ),
}

_PRINTF_TOKEN = re.compile(
    r"%(?:(?P<position>\d+)\$)?[-+#0 'I]*(?:\*|\d+)?"
    r"(?:\.(?:\*|\d+))?(?:hh|h|ll|l|j|z|t|L)?(?P<kind>[diuoxXfFeEgGaAcsp])"
)
_BRAND_WORDS = re.compile(r"\b(?:linuxcnc|linuxcncrsh|emc|hal|nml|motmod|rtapi|gdb|python)\b", re.I)
_ABSOLUTE_PATH = re.compile(r"(?<!\w)(?:[A-Za-z]:[\\/]|/)[^\s，。；：]+")


@dataclass(frozen=True)
class NativeOperatorMessage:
    source_id: str
    title_cn: str
    reason_cn: str
    next_cn: str
    fingerprint: str
    matched: bool


@dataclass(frozen=True)
class _MapEntry:
    source_id: str
    origin: str
    native_pattern: str
    reason_cn: str
    handling_group: str
    regex: Pattern[str]
    capture_count: int
    literal_score: int


def _normalize(value: str) -> str:
    return " ".join((value or "").replace("\x00", " ").split())


def _capture_regex(kind: str) -> str:
    if kind == "c":
        return r"(.)"
    if kind in "diuoxX":
        return r"([+-]?(?:0[xX])?[0-9A-Fa-f]+)"
    if kind in "fFeEgGaA":
        return r"([+-]?(?:nan|inf|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))"
    if kind == "p":
        return r"(\S+)"
    return r"(.*?)"


def _compile_printf_pattern(pattern: str) -> Tuple[Pattern[str], int, int]:
    normalized = _normalize(pattern)
    pieces = ["^"]
    capture_count = 0
    literal_score = 0
    index = 0
    while index < len(normalized):
        if normalized[index] != "%":
            end = normalized.find("%", index)
            if end < 0:
                end = len(normalized)
            literal = normalized[index:end]
            pieces.append(re.escape(literal))
            literal_score += len(literal)
            index = end
            continue
        if index + 1 < len(normalized) and normalized[index + 1] == "%":
            pieces.append("%")
            literal_score += 1
            index += 2
            continue
        token = _PRINTF_TOKEN.match(normalized, index)
        if not token:
            pieces.append("%")
            literal_score += 1
            index += 1
            continue
        pieces.append(_capture_regex(token.group("kind")))
        capture_count += 1
        index = token.end()
    pieces.append("$")
    return re.compile("".join(pieces), re.DOTALL), capture_count, literal_score


def _render_template(template: str, captures: Iterable[str]) -> str:
    values = list(captures)
    sequential = 0
    output: List[str] = []
    index = 0
    while index < len(template):
        if template[index] != "%":
            output.append(template[index])
            index += 1
            continue
        if index + 1 < len(template) and template[index + 1] == "%":
            output.append("%")
            index += 2
            continue
        token = _PRINTF_TOKEN.match(template, index)
        if not token:
            output.append("%")
            index += 1
            continue
        if token.group("position"):
            value_index = int(token.group("position")) - 1
        else:
            value_index = sequential
            sequential += 1
        output.append(values[value_index] if 0 <= value_index < len(values) else "")
        index = token.end()
    return "".join(output)


def _sanitize_operator_text(text: str) -> str:
    value = _BRAND_WORDS.sub("控制系统", text or "")
    value = _ABSOLUTE_PATH.sub("目标文件", value)
    return _normalize(value)


def _fingerprint(raw_text: str) -> str:
    return hashlib.sha256((raw_text or "").encode("utf-8", errors="replace")).hexdigest()[:12].upper()


def _crc32_like(prefix: bytes) -> int:
    value = 2166136261
    for byte in prefix:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def _fixed_utf8(text: str, capacity: int) -> bytes:
    raw = (text or '').encode('utf-8', errors='replace')[:max(0, capacity - 1)]
    raw = raw.decode('utf-8', errors='ignore').encode('utf-8')
    return raw + (b'\0' * (capacity - len(raw)))


def write_operator_error_status(path: str, valid: int, kind: int, generation: int, message=None) -> None:
    source_id = _fixed_utf8(message.source_id if message else '', 64)
    fingerprint = _fixed_utf8(message.fingerprint if message else '', 24)
    title_cn = _fixed_utf8(message.title_cn if message else '', 96)
    reason_cn = _fixed_utf8(message.reason_cn if message else '', 384)
    next_cn = _fixed_utf8(message.next_cn if message else '', 256)
    valid_value = 1 if valid and message and message.reason_cn else 0
    monotonic_ns = time.monotonic_ns()
    fields = (
        OPERATOR_ERROR_MAGIC,
        OPERATOR_ERROR_VERSION,
        OPERATOR_ERROR_BLOCK_STRUCT.size,
        valid_value,
        int(kind) if valid_value else 0,
        0,
        int(generation) if valid_value else 0,
        monotonic_ns,
        source_id,
        fingerprint,
        title_cn,
        reason_cn,
        next_cn,
    )
    prefix = OPERATOR_ERROR_PREFIX_STRUCT.pack(*fields)
    payload = OPERATOR_ERROR_BLOCK_STRUCT.pack(*fields, _crc32_like(prefix), 0)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    temporary = f'{path}.tmp.{os.getpid()}'
    with open(temporary, 'wb') as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


class NativeOperatorErrorMap:
    def __init__(self, entries: Iterable[_MapEntry]):
        self._entries = tuple(sorted(
            entries,
            key=lambda item: (
                item.handling_group == "PASSTHROUGH",
                -item.literal_score,
                item.source_id,
            ),
        ))

    @property
    def source_count(self) -> int:
        return len(self._entries)

    @classmethod
    def from_tsv(cls, path: str) -> "NativeOperatorErrorMap":
        entries: List[_MapEntry] = []
        seen_ids = set()
        seen_patterns = set()
        inventory_material: List[str] = []
        with open(path, "r", encoding="utf-8", newline="") as stream:
            data_lines = [line for line in stream if line.strip() and not line.startswith("#")]
        reader = csv.DictReader(data_lines, delimiter="\t", quoting=csv.QUOTE_NONE)
        for row in reader:
            source_id = row["source_id"].strip()
            origin = row["origin"].strip()
            native_pattern = json.loads(row["native_pattern_json"])
            reason_cn = json.loads(row["reason_cn_json"])
            handling_group = row["handling_group"].strip()
            if source_id in seen_ids or native_pattern in seen_patterns:
                raise ValueError(f"duplicate native error mapping: {source_id}")
            if handling_group not in GROUP_PRESENTATION:
                raise ValueError(f"unknown handling group: {handling_group}")
            if not reason_cn or _BRAND_WORDS.search(reason_cn):
                raise ValueError(f"unsafe operator translation: {source_id}")
            regex, capture_count, literal_score = _compile_printf_pattern(native_pattern)
            entries.append(_MapEntry(
                source_id,
                origin,
                native_pattern,
                reason_cn,
                handling_group,
                regex,
                capture_count,
                literal_score,
            ))
            seen_ids.add(source_id)
            seen_patterns.add(native_pattern)
            inventory_material.append(f"{source_id}\t{native_pattern}\n")
        inventory_hash = hashlib.sha256("".join(inventory_material).encode("utf-8")).hexdigest()
        if len(entries) != EXPECTED_SOURCE_COUNT or inventory_hash != EXPECTED_INVENTORY_SHA256:
            raise ValueError(
                f"native error map coverage mismatch: count={len(entries)} sha256={inventory_hash}"
            )
        return cls(entries)

    def translate(self, raw_text: str) -> NativeOperatorMessage:
        normalized = _normalize(raw_text)
        fingerprint = _fingerprint(raw_text)
        for entry in self._entries:
            match = entry.regex.fullmatch(normalized)
            if not match:
                continue
            title_cn, next_cn = GROUP_PRESENTATION[entry.handling_group]
            if entry.handling_group == "INTERNAL":
                reason_cn = "控制系统内部状态异常"
            elif entry.handling_group == "PASSTHROUGH":
                return NativeOperatorMessage(
                    "NATIVE_UNKNOWN",
                    "控制系统错误",
                    "控制系统返回未识别错误",
                    "停止当前操作并联系维护人员",
                    fingerprint,
                    False,
                )
            else:
                reason_cn = _render_template(entry.reason_cn, match.groups())
            return NativeOperatorMessage(
                entry.source_id,
                _sanitize_operator_text(title_cn),
                _sanitize_operator_text(reason_cn),
                _sanitize_operator_text(next_cn),
                fingerprint,
                True,
            )
        return NativeOperatorMessage(
            "NATIVE_UNKNOWN",
            "控制系统错误",
            "控制系统返回未识别错误",
            "停止当前操作并联系维护人员",
            fingerprint,
            False,
        )


def poll_operator_error_events(channel, error_types, error_map, path: str, generation: int):
    published = 0
    while published < 32:
        event = channel.poll()
        if not event:
            break
        try:
            kind = int(event[0])
            raw_text = str(event[1])
        except Exception:
            continue
        if kind not in error_types:
            continue
        generation = (int(generation) + 1) & 0xFFFFFFFFFFFFFFFF
        if generation == 0:
            generation = 1
        message = error_map.translate(raw_text)
        write_operator_error_status(path, 1, kind, generation, message)
        print(
            f'v5_native_operator_error generation={generation} source_id={message.source_id} '
            f'fingerprint={message.fingerprint} matched={1 if message.matched else 0}',
            flush=True)
        published += 1
    return generation, published


def translate_internal_alias(code: str) -> NativeOperatorMessage:
    presentation = ALIAS_PRESENTATION.get(code or "")
    fingerprint = _fingerprint(code)
    if not presentation:
        return NativeOperatorMessage(
            "OWNER_UNKNOWN",
            "操作未完成",
            "当前操作返回未识别状态",
            "保持当前状态并联系维护人员",
            fingerprint,
            False,
        )
    title_cn, reason_cn, next_cn = presentation
    return NativeOperatorMessage(
        "OWNER_ALIAS",
        title_cn,
        reason_cn,
        next_cn,
        fingerprint,
        True,
    )
