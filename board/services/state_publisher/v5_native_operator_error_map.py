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


EXPECTED_SOURCE_COUNT = 556
EXPECTED_INVENTORY_SHA256 = "0ba0c29826e96f54f574a8db9c6319d29f79c08082cacf170ea8bf4163e7b175"
OPERATOR_ERROR_MAGIC = 0x564F4552
OPERATOR_ERROR_VERSION = 2
OPERATOR_ERROR_PREFIX_STRUCT = struct.Struct('<IIIIIIQQ64s24s96s384s256s')
OPERATOR_ERROR_BLOCK_STRUCT = struct.Struct('<IIIIIIQQ64s24s96s384s256sII')
DEFAULT_OPERATOR_ERROR_PATH = '/dev/shm/v5_native_operator_error_status.bin'
DEFAULT_OPERATOR_ERROR_MAP = '/opt/8ax/v5/config/ui/v5_native_operator_error_map.tsv'

DISPLAY_LOG_ONLY = 1
DISPLAY_TOP_STATUS = 2
DISPLAY_POPUP = 3
VALID_DISPLAY_MODES = {DISPLAY_LOG_ONLY, DISPLAY_TOP_STATUS, DISPLAY_POPUP}

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
    "CONTROL_REALTIME": ("控制周期异常", "保持急停并重新启动；若再次出现，联系维护人员检查实时负载和总线状态"),
    "INTERNAL": ("控制系统内部错误", "停止当前运行并保留程序名和行号，联系维护人员处理"),
    "USER_PROGRAM": ("用户程序报告错误", "按用户程序给出的原因修改程序或现场条件后重新运行"),
    "PASSTHROUGH": ("控制系统报告错误", "停止当前动作并重新执行一次；若再次出现，联系维护人员"),
}

