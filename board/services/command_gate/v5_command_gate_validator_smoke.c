#include "v5_command_gate_validator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void init_frame(V5CommandGateIpcRequestFrame *frame, V5CommandKind kind)
{
    memset(frame, 0, sizeof(*frame));
    frame->magic = V5_COMMAND_GATE_IPC_MAGIC;
    frame->version = V5_COMMAND_GATE_IPC_VERSION;
    frame->size = (uint32_t)sizeof(*frame);
    frame->op = V5_COMMAND_GATE_IPC_OP_EXECUTE;
    frame->kind = (int32_t)kind;
}

static int expect_accept(V5CommandGateIpcRequestFrame *frame)
{
    V5CommandRequest request;
    char reason[64];
    if (!v5_command_gate_validate_execute_frame(frame, &request, reason, sizeof(reason))) {
        printf("unexpected reject: %s\n", reason);
        return 0;
    }
    return 1;
}

static int expect_reject(V5CommandGateIpcRequestFrame *frame, const char *label)
{
    V5CommandRequest request;
    char reason[64];
    if (v5_command_gate_validate_execute_frame(frame, &request, reason, sizeof(reason))) {
        printf("unexpected accept: %s\n", label);
        return 0;
    }
    printf("reject %s: %s\n", label, reason);
    return 1;
}

int main(void)
{
    V5CommandGateIpcRequestFrame frame;

    init_frame(&frame, V5_COMMAND_WCS_SELECT);
    frame.index_value = 8;
    if (!expect_accept(&frame)) return 1;

    init_frame(&frame, V5_COMMAND_WCS_SELECT);
    frame.index_value = 9;
    if (!expect_reject(&frame, "bad_wcs")) return 2;

    init_frame(&frame, V5_COMMAND_MDI_RUN);
    snprintf(frame.text_value, sizeof(frame.text_value), "Set MDI G0 X0");
    if (!expect_reject(&frame, "raw_set")) return 3;

    init_frame(&frame, V5_COMMAND_JOG_CONTINUOUS);
    frame.index_value = 0;
    frame.axis_value = NAN;
    if (!expect_reject(&frame, "nan_axis_value")) return 4;

    init_frame(&frame, V5_COMMAND_WORK_ZERO);
    frame.index_value = 1;
    snprintf(frame.text_value, sizeof(frame.text_value), "B");
    if (!expect_reject(&frame, "bad_axis")) return 5;

    init_frame(&frame, V5_COMMAND_RTCP_SET);
    frame.enabled_value = 2;
    if (!expect_reject(&frame, "bad_rtcp")) return 6;

    init_frame(&frame, V5_COMMAND_FEED_OVERRIDE_SET);
    frame.index_value = 201;
    if (!expect_reject(&frame, "bad_override")) return 7;

    init_frame(&frame, V5_COMMAND_FIRST_POINT);
    frame.index_value = 1;
    frame.axis_mask = V5_COMMAND_AXIS_X_MASK;
    frame.point_axis[0] = 1.0;
    snprintf(frame.text_value, sizeof(frame.text_value), "/opt/8ax/v5/gcode/golden/cc.ngc");
    snprintf(frame.secondary_text_value, sizeof(frame.secondary_text_value),
             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    if (!expect_accept(&frame)) return 8;

    init_frame(&frame, V5_COMMAND_START);
    frame.axis_mask = V5_COMMAND_AXIS_X_MASK;
    if (!expect_reject(&frame, "stray_axis_mask")) return 9;

    init_frame(&frame, V5_COMMAND_START);
    if (!expect_reject(&frame, "start_missing_path")) return 10;

    init_frame(&frame, V5_COMMAND_START);
    snprintf(frame.text_value, sizeof(frame.text_value), "/opt/8ax/v5/gcode/golden/cc.ngc");
    if (!expect_accept(&frame)) return 11;

    printf("v5 command gate validator smoke passed\n");
    return 0;
}
