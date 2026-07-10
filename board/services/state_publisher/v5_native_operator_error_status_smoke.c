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
        strcmp(status.title_cn, "需要回零") != 0 ||
        strcmp(status.reason_cn, "未返回原点时无法运行程序") != 0) {
        return 3;
    }
    if (!v5_native_operator_error_status_from_alias("POWER_ON_HOME_REQUIRED", &status) ||
        strcmp(status.title_cn, "需要回零") != 0 || strstr(status.reason_cn, "POWER_ON_HOME") != NULL) {
        return 4;
    }
    if (v5_native_operator_error_status_from_alias("UNKNOWN_ALIAS", &status)) {
        return 5;
    }
    if (!v5_native_operator_error_status_write(path, 0, 0U, 0ULL, 0, 0, 0, 0, 0) ||
        v5_native_operator_error_status_read(path, 1000U, &status)) {
        return 6;
    }
    remove(path);
    puts("v5_native_operator_error_status_smoke PASS");
    return 0;
}
