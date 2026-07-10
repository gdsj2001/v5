#include "v5_native_work_zero.h"

#include "v5_native_readback.h"
#include "v5_native_wcs_status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_WORK_ZERO_POSITION_TOLERANCE 0.001
#define V5_WORK_ZERO_WAIT_ATTEMPTS 100U
#define V5_WORK_ZERO_WAIT_US 50000U

static void result_init(V5NativeWorkZeroResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->code, sizeof(result->code), "%s", "WORK_ZERO_NOT_ATTEMPTED");
}

static void result_code(V5NativeWorkZeroResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s", code ? code : "WORK_ZERO_FAILED");
    }
}

static int request_ok(const V5CommandPrepared *prepared, const V5CommandRequest *request)
{
    char axis;
    if (!prepared || !request || !prepared->accepted ||
        request->kind != V5_COMMAND_WORK_ZERO ||
        !prepared->name || strcmp(prepared->name, "work_zero") != 0 ||
        !prepared->owner || strcmp(prepared->owner, "native_work_zero") != 0 ||
        request->index_value < 1 || request->index_value > 9 ||
        !request->text_value || request->text_value[1]) {
        return 0;
    }
    axis = request->text_value[0];
    return axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'A' || axis == 'B' || axis == 'C';
}

static int read_wcs(V5NativeReadback *readback)
{
    v5_native_readback_init(readback);
    return v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, readback) &&
           v5_native_readback_wcs_table_known(readback);
}

V5LinuxcncrshSendStatus v5_native_work_zero_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5NativeWorkZeroResult *result)
{
#ifdef _WIN32
    (void)config;
    (void)parameters;
    (void)prepared;
    (void)request;
    result_init(result);
    result_code(result, "WORK_ZERO_UNAVAILABLE_ON_WIN32");
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    const V5NativeMotionAxisParameters *axis_parameters;
    V5NativeReadback before_wcs;
    V5NativeReadback after_wcs;
    char axis;
    char line[96];
    double before_abs;
    double before_rel;
    unsigned int attempt;
    unsigned int stable = 0U;
    int rc;

    result_init(result);
    if (!request_ok(prepared, request)) {
        result_code(result, "WORK_ZERO_REQUEST_INVALID");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    axis = request->text_value[0];
    axis_parameters = v5_native_motion_parameters_axis(parameters, axis);
    if (!axis_parameters || axis_parameters->status_slot >= V5_NATIVE_READBACK_WCS_AXIS_COUNT) {
        result_code(result, "WORK_ZERO_AXIS_PARAMETERS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (!read_wcs(&before_wcs) || before_wcs.wcs_index != request->index_value - 1) {
        result_code(result, "WORK_ZERO_ACTIVE_WCS_MISMATCH");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_get_axis_position(config, axis, 0, &before_abs) ||
        !v5_linuxcncrsh_get_axis_position(config, axis, 1, &before_rel)) {
        result_code(result, "WORK_ZERO_START_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (result) {
        result->before_generation = before_wcs.wcs_offsets_epoch;
    }
    rc = snprintf(line, sizeof(line), "Set MDI G10 L20 P%d %c0", request->index_value, axis);
    if (rc <= 0 || (size_t)rc >= sizeof(line) ||
        v5_linuxcncrsh_send_line(config, "Set Mode MDI") != V5_LINUXCNCRSH_SEND_SENT ||
        v5_linuxcncrsh_send_line(config, line) != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "WORK_ZERO_OWNER_SUBMIT_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (result) {
        result->persistent_owner_submitted = 1;
    }
    for (attempt = 0U; attempt < V5_WORK_ZERO_WAIT_ATTEMPTS; ++attempt) {
        double after_abs;
        double after_rel;
        const double *before_offsets;
        const double *after_offsets;
        int selected_offset_confirmed = 0;
        before_offsets = v5_native_readback_active_wcs_offsets(&before_wcs);
        if (v5_linuxcncrsh_get_axis_position(config, axis, 0, &after_abs) &&
            v5_linuxcncrsh_get_axis_position(config, axis, 1, &after_rel) &&
            read_wcs(&after_wcs) && after_wcs.wcs_index == before_wcs.wcs_index) {
            after_offsets = v5_native_readback_active_wcs_offsets(&after_wcs);
            selected_offset_confirmed = before_offsets && after_offsets &&
                (fabs(before_rel) <= V5_WORK_ZERO_POSITION_TOLERANCE ||
                 fabs(after_offsets[axis_parameters->status_slot] -
                      before_offsets[axis_parameters->status_slot]) > V5_WORK_ZERO_POSITION_TOLERANCE);
        }
        if (selected_offset_confirmed &&
            fabs(after_abs - before_abs) <= V5_WORK_ZERO_POSITION_TOLERANCE &&
            fabs(after_rel) <= V5_WORK_ZERO_POSITION_TOLERANCE &&
            (after_wcs.wcs_offsets_epoch != before_wcs.wcs_offsets_epoch ||
             fabs(before_rel) <= V5_WORK_ZERO_POSITION_TOLERANCE)) {
            ++stable;
            if (stable >= 3U) {
                if (result) {
                    result->native_readback_confirmed = 1;
                    result->machine_position_unchanged = 1;
                    result->after_generation = after_wcs.wcs_offsets_epoch;
                }
                result_code(result, "WORK_ZERO_PERSISTED_NATIVE_READBACK_CONFIRMED");
                return V5_LINUXCNCRSH_SEND_SENT;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_WORK_ZERO_WAIT_US);
    }
    result_code(result, "WORK_ZERO_NATIVE_READBACK_NOT_CONFIRMED");
    return V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}
