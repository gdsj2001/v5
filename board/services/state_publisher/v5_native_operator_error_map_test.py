#!/usr/bin/env python3

from pathlib import Path
import re

from v5_native_operator_error_map import (
    ALIAS_PRESENTATION,
    DISPLAY_LOG_ONLY,
    DISPLAY_POPUP,
    DISPLAY_TOP_STATUS,
    EXPECTED_SOURCE_COUNT,
    NativeOperatorErrorMap,
    _PRINTF_TOKEN,
    translate_internal_alias,
)


ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = ROOT / "config/ui/v5_native_operator_error_map.tsv"
FORBIDDEN = re.compile(r"linuxcnc|linuxcncrsh|\bemc\b|\bhal\b|\bnml\b|motmod|rtapi|gdb|python", re.I)
FORBIDDEN_CN = re.compile(r"底层控制|内部进程|回溯路径")


def sample_for_pattern(pattern: str) -> str:
    output = []
    index = 0
    while index < len(pattern):
        if pattern[index] != "%":
            output.append(pattern[index])
            index += 1
            continue
        if index + 1 < len(pattern) and pattern[index + 1] == "%":
            output.append("%")
            index += 2
            continue
        token = _PRINTF_TOKEN.match(pattern, index)
        if not token:
            output.append("%")
            index += 1
            continue
        kind = token.group("kind")
        if kind == "c":
            output.append("X")
        elif kind in "diu":
            output.append("7")
        elif kind in "oxX":
            output.append("1A")
        elif kind in "fFeEgGaA":
            output.append("1.25")
        elif kind == "p":
            output.append("0x1A")
        else:
            output.append("VALUE")
        index = token.end()
    return "".join(output)


def assert_safe(message) -> None:
    assert message.title_cn
    assert message.reason_cn
    assert message.next_cn
    assert not FORBIDDEN.search(message.title_cn)
    assert not FORBIDDEN.search(message.reason_cn)
    assert not FORBIDDEN.search(message.next_cn)
    assert not FORBIDDEN_CN.search(message.title_cn)
    assert not FORBIDDEN_CN.search(message.reason_cn)
    assert not FORBIDDEN_CN.search(message.next_cn)
    assert message.display_mode in {DISPLAY_LOG_ONLY, DISPLAY_TOP_STATUS, DISPLAY_POPUP}