GROUP_DISPLAY_MODE = {group: DISPLAY_POPUP for group in GROUP_PRESENTATION}
GROUP_DISPLAY_MODE["MOTION_JOG"] = DISPLAY_TOP_STATUS
GROUP_DISPLAY_MODE["MACHINE_MODE"] = DISPLAY_TOP_STATUS
SOURCE_DISPLAY_MODE = {
    "MOTION_FMT_73BD5ADAF3CF": DISPLAY_LOG_ONLY,
    "MOTION_FMT_B1C1DF723CAD": DISPLAY_LOG_ONLY,
}
SOURCE_CAPTURE_DISPLAY_MODE = {
    (
        "TASK_FMT_71643D285DD0",
        ("EMC_TASK_PLAN_INIT",),
    ): DISPLAY_LOG_ONLY,
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
    "HOME_PRECONDITION_ESTOP": (
        "请先取消急停",
        "急停正在生效，控制系统未启动任何轴回零运动",
        "确认现场安全，点击右下角“取消急停”并确认机器使能后重新回零",
    ),
    "DISABLED": GROUP_PRESENTATION["SAFETY_ENABLE"][:1] + (
        "机器当前未上使能",
        GROUP_PRESENTATION["SAFETY_ENABLE"][1],
    ),
    "HOME_PRECONDITION_DISABLED": (
        "机器状态不允许回零",
        "机器尚未使能，控制系统未启动任何轴回零运动",
        "确认已取消急停且机器使能成功，再重新执行机械全轴回零",
    ),
    "HOME_PRECONDITION_MOVING": (
        "机器正在运动", "提交回零时仍有轴在运动，新的回零事务未开始",
        "先停止当前运动并等待所有轴静止，再重新执行机械全轴回零"),
    "HOME_PRECONDITION_SAFETY_UNAVAILABLE": (
        "安全状态不可用", "未取得有效的急停与机器使能状态，不能安全开始回零",
        "保持机器停止，恢复安全状态回读后确认急停和使能状态，再重新回零"),
    "HOME_PRECONDITION_RTCP_STATUS_UNAVAILABLE": (
        "RTCP状态不可用", "未取得本次回零所需的有效RTCP实际状态，不能安全开始运动",
        "保持机器停止，恢复RTCP实际状态回读并确认其已关闭后重新回零"),
    "HOME_PRECONDITION_FAILED": (
        "回零前置条件不满足", "回零前的安全或运动状态检查未通过，未开始轴运动",
        "确认已取消急停、机器已使能且所有轴静止后重新回零"),
    "ALL_HOME_NATIVE_CONFIG_INVALID": (
        "回零配置错误", "本次全轴回零的轴配置缺失或无效，未开始运动",
        "保持机器停止，检查轴参数、回零参数和启用轴后保存并重启，再重新回零"),
    "ALL_HOME_COUNT_READBACK_INVALID": (
        "回零反馈不可用", "未取得本次回零所需的有效驱动计数回读，不能计算或确认机械零位",
        "检查驱动在线状态、编码器反馈和计数映射，读取驱动正常后重新回零"),
    "ALL_HOME_LIMIT_ACTIVE": (
        "轴限位阻止回零", "目标轴的限位输入正在生效，回零运动已被阻止",
        "检查该轴软限位、硬限位开关和实际位置，排除限位后重新回零"),
    "HOME_CANCELLED": (
        "回零已取消", "本次回零已被操作员急停取消",
        "确认现场安全后取消急停，再重新执行机械全轴回零"),
    "HOME_CANCELLED_BY_ESTOP": (
        "回零已取消", "操作员按下急停，本次单轴定位已立即取消",
        "确认现场安全后取消急停，再重新执行所需回零动作"),
    "ALL_HOME_DRIVE_FAULT": (
        "驱动故障阻止回零", "目标轴驱动器报告故障，本次回零已停止",
        "查看具体故障轴，排除并清除驱动报警，读取驱动正常后重新回零"),
    "ALL_HOME_RTCP_FORCE_OFF_NOT_CONFIRMED": (
        "RTCP状态阻止回零", "回零前RTCP状态未能切换为关闭，未启动任何轴运动",
        "保持机器停止，确认RTCP实际状态为关闭后重新回零"),
    "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED": (
        "RTCP状态阻止回零", "回零前RTCP状态未能切换为关闭，未启动任何轴运动",
        "保持机器停止，确认RTCP实际状态为关闭后重新回零"),
    "ALL_HOME_AXIS_SLAVE_MAPPING_INVALID": (
        "运动模型与从站映射不一致",
        "当前运动模型的生效逻辑轴与轴参数中的从站映射缺失、重复或不一致，运动保持禁用",
        "进入设置页，在轴参数的从站下拉中逐轴选择与当前运动模型一致的从站，保存并重启后再使能、回零或启动"),
    "MOTION_MODEL_MAPPING_REVIEW_REQUIRED": (
        "请核对轴从站映射",
        "运动模型已切换保存；当前模型的默认轴从站映射已经自动更新",
        "可直接保存并重启，或先进入轴参数，在从站下拉中人工调整并确认全部生效轴"),
    "ALL_HOME_MOTION_NOT_STARTED": (
        "回零运动未启动", "完整回零位移已提交，但目标轴实际计数没有变化，未证明运动已启动",
        "检查机器使能、驱动状态、限位和轴映射，确认轴可运动后重新回零"),
    "ALL_HOME_MOTION_NOT_COMPLETE": (
        "回零运动未完成", "已经观察到轴运动，但未取得本次运动完成并稳定静止的有效证明",
        "保持机器停止，检查驱动反馈和运动状态；确认轴已静止后重新回零"),
    "ALL_HOME_SEQUENCE_NOT_STARTED": (
        "回零次序未启动", "控制系统未进入任何已登记的回零次序，未启动目标轴回零",
        "保持机器停止，由维护人员检查启用轴和回零次序配置后重新回零"),
    "ALL_HOME_DIRECT_SINGLE_JOINT_UNSUPPORTED": (
        "回零入口不正确", "当前请求不是允许的机械全轴回零方式，控制系统已拒绝执行",
        "关闭提示并从主页面选择机械全轴后重新回零；若再次出现请联系维护人员"),
    "ALL_HOME_ZERO_DELTA_MOVEMENT_UNPROVEN": (
        "回零运动未证明", "目标轴位移为零且没有本次真实运动证明，不能把旧零位当作回零成功",
        "先将该轴安全点动离开机械零位，再重新执行机械全轴回零"),
    "ALL_HOME_PLAN_STALE": (
        "回零执行计划已失效", "RTCP关闭后登记的轴位置或坐标窗口在运动前发生变化，控制系统已拒绝使用旧计划",
        "保持机器停止，确认轴静止且坐标窗口回读稳定后重新回零；若再次出现请联系维护人员"),
    "ALL_HOME_NATIVE_FAILED": (
        "回零失败", "控制系统报告本次全轴回零失败，但失败类别不在当前登记表中",
        "保持机器停止并联系维护人员核对本次回零结果后再重试"),
    "HOME_FAILED": (
        "回零失败", "本次回零返回失败，但没有取得更具体的失败原因",
        "保持机器停止并联系维护人员核对本次回零结果后再重试"),
    "AXIS_ZERO_REPORT_INVALID": (
        "单轴回零请求无效", "本次单轴回机械零或加工零的请求内容无效，未开始运动",
        "重新选择目标轴和坐标域后只执行一次回零；仍失败时联系维护人员"),
    "AXIS_ZERO_NOT_ATTEMPTED": (
        "单轴回零未执行", "本次单轴回机械零或加工零尚未提交到控制系统",
        "重新选择目标轴和坐标域后只执行一次回零"),
    "AXIS_ZERO_FAILED": (
        "单轴回零失败", "本次单轴回机械零或加工零返回失败，但没有取得更具体原因",
        "保持机器停止并联系维护人员核对本次单轴回零结果后再重试"),
    "AXIS_ZERO_REQUEST_INVALID": (
        "单轴回零请求无效", "目标轴或坐标域请求无效，控制系统未开始单轴运动",
        "重新选择一个有效轴及机械坐标或加工坐标后再回零"),
    "AXIS_ZERO_POSITION_INVALID": (
        "单轴回零目标无效", "单轴回零请求没有提供有效目标轴",
        "重新选择X、Y、Z、A、B或C轴后再回零"),
    "AXIS_ZERO_POSITION_MODE_INVALID": (
        "单轴回零坐标域无效", "单轴回零请求没有提供有效的机械坐标或加工坐标域",
        "重新选择机械坐标或加工坐标后再回零"),
    "AXIS_ZERO_PARAMETERS_UNAVAILABLE": (
        "单轴参数不可用", "未取得目标轴的有效运行参数，不能计算或执行单轴回零",
        "保持机器停止，检查该轴参数并保存重启后重新回零"),
    "AXIS_ZERO_START_POSITION_UNAVAILABLE": (
        "单轴位置不可用", "未取得目标轴本次运动前的有效实际位置",
        "检查轴反馈和控制状态，位置恢复正常后重新回零"),
    "AXIS_ZERO_WCS_READBACK_UNAVAILABLE": (
        "加工坐标状态不可用", "未取得当前加工坐标偏移回读，不能安全执行回加工零",
        "保持机器停止，恢复加工坐标偏移回读后重新执行回加工零"),
    "AXIS_ZERO_MDI_MODE_REJECTED": (
        "单轴运动模式被拒绝", "控制系统未能进入本次单轴定位所需的运动模式",
        "停止当前程序或任务，确认机器可手动运动后重新回零"),
    "AXIS_ZERO_PROOF_MOVE_NOT_CONFIRMED": (
        "单轴证明运动失败", "目标轴的本次证明位移没有取得真实运动与到位确认",
        "检查驱动、限位、轴映射和反馈，确认轴可安全运动后重新回零"),
    "AXIS_ZERO_ARRIVAL_NOT_CONFIRMED": (
        "单轴到位未确认", "目标轴没有取得本次运动完成、静止并到达登记零位的有效证明",
        "保持机器停止，检查实际位置和反馈后重新执行该轴回零"),
    "AXIS_ZERO_REAL_MOVE_NOT_CONFIRMED": (
        "单轴运动未确认", "本次单轴回零没有取得实际位置变化证明，不能把原位置当作成功",
        "先确认目标轴可运动并检查反馈，再重新执行该轴回零"),
    "AXIS_ZERO_WCS_OFFSET_CHANGED": (
        "加工坐标偏移异常", "回加工零过程中当前加工坐标偏移发生变化，本次结果已拒绝",
        "保持机器停止，检查加工坐标偏移来源并恢复稳定后重新回加工零"),
    "HOME_NATIVE_OWNER_FAILED": (
        "回零控制状态异常", "回零控制状态初始化失败，不能确认本次动作",
        "保持机器停止并联系维护人员恢复控制状态后再回零"),
    "HOME_WCHECKPOINT_AXIS_INVALID": (
        "旋转轴选择无效", "当前目标不是已登记的旋转轴，无法读取坐标窗口",
        "重新选择有效旋转轴后执行回机械零"),
    "HOME_WCHECKPOINT_READBACK_INVALID": (
        "旋转轴窗口回读无效", "未取得旋转轴坐标窗口的有效代次与平移基准",
        "保持机器停止，恢复旋转轴窗口回读后重新回零"),
    "HOME_WCHECKPOINT_RUNTIME_ACTUAL_INVALID": (
        "旋转轴实际位置无效", "未取得可与坐标窗口绑定的旋转轴运行实际位置",
        "检查旋转轴反馈和坐标窗口状态，恢复有效回读后重新回零"),
    "HOME_SAFE_ZERO_INPUT_INVALID": (
        "旋转轴零位参数无效", "旋转轴等效机械零计算缺少有效轴参数或实际计数",
        "保持机器停止，检查旋转轴参数与反馈后重新回零"),
    "HOME_SAFE_ZERO_RANGE_INVALID": (
        "旋转轴零位范围无效", "旋转轴每圈计数或整数范围无效，不能计算等效机械零",
        "检查每单位计数和每圈整数范围，保存重启后重新回零"),
    "HOME_SAFE_ZERO_TIE_AMBIGUOUS": (
        "旋转轴目标不唯一", "旋转轴到两个等效机械零位的距离相同，无法唯一选择最近目标",
        "先将该旋转轴安全点动离开半圈等距位置，再重新回零"),
    "HOME_SAFE_ZERO_GENERATION_MISMATCH": (
        "旋转轴窗口已变化", "到位检查使用的坐标窗口代次与本次固定目标不一致",
        "保持机器停止，等待坐标窗口稳定后重新回零"),
    "HOME_SAFE_ZERO_LOGICAL_TARGET_COUNT_MISMATCH": (
        "旋转轴逻辑到位超差", "旋转轴最终逻辑实际计数没有到达本次固定机械零目标",
        "检查旋转轴反馈、零点和计数参数，排除偏差后重新回零"),
    "HOME_SAFE_ZERO_PHASE_COUNT_MISMATCH": (
        "旋转轴等效零位超差", "旋转轴最终等效角计数没有落入机械零允许范围",
        "检查旋转轴每圈计数和零点模型，排除偏差后重新回零"),
    "HOME_SAFE_ZERO_REMAP_READBACK_INVALID": (
        "旋转轴重映射回读无效", "坐标窗口变化后未取得有效的新平移基准，不能继续本次回零",
        "保持机器停止，恢复坐标窗口回读后重新回零"),
    "HOME_SAFE_ZERO_REMAP_FAILED": (
        "旋转轴目标重映射失败", "坐标窗口变化后无法按固定逻辑零位重发剩余运动",
        "保持机器停止，检查旋转轴窗口与反馈后重新回零"),
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
    display_mode: int
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
    value = value.replace("底层控制", "系统")
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
    valid_value = 1 if (
        valid and message and message.reason_cn and message.display_mode in VALID_DISPLAY_MODES
    ) else 0
    monotonic_ns = time.monotonic_ns()
    fields = (
        OPERATOR_ERROR_MAGIC,
        OPERATOR_ERROR_VERSION,
        OPERATOR_ERROR_BLOCK_STRUCT.size,
        valid_value,
        int(kind) if valid_value else 0,
        int(message.display_mode) if valid_value else 0,
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
                    DISPLAY_POPUP,
                    fingerprint,
                    False,
                )
            else:
                reason_cn = _render_template(entry.reason_cn, match.groups())
            capture_key = (
                entry.source_id,
                tuple(_normalize(value) for value in match.groups()),
            )
            return NativeOperatorMessage(
                entry.source_id,
                _sanitize_operator_text(title_cn),
                _sanitize_operator_text(reason_cn),
                _sanitize_operator_text(next_cn),
                SOURCE_CAPTURE_DISPLAY_MODE.get(
                    capture_key,
                    SOURCE_DISPLAY_MODE.get(
                        entry.source_id,
                        GROUP_DISPLAY_MODE[entry.handling_group],
                    ),
                ),
                fingerprint,
                True,
            )
        return NativeOperatorMessage(
            "NATIVE_UNKNOWN",
            "控制系统错误",
            "控制系统返回未识别错误",
            "停止当前操作并联系维护人员",
            DISPLAY_POPUP,
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
            f'display_mode={message.display_mode} fingerprint={message.fingerprint} '
            f'matched={1 if message.matched else 0}',
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
            DISPLAY_POPUP,
            fingerprint,
            False,
        )
    title_cn, reason_cn, next_cn = presentation
    return NativeOperatorMessage(
        "OWNER_ALIAS",
        title_cn,
        reason_cn,
        next_cn,
        DISPLAY_POPUP,
        fingerprint,
        True,
    )
