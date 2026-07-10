#include "v5_native_axis_zero_position.h"

#include "v5_native_readback.h"
#include "v5_native_wcs_status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_AXIS_ZERO_TARGET_TOLERANCE 0.001
#define V5_AXIS_ZERO_COMPARE_EPSILON 1.0e-9
#define V5_AXIS_ZERO_MOTION_TOLERANCE 0.0001
#define V5_AXIS_ZERO_LINEAR_PROOF 0.1
#define V5_AXIS_ZERO_ROTARY_PROOF 1.0
#define V5_AXIS_ZERO_WAIT_ATTEMPTS 2400U
#define V5_AXIS_ZERO_WAIT_US 50000U

static void result_init(V5NativeAxisZeroPositionResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->code, sizeof(result->code), "%s", "AXIS_ZERO_NOT_ATTEMPTED");
}

static void result_code(V5NativeAxisZeroPositionResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s", code ? code : "AXIS_ZERO_FAILED");
    }
}

static int request_ok(const V5CommandPrepared *prepared, const V5CommandRequest *request)
{
    char axis;
    if (!prepared || !request || !prepared->accepted ||
        request->kind != V5_COMMAND_AXIS_ZERO_POSITION ||
        !prepared->name || strcmp(prepared->name, "axis_zero_position") != 0 ||
        !prepared->owner || strcmp(prepared->owner, "native_axis_zero_position") != 0 ||
        !request->text_value || request->text_value[1] || !request->mode_value) {
        return 0;
    }
    axis = request->text_value[0];
    return (axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'A' || axis == 'B' || axis == 'C') &&
           (strcmp(request->mode_value, "mcs") == 0 || strcmp(request->mode_value, "wcs") == 0);
}

static int rotary_axis(char axis)
{
    return axis == 'A' || axis == 'B' || axis == 'C';
}

static double target_error(char axis, double current, double target)
{
    double error = fabs(current - target);
    if (!isfinite(error)) {
        return HUGE_VAL;
    }
    if (rotary_axis(axis)) {
        error = fmod(error, 360.0);
        if (error > 180.0) {
            error = 360.0 - error;
        }
    }
    return error;
}

static int wait_position(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double before,
    double target,
    int *moved,
    double *position_out)
{
#ifdef _WIN32
    (void)config;
    (void)axis;
    (void)relative;
    (void)before;
    (void)target;
    (void)moved;
    (void)position_out;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < V5_AXIS_ZERO_WAIT_ATTEMPTS; ++attempt) {
        double current;
        if (v5_linuxcncrsh_get_axis_position(config, axis, relative, &current)) {
            if (fabs(current - before) > V5_AXIS_ZERO_MOTION_TOLERANCE && moved) {
                *moved = 1;
            }
            if (target_error(axis, current, target) <=
                V5_AXIS_ZERO_TARGET_TOLERANCE + V5_AXIS_ZERO_COMPARE_EPSILON) {
                ++stable;
                if (stable >= 3U) {
                    if (position_out) {
                        *position_out = current;
                    }
                    return 1;
                }
            } else {
                stable = 0U;
            }
        }
        usleep(V5_AXIS_ZERO_WAIT_US);
    }
    return 0;
#endif
}

static int send_target(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double target)
{
    char line[96];
    int rc = snprintf(
        line,
        sizeof(line),
        relative ? "Set MDI G0 %c%.6f" : "Set MDI G53 G0 %c%.6f",
        axis,
        target);
    return rc > 0 && (size_t)rc < sizeof(line) &&
           v5_linuxcncrsh_send_line(config, line) == V5_LINUXCNCRSH_SEND_SENT;
}

static int read_wcs_snapshot(V5NativeReadback *readback)
{
    v5_native_readback_init(readback);
    return v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, readback) &&
           v5_native_readback_wcs_table_known(readback);
}

