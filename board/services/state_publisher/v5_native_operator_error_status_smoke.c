#include "v5_native_operator_error_status.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char *required_home_aliases[] = {
        "ALL_HOME_NATIVE_CONFIG_INVALID", "ALL_HOME_COUNT_READBACK_INVALID",
        "ALL_HOME_AXIS_X_CONFIG_INVALID", "ALL_HOME_AXIS_Y_CONFIG_INVALID",
        "ALL_HOME_AXIS_Z_CONFIG_INVALID", "ALL_HOME_AXIS_A_CONFIG_INVALID",
        "ALL_HOME_AXIS_B_CONFIG_INVALID", "ALL_HOME_AXIS_C_CONFIG_INVALID",
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
        "HOME_SAFE_ZERO_REMAP_READBACK_INVALID", "HOME_SAFE_ZERO_REMAP_FAILED"
    };
    static const char *retired_home_adapter_aliases[] = {
        "ALL_HOME_NOT_SUBMITTED", "ALL_HOME_FAILED", "ALL_HOME_REQUEST_INVALID",
        "ALL_HOME_NATIVE_STATUS_UNAVAILABLE", "ALL_HOME_INTERPRETER_CONTEXT_UNAVAILABLE",
        "ALL_HOME_ABORT_NOT_CONFIRMED", "ALL_HOME_MANUAL_MODE_NOT_CONFIRMED",
        "ALL_HOME_JOINT_MODE_NOT_CONFIRMED", "ALL_HOME_PROGRAM_IDENTITY_CHANGED",
        "ALL_HOME_SEND_FAILED", "ALL_HOME_NATIVE_RESULT_TIMEOUT",
        "ALL_HOME_TRANSACTION_SUPERSEDED", "ALL_HOME_NATIVE_MASK_MISMATCH",
        "ALL_HOME_ROTARY_EQUIVALENT_TIE", "ALL_HOME_COORDINATE_CONFIG_INVALID",
        "HOME_TRANSACTION_INVALID_OR_ACTIVE", "HOME_TRANSACTION_ALREADY_ACTIVE",
        "HOME_TRANSACTION_START_FAILED", "HOME_TERMINAL_STATUS_MISSING",
        "HOME_RESULT_MISSING", "HOME_STATUS_NOT_FOUND"
    };
    const char *path = "v5_native_operator_error_status_smoke.bin";
    V5NativeOperatorErrorStatus status;
    size_t alias_index;
    remove(path);
    v5_native_operator_error_status_init(&status);
    if (v5_native_operator_error_status_read(path, 1000U, &status)) {
        return 1;
    }
    if (!v5_native_operator_error_status_write(
            path,
            1,
            3U,
            V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP,
            7ULL,
            "TASK_FMT_6605BE5830FC",
            "6605BE5830FC",
            "需要回零",
            "未返回原点时无法运行程序",
            "选择机械全轴并完成本次开机回零，再重新执行当前动作")) {
        return 2;
    }
    if (!v5_native_operator_error_status_read(path, 1000U, &status) ||
        status.generation != 7ULL || status.kind != 3U ||
        status.display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
        strcmp(status.title_cn, "需要回零") != 0 ||
        strcmp(status.reason_cn, "未返回原点时无法运行程序") != 0) {
        return 3;
    }
    if (!v5_native_operator_error_status_write(
            path,
            1,
            3U,
            V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS,
            8ULL,
            "MOTION_FMT_46B58D27CD79",
            "46B58D27CD79",
            "点动未执行",
            "当前状态不允许点动",
            "松开点动按钮后重试") ||
        !v5_native_operator_error_status_read(path, 1000U, &status) ||
        status.generation != 8ULL ||
        status.display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS) {
        return 4;
    }
    if (!v5_native_operator_error_status_from_alias("POWER_ON_HOME_REQUIRED", &status) ||
        status.display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
        strcmp(status.title_cn, "需要回零") != 0 || strstr(status.reason_cn, "POWER_ON_HOME") != NULL) {
        return 5;
    }
    if (!v5_native_operator_error_status_from_alias("HOME_PRECONDITION_ESTOP", &status) ||
        status.display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
        strcmp(status.title_cn, "请先取消急停") != 0 ||
        strstr(status.reason_cn, "未启动任何轴回零运动") == NULL ||
        strstr(status.next_cn, "取消急停") == NULL) {
        return 9;
    }
    if (!v5_native_operator_error_status_from_alias(
            "ALL_HOME_AXIS_SLAVE_MAPPING_INVALID", &status) ||
        strcmp(status.title_cn, "运动模型与从站映射不一致") != 0 ||
        strstr(status.reason_cn, "运动保持禁用") == NULL ||
        strstr(status.next_cn, "使能、回零或启动") == NULL) {
        return 13;
    }
    if (!v5_native_operator_error_status_from_alias(
            "ALL_HOME_AXIS_B_CONFIG_INVALID", &status) ||
        strcmp(status.title_cn, "B轴回零配置无效") != 0 ||
        strstr(status.reason_cn, "B轴回零配置缺失或无效") == NULL ||
        strstr(status.next_cn, "彻底重启") == NULL) {
        return 14;
    }
    for (alias_index = 0U;
         alias_index < sizeof(required_home_aliases) / sizeof(required_home_aliases[0]);
         ++alias_index) {
        if (!v5_native_operator_error_status_from_alias(required_home_aliases[alias_index], &status) ||
            status.display_mode != V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
            !status.title_cn[0] || !status.reason_cn[0] || !status.next_cn[0] ||
            strstr(status.reason_cn, required_home_aliases[alias_index]) != NULL) {
            return 10;
        }
    }
    for (alias_index = 0U;
         alias_index < sizeof(retired_home_adapter_aliases) / sizeof(retired_home_adapter_aliases[0]);
         ++alias_index) {
        if (v5_native_operator_error_status_from_alias(
                retired_home_adapter_aliases[alias_index], &status)) {
            return 11;
        }
    }
    if (v5_native_operator_error_status_from_alias("UNKNOWN_ALIAS", &status) ||
        strcmp(status.source_id, "OWNER_UNKNOWN") != 0 ||
        strstr(status.reason_cn, "具体回零失败原因") == NULL) {
        return 6;
    }
    if (!v5_native_operator_error_status_write(path, 1, 3U, 0U, 9ULL, "BAD", "BAD", "错误", "错误", "处理") ||
        v5_native_operator_error_status_read(path, 1000U, &status)) {
        return 7;
    }
    if (!v5_native_operator_error_status_write(path, 0, 0U, 0U, 0ULL, 0, 0, 0, 0, 0) ||
        v5_native_operator_error_status_read(path, 1000U, &status)) {
        return 8;
    }
    remove(path);
    puts("v5_native_operator_error_status_smoke PASS");
    return 0;
}