def main() -> int:
    error_map = NativeOperatorErrorMap.from_tsv(str(MAP_PATH))
    assert error_map.source_count == EXPECTED_SOURCE_COUNT
    checked = 0
    for entry in error_map._entries:
        sample = sample_for_pattern(entry.native_pattern)
        message = error_map.translate(sample)
        assert_safe(message)
        if entry.handling_group == "PASSTHROUGH":
            assert not message.matched
        else:
            assert message.matched, entry.source_id
            assert message.source_id == entry.source_id, (entry.source_id, message.source_id, sample)
            if entry.handling_group == "MACHINE_MODE":
                assert message.display_mode == DISPLAY_TOP_STATUS, entry.source_id
        checked += 1
    home = error_map.translate("Can't run a program when not homed")
    assert home.matched and home.title_cn == "需要回零"
    assert home.display_mode == DISPLAY_POPUP
    assert "原点" in home.reason_cn
    jog = error_map.translate("Can't jog joint when not enabled.")
    assert jog.matched and jog.display_mode == DISPLAY_TOP_STATUS
    jog_stopped = error_map.translate("Jog aborted by jog-stop-immediate")
    assert jog_stopped.matched and jog_stopped.display_mode == DISPLAY_LOG_ONLY
    redundant_plan_init = error_map.translate(
        "can't do that (EMC_TASK_PLAN_INIT) in auto mode with the interpreter waiting"
    )
    assert redundant_plan_init.matched
    assert redundant_plan_init.source_id == "TASK_FMT_71643D285DD0"
    assert redundant_plan_init.display_mode == DISPLAY_LOG_ONLY
    other_waiting_mode_request = error_map.translate(
        "can't do that (EMC_TASK_PLAN_OPEN) in auto mode with the interpreter waiting"
    )
    assert other_waiting_mode_request.matched
    assert other_waiting_mode_request.source_id == "TASK_FMT_71643D285DD0"
    assert other_waiting_mode_request.display_mode == DISPLAY_TOP_STATUS
    mcode = error_map.translate("Unknown m code used: M123")
    assert mcode.matched and "M123" in mcode.reason_cn
    for axis in "abcuvwxyz":
        model_axis = error_map.translate(f"Bad character '{axis}' used")
        assert model_axis.matched
        assert model_axis.source_id == "INTERPRETER_FMT_8B4A844A680D"
        assert model_axis.title_cn == "程序与运动模型不匹配"
        assert f"{axis.upper()}轴" in model_axis.reason_cn
        assert "切换运动模型" in model_axis.next_cn
    unsupported_word = error_map.translate("Bad character 'n' used")
    assert unsupported_word.matched
    assert unsupported_word.title_cn == "程序格式错误"
    assert "地址字n" in unsupported_word.reason_cn
    non_printable = error_map.translate("Bad character '\\001' used")
    assert non_printable.matched
    assert non_printable.source_id == "INTERPRETER_FMT_E58D8E4C4581"
    internal = error_map.translate("invalid V5 wrapped rotary target state")
    assert internal.matched and "旋转轴" in internal.reason_cn
    native_parameter = error_map.translate("Named hal parameter #<axis> not found")
    assert native_parameter.matched and "底层控制" not in native_parameter.reason_cn
    realtime_delay = error_map.translate(
        "Unexpected realtime delay on task 0 with period 2000000\n"
        "This Message will only display once per session.\n"
        "Run the Latency Test and resolve before continuing.\n"
    )
    assert realtime_delay.matched
    assert realtime_delay.source_id == "RTAPI_FMT_095C0B454AF3"
    assert realtime_delay.display_mode == DISPLAY_POPUP
    assert realtime_delay.title_cn == "控制周期异常"
    assert "2000000纳秒" in realtime_delay.reason_cn
    assert "task 0" not in realtime_delay.reason_cn
    assert "保持急停" in realtime_delay.next_cn
    unknown = error_map.translate("LinuxCNC private process /internal/path failed")
    assert not unknown.matched
    assert unknown.display_mode == DISPLAY_POPUP
    assert "尚未登记" in unknown.reason_cn
    assert "参考编号" not in unknown.reason_cn
    assert_safe(unknown)
    alias = translate_internal_alias("POWER_ON_HOME_REQUIRED")
    assert alias.matched and alias.title_cn == "需要回零"
    assert alias.display_mode == DISPLAY_POPUP
    assert "POWER_ON_HOME" not in alias.reason_cn
    estop_home = translate_internal_alias("HOME_PRECONDITION_ESTOP")
    assert estop_home.matched and estop_home.title_cn == "请先取消急停"
    assert estop_home.display_mode == DISPLAY_POPUP
    assert "未启动任何轴回零运动" in estop_home.reason_cn
    assert "取消急停" in estop_home.next_cn
    model_review = translate_internal_alias("MOTION_MODEL_MAPPING_REVIEW_REQUIRED")
    assert model_review.matched
    assert model_review.title_cn == "请核对轴从站映射"
    assert "已经自动更新" in model_review.reason_cn
    assert "人工调整" in model_review.next_cn
    mapping_invalid = translate_internal_alias("ALL_HOME_AXIS_SLAVE_MAPPING_INVALID")
    assert mapping_invalid.matched
    assert mapping_invalid.title_cn == "运动模型与从站映射不一致"
    assert "运动保持禁用" in mapping_invalid.reason_cn
    assert "使能、回零或启动" in mapping_invalid.next_cn
    required_home_aliases = {
        "ALL_HOME_NATIVE_CONFIG_INVALID", "ALL_HOME_COUNT_READBACK_INVALID",
        "ALL_HOME_LIMIT_ACTIVE", "HOME_CANCELLED",
        "HOME_PRECONDITION_ESTOP", "HOME_PRECONDITION_DISABLED", "ALL_HOME_DRIVE_FAULT",
        "ALL_HOME_RTCP_FORCE_OFF_NOT_CONFIRMED", "HOME_PRECONDITION_MOVING",
        "HOME_PRECONDITION_SAFETY_UNAVAILABLE", "ALL_HOME_AXIS_SLAVE_MAPPING_INVALID",
        "HOME_PRECONDITION_RTCP_STATUS_UNAVAILABLE", "ALL_HOME_MOTION_NOT_STARTED",
        "ALL_HOME_MOTION_NOT_COMPLETE", "ALL_HOME_SEQUENCE_NOT_STARTED",
        "ALL_HOME_DIRECT_SINGLE_JOINT_UNSUPPORTED", "ALL_HOME_ZERO_DELTA_MOVEMENT_UNPROVEN",
        "ALL_HOME_PLAN_STALE", "ALL_HOME_NATIVE_FAILED",
        "AXIS_ZERO_REQUEST_INVALID", "AXIS_ZERO_PARAMETERS_UNAVAILABLE",
        "AXIS_ZERO_START_POSITION_UNAVAILABLE", "AXIS_ZERO_WCS_READBACK_UNAVAILABLE",
        "AXIS_ZERO_MDI_MODE_REJECTED", "AXIS_ZERO_PROOF_MOVE_NOT_CONFIRMED",
        "AXIS_ZERO_ARRIVAL_NOT_CONFIRMED", "AXIS_ZERO_REAL_MOVE_NOT_CONFIRMED",
        "AXIS_ZERO_WCS_OFFSET_CHANGED", "AXIS_ZERO_REPORT_INVALID",
        "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED", "HOME_WCHECKPOINT_AXIS_INVALID",
        "HOME_WCHECKPOINT_READBACK_INVALID", "HOME_WCHECKPOINT_RUNTIME_ACTUAL_INVALID",
        "HOME_SAFE_ZERO_INPUT_INVALID", "HOME_SAFE_ZERO_RANGE_INVALID",
        "HOME_SAFE_ZERO_TIE_AMBIGUOUS", "HOME_SAFE_ZERO_GENERATION_MISMATCH",
        "HOME_SAFE_ZERO_LOGICAL_TARGET_COUNT_MISMATCH", "HOME_SAFE_ZERO_PHASE_COUNT_MISMATCH",
        "HOME_SAFE_ZERO_REMAP_READBACK_INVALID", "HOME_SAFE_ZERO_REMAP_FAILED",
    }
    for code in sorted(required_home_aliases):
        translated = translate_internal_alias(code)
        assert translated.matched, code
        assert translated.display_mode == DISPLAY_POPUP, code
        assert translated.title_cn and translated.reason_cn and translated.next_cn, code
        assert code not in translated.reason_cn and code not in translated.next_cn, code
    assert required_home_aliases <= set(ALIAS_PRESENTATION)
    retired_home_adapter_aliases = {
        "ALL_HOME_NOT_SUBMITTED", "ALL_HOME_FAILED", "ALL_HOME_REQUEST_INVALID",
        "ALL_HOME_NATIVE_STATUS_UNAVAILABLE", "ALL_HOME_INTERPRETER_CONTEXT_UNAVAILABLE",
        "ALL_HOME_ABORT_NOT_CONFIRMED", "ALL_HOME_MANUAL_MODE_NOT_CONFIRMED",
        "ALL_HOME_JOINT_MODE_NOT_CONFIRMED", "ALL_HOME_PROGRAM_IDENTITY_CHANGED",
        "ALL_HOME_SEND_FAILED", "ALL_HOME_NATIVE_RESULT_TIMEOUT",
        "ALL_HOME_TRANSACTION_SUPERSEDED", "ALL_HOME_NATIVE_MASK_MISMATCH",
        "ALL_HOME_ROTARY_EQUIVALENT_TIE", "ALL_HOME_COORDINATE_CONFIG_INVALID",
        "HOME_TRANSACTION_INVALID_OR_ACTIVE", "HOME_TRANSACTION_ALREADY_ACTIVE",
        "HOME_TRANSACTION_START_FAILED", "HOME_TERMINAL_STATUS_MISSING",
        "HOME_RESULT_MISSING", "HOME_STATUS_NOT_FOUND",
    }
    for code in sorted(retired_home_adapter_aliases):
        assert code not in ALIAS_PRESENTATION, code
        assert not translate_internal_alias(code).matched, code
    print(f"v5_native_operator_error_map_test PASS count={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