static int wcs_offset_same(
    const V5NativeReadback *before,
    const V5NativeReadback *after)
{
    const double *before_offsets;
    const double *after_offsets;
    unsigned int slot;
    if (!before || !after || before->wcs_index != after->wcs_index) {
        return 0;
    }
    before_offsets = v5_native_readback_active_wcs_offsets(before);
    after_offsets = v5_native_readback_active_wcs_offsets(after);
    if (!before_offsets || !after_offsets) {
        return 0;
    }
    for (slot = 0U; slot < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++slot) {
        if (fabs(before_offsets[slot] - after_offsets[slot]) > 1.0e-9) {
            return 0;
        }
    }
    return 1;
}

int v5_native_axis_zero_position_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    int rc;
    if (!out || out_size == 0U || !request_ok(prepared, request)) {
        return 0;
    }
    rc = snprintf(out, out_size, "native_axis_zero_position %s %s0",
                  request->mode_value, request->text_value);
    return rc > 0 && (size_t)rc < out_size;
}

V5LinuxcncrshSendStatus v5_native_axis_zero_position_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5NativeAxisZeroPositionResult *result)
{
    const V5NativeMotionAxisParameters *axis_parameters;
    V5NativeReadback before_wcs;
    V5NativeReadback after_wcs;
    char axis;
    int relative;
    int moved = 0;
    double before_abs;
    double before_position;
    double initial_position;
    double after_position = 0.0;

    result_init(result);
    if (!request_ok(prepared, request)) {
        result_code(result, "AXIS_ZERO_REQUEST_INVALID");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    axis = request->text_value[0];
    relative = strcmp(request->mode_value, "wcs") == 0;
    axis_parameters = v5_native_motion_parameters_axis(parameters, axis);
    if (!axis_parameters) {
        result_code(result, "AXIS_ZERO_PARAMETERS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (!v5_linuxcncrsh_get_axis_position(config, axis, 0, &before_abs) ||
        !v5_linuxcncrsh_get_axis_position(config, axis, relative, &before_position)) {
        result_code(result, "AXIS_ZERO_START_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    initial_position = before_position;
    if (relative && !read_wcs_snapshot(&before_wcs)) {
        result_code(result, "AXIS_ZERO_WCS_READBACK_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode MDI") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "AXIS_ZERO_MDI_MODE_REJECTED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (target_error(axis, before_position, 0.0) <=
        V5_AXIS_ZERO_TARGET_TOLERANCE + V5_AXIS_ZERO_COMPARE_EPSILON) {
        double delta = rotary_axis(axis) ? V5_AXIS_ZERO_ROTARY_PROOF : V5_AXIS_ZERO_LINEAR_PROOF;
        double proof_target = delta;
        if (before_abs + delta >= axis_parameters->max_limit) {
            proof_target = -delta;
        }
        if (before_abs + proof_target <= axis_parameters->min_limit ||
            before_abs + proof_target >= axis_parameters->max_limit ||
            !send_target(config, axis, relative, proof_target) ||
            !wait_position(config, axis, relative, before_position, proof_target, &moved, 0)) {
            result_code(result, "AXIS_ZERO_PROOF_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        before_position = proof_target;
    }
    if (!send_target(config, axis, relative, 0.0) ||
        !wait_position(config, axis, relative, before_position, 0.0, &moved, &after_position)) {
        result_code(result, "AXIS_ZERO_ARRIVAL_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!moved) {
        result_code(result, "AXIS_ZERO_REAL_MOVE_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (relative) {
        if (!read_wcs_snapshot(&after_wcs) ||
            !wcs_offset_same(&before_wcs, &after_wcs)) {
            result_code(result, "AXIS_ZERO_WCS_OFFSET_CHANGED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (result) {
            result->wcs_offset_unchanged = 1;
        }
    }
    if (result) {
        result->movement_confirmed = 1;
        result->arrival_confirmed = 1;
        result->before_position = relative ? initial_position : before_abs;
        result->after_position = after_position;
    }
    result_code(result, relative ? "WCS_AXIS_ZERO_MOVE_CONFIRMED" : "MCS_AXIS_ZERO_MOVE_CONFIRMED");
    return V5_LINUXCNCRSH_SEND_SENT;
}
