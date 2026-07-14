#include "v5_native_operator_error_status.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "v5_native_operator_error_status_smoke.bin";
    V5NativeOperatorErrorStatus status;
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
        strstr(status.reason_cn, "不能执行回零") == NULL ||
        strstr(status.next_cn, "取消急停") == NULL) {
        return 9;
    }
    if (v5_native_operator_error_status_from_alias("UNKNOWN_ALIAS", &status)) {
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
