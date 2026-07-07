#include "v5_command_motion.h"

#include <math.h>
#include <string.h>

static int v5_command_motion_prepare(
    V5CommandKind kind,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = kind;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_pause_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_motion_prepare(V5_COMMAND_PAUSE, prepared, request);
}

int v5_command_home_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_motion_prepare(V5_COMMAND_HOME, prepared, request);
}

int v5_command_resume_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_motion_prepare(V5_COMMAND_RESUME, prepared, request);
}


static int v5_command_motion_axis_ok(char axis)
{
    return axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'A' || axis == 'C';
}

static int v5_command_motion_axis_joint(char axis)
{
    switch (axis) {
    case 'X':
        return 0;
    case 'Y':
        return 1;
    case 'Z':
        return 2;
    case 'A':
        return 3;
    case 'C':
        return 4;
    default:
        return -1;
    }
}

static const char *v5_command_motion_axis_text(char axis)
{
    return axis == 'X' ? "X" : axis == 'Y' ? "Y" : axis == 'Z' ? "Z" : axis == 'A' ? "A" : axis == 'C' ? "C" : 0;
}

int v5_command_jog_increment_prepare(char axis, double step, int positive, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;
    char axis_text[2];

    if (!prepared || !v5_command_motion_axis_ok(axis) || !isfinite(step) || step <= 0.0) {
        return 0;
    }
    axis_text[0] = axis;
    axis_text[1] = '\0';
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_INCREMENT;
    local_request.axis_value = positive ? 100.0 : -100.0;
    local_request.increment_value = step;
    local_request.text_value = axis_text;
    if (!v5_command_gate_prepare(&local_request, prepared)) {
        return 0;
    }
    if (request) {
        *request = local_request;
        request->text_value = v5_command_motion_axis_text(axis);
    }
    return 1;
}

int v5_command_jog_continuous_prepare(char axis, double velocity, int positive, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;
    int joint = v5_command_motion_axis_joint(axis);

    if (!prepared || joint < 0 || !isfinite(velocity) || velocity <= 0.0) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_CONTINUOUS;
    local_request.index_value = joint;
    local_request.axis_value = positive ? velocity : -velocity;
    local_request.text_value = v5_command_motion_axis_text(axis);
    if (!v5_command_gate_prepare(&local_request, prepared)) {
        return 0;
    }
    if (request) {
        *request = local_request;
    }
    return 1;
}

int v5_command_jog_stop_prepare(char axis, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;
    int joint = v5_command_motion_axis_joint(axis);

    if (!prepared || joint < 0) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_STOP;
    local_request.index_value = joint;
    local_request.text_value = v5_command_motion_axis_text(axis);
    if (!v5_command_gate_prepare(&local_request, prepared)) {
        return 0;
    }
    if (request) {
        *request = local_request;
    }
    return 1;
}
