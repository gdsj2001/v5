#include "v5_native_work_zero.h"

#include <stdio.h>
#include <string.h>

static void good_status(V5NativeReadback *modal_tool, V5NativeReadback *rtcp)
{
    v5_native_readback_init(modal_tool);
    v5_native_readback_set_interpreter_idle(modal_tool, 1);
    v5_native_readback_set_interpreter_paused(modal_tool, 0);
    v5_native_readback_set_mdi_run_actual(modal_tool, 0, 0, "");
    v5_native_readback_set_modal_actual(
        modal_tool,
        "G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97");
    v5_native_readback_set_tool_actual(modal_tool, 0, 1, 0.0);
    v5_native_readback_init(rtcp);
    v5_native_readback_set_rtcp_actual(rtcp, 0);
}

int main(void)
{
    V5NativeReadback modal_tool;
    V5NativeReadback rtcp;
    char code[64];

    good_status(&modal_tool, &rtcp);
    if (!v5_native_work_zero_preflight(&modal_tool, &rtcp, code, sizeof(code)) ||
        strcmp(code, "WORK_ZERO_PREFLIGHT_OK") != 0) {
        return 1;
    }
    modal_tool.interpreter_idle = 0;
    if (v5_native_work_zero_preflight(&modal_tool, &rtcp, code, sizeof(code)) ||
        strcmp(code, "WORK_ZERO_MACHINE_NOT_IDLE") != 0) {
        return 2;
    }
    good_status(&modal_tool, &rtcp);
    rtcp.rtcp_enabled = 1;
    if (v5_native_work_zero_preflight(&modal_tool, &rtcp, code, sizeof(code)) ||
        strcmp(code, "WORK_ZERO_RTCP_ACTIVE") != 0) {
        return 3;
    }
    good_status(&modal_tool, &rtcp);
    v5_native_readback_set_modal_actual(&modal_tool, "G0 G17 G21 G40 G43 G54 G90");
    if (v5_native_work_zero_preflight(&modal_tool, &rtcp, code, sizeof(code)) ||
        strcmp(code, "WORK_ZERO_TOOL_COMPENSATION_ACTIVE_OR_UNKNOWN") != 0) {
        return 4;
    }
    if (!v5_native_work_zero_coordinate_frame_clear(123.0, 23.0, 100.0) ||
        v5_native_work_zero_coordinate_frame_clear(123.0, 22.0, 100.0)) {
        return 5;
    }
    puts("v5_native_work_zero_smoke: ok");
    return 0;
}
