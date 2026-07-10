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

static char v5_command_motion_axis_upper(char axis)
{
    return axis >= 'a' && axis <= 'z' ? (char)(axis - ('a' - 'A')) : axis;
}

static int v5_command_motion_axis_ok(char axis)
{
    axis = v5_command_motion_axis_upper(axis);
    return axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'A' || axis == 'B' || axis == 'C';
}

static const char *v5_command_motion_axis_text(char axis)
{
    axis = v5_command_motion_axis_upper(axis);
    return axis == 'X' ? "X" : axis == 'Y' ? "Y" : axis == 'Z' ? "Z" :
           axis == 'A' ? "A" : axis == 'B' ? "B" : axis == 'C' ? "C" : 0;
}

int v5_command_axis_zero_position_prepare(
    char axis,
    const char *coordinate_space,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    V5CommandRequest local_request;
    const char *axis_text;

    axis = v5_command_motion_axis_upper(axis);
    axis_text = v5_command_motion_axis_text(axis);
    if (!prepared || !axis_text || !coordinate_space ||
        (strcmp(coordinate_space, "mcs") != 0 && strcmp(coordinate_space, "wcs") != 0)) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_AXIS_ZERO_POSITION;
    local_request.text_value = axis_text;
    local_request.mode_value = coordinate_space;
    if (!v5_command_gate_prepare(&local_request, prepared)) {
        return 0;
    }
    if (request) {
        *request = local_request;
    }
    return 1;
}

int v5_command_jog_increment_prepare(char axis, double step, int positive, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;
    char axis_text[2];

    axis = v5_command_motion_axis_upper(axis);
    if (!prepared || !v5_command_motion_axis_ok(axis) || !isfinite(step) || step <= 0.0) {
        return 0;
    }
    axis_text[0] = axis;
    axis_text[1] = '\0';
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_INCREMENT;
    local_request.axis_value = positive ? 1.0 : -1.0;
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

int v5_command_jog_continuous_prepare(char axis, int positive, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;
    const char *axis_text;

    axis = v5_command_motion_axis_upper(axis);
    axis_text = v5_command_motion_axis_text(axis);
    if (!prepared || !axis_text) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_CONTINUOUS;
    local_request.axis_value = positive ? 1.0 : -1.0;
    local_request.text_value = axis_text;
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
    const char *axis_text;

    axis = v5_command_motion_axis_upper(axis);
    axis_text = v5_command_motion_axis_text(axis);
    if (!prepared || !axis_text) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_JOG_STOP;
    local_request.text_value = axis_text;
    if (!v5_command_gate_prepare(&local_request, prepared)) {
        return 0;
    }
    if (request) {
        *request = local_request;
    }
    return 1;
}
