#include "v5_native_work_zero.h"

#include "v5_native_modal_tool_status.h"
#include "v5_native_readback.h"
#include "v5_native_rtcp_status.h"
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

static int modal_has_token(const char *text, const char *token)
{
    size_t token_len;
    const char *cursor;
    if (!text || !token || !token[0]) {
        return 0;
    }
    token_len = strlen(token);
    for (cursor = text; *cursor;) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
        if (strncmp(cursor, token, token_len) == 0 &&
            (cursor[token_len] == '\0' || cursor[token_len] == ' ' || cursor[token_len] == '\t' ||
             cursor[token_len] == '\r' || cursor[token_len] == '\n')) {
            return 1;
        }
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
    }
    return 0;
}

static void write_code(char *code, size_t code_cap, const char *value)
{
    if (code && code_cap > 0U) {
        snprintf(code, code_cap, "%s", value);
    }
}

int v5_native_work_zero_preflight(
    const V5NativeReadback *modal_tool,
    const V5NativeReadback *rtcp,
    char *code,
    size_t code_cap)
{
    if (!modal_tool || !v5_native_readback_interpreter_idle_known(modal_tool) ||
        !v5_native_readback_interpreter_known(modal_tool) ||
        !v5_native_readback_mdi_run_known(modal_tool)) {
        write_code(code, code_cap, "WORK_ZERO_INTERPRETER_STATUS_UNAVAILABLE");
        return 0;
    }
    if (!modal_tool->interpreter_idle || modal_tool->interpreter_paused || modal_tool->mdi_run_active) {
        write_code(code, code_cap, "WORK_ZERO_MACHINE_NOT_IDLE");
        return 0;
    }
    if (!v5_native_readback_modal_known(modal_tool) || !v5_native_readback_tool_known(modal_tool)) {
        write_code(code, code_cap, "WORK_ZERO_MODAL_TOOL_STATUS_UNAVAILABLE");
        return 0;
    }
    if (!modal_has_token(modal_tool->modal_text, "G40") ||
        !modal_has_token(modal_tool->modal_text, "G49")) {
        write_code(code, code_cap, "WORK_ZERO_TOOL_COMPENSATION_ACTIVE_OR_UNKNOWN");
        return 0;
    }
    if (!rtcp || !v5_native_readback_rtcp_known(rtcp)) {
        write_code(code, code_cap, "WORK_ZERO_RTCP_STATUS_UNAVAILABLE");
        return 0;
    }
    if (rtcp->rtcp_enabled) {
        write_code(code, code_cap, "WORK_ZERO_RTCP_ACTIVE");
        return 0;
    }
    write_code(code, code_cap, "WORK_ZERO_PREFLIGHT_OK");
    return 1;
}

int v5_native_work_zero_coordinate_frame_clear(
    double machine_position,
    double relative_position,
    double active_wcs_offset)
{
    return isfinite(machine_position) && isfinite(relative_position) && isfinite(active_wcs_offset) &&
           fabs(relative_position - (machine_position - active_wcs_offset)) <=
               V5_WORK_ZERO_POSITION_TOLERANCE;
}

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

#ifndef _WIN32
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

static int machine_axes_still(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters)
{
    double before[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    double after[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    unsigned int i;
    if (!config || !parameters || !parameters->loaded) {
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (parameters->axes[i].active &&
            !v5_linuxcncrsh_get_axis_position(config, parameters->axes[i].axis, 0, &before[i])) {
            return 0;
        }
    }
    usleep(V5_WORK_ZERO_WAIT_US);
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (parameters->axes[i].active &&
            (!v5_linuxcncrsh_get_axis_position(config, parameters->axes[i].axis, 0, &after[i]) ||
             fabs(after[i] - before[i]) > V5_WORK_ZERO_POSITION_TOLERANCE)) {
            return 0;
        }
    }
    return 1;
}

static int restore_manual_mode(const V5LinuxcncrshConfig *config, V5NativeWorkZeroResult *result)
{
    if (v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "WORK_ZERO_MODE_RESTORE_FAILED");
        return 0;
    }
    if (result) {
        result->mode_restored = 1;
    }
    return 1;
}
#endif

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
    V5NativeReadback modal_tool;
    V5NativeReadback rtcp;
    char axis;
    char line[96];
    double before_abs;
    double before_rel;
    unsigned int attempt;
    unsigned int stable = 0U;
    int rc;
    int machine_enabled;
    int mdi_entered = 0;
    char preflight_code[64] = {0};
    const double *before_offsets;

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
    v5_native_readback_init(&modal_tool);
    v5_native_readback_init(&rtcp);
    if (!v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool) ||
        !v5_native_rtcp_status_read(0, V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS, &rtcp) ||
        !v5_native_work_zero_preflight(&modal_tool, &rtcp, preflight_code, sizeof(preflight_code))) {
        result_code(result, preflight_code[0] ? preflight_code : "WORK_ZERO_NATIVE_STATUS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_probe_machine_enabled(config, &machine_enabled, 0, 0) || !machine_enabled) {
        result_code(result, "WORK_ZERO_MACHINE_NOT_ENABLED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!machine_axes_still(config, parameters)) {
        result_code(result, "WORK_ZERO_MACHINE_NOT_STILL");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_get_axis_position(config, axis, 0, &before_abs) ||
        !v5_linuxcncrsh_get_axis_position(config, axis, 1, &before_rel)) {
        result_code(result, "WORK_ZERO_START_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    before_offsets = v5_native_readback_active_wcs_offsets(&before_wcs);
    if (!before_offsets ||
        !v5_native_work_zero_coordinate_frame_clear(
            before_abs,
            before_rel,
            before_offsets[axis_parameters->status_slot])) {
        result_code(result, "WORK_ZERO_G92_OR_HIDDEN_OFFSET_ACTIVE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (result) {
        result->before_generation = before_wcs.wcs_offsets_epoch;
    }
    rc = snprintf(line, sizeof(line), "Set MDI G10 L20 P%d %c0", request->index_value, axis);
    if (rc <= 0 || (size_t)rc >= sizeof(line)) {
        result_code(result, "WORK_ZERO_REQUEST_INVALID");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode MDI") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "WORK_ZERO_OWNER_SUBMIT_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    mdi_entered = 1;
    if (v5_linuxcncrsh_send_line(config, line) != V5_LINUXCNCRSH_SEND_SENT) {
        if (!restore_manual_mode(config, result)) {
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        result_code(result, "WORK_ZERO_OWNER_SUBMIT_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (result) {
        result->persistent_owner_submitted = 1;
    }
    for (attempt = 0U; attempt < V5_WORK_ZERO_WAIT_ATTEMPTS; ++attempt) {
        double after_abs;
        double after_rel;
        const double *after_offsets;
        int selected_offset_confirmed = 0;
        if (v5_linuxcncrsh_get_axis_position(config, axis, 0, &after_abs) &&
            v5_linuxcncrsh_get_axis_position(config, axis, 1, &after_rel) &&
            read_wcs(&after_wcs) && after_wcs.wcs_index == before_wcs.wcs_index) {
            after_offsets = v5_native_readback_active_wcs_offsets(&after_wcs);
            selected_offset_confirmed = before_offsets && after_offsets &&
                v5_native_work_zero_coordinate_frame_clear(
                    after_abs,
                    after_rel,
                    after_offsets[axis_parameters->status_slot]);
        }
        if (selected_offset_confirmed &&
            fabs(after_abs - before_abs) <= V5_WORK_ZERO_POSITION_TOLERANCE &&
            fabs(after_rel) <= V5_WORK_ZERO_POSITION_TOLERANCE &&
            after_wcs.wcs_offsets_epoch != before_wcs.wcs_offsets_epoch) {
            ++stable;
            if (stable >= 3U) {
                if (result) {
                    result->native_readback_confirmed = 1;
                    result->machine_position_unchanged = 1;
                    result->after_generation = after_wcs.wcs_offsets_epoch;
                }
                if (!restore_manual_mode(config, result)) {
                    return V5_LINUXCNCRSH_SEND_IO_ERROR;
                }
                result_code(result, "WORK_ZERO_PERSISTED_NATIVE_READBACK_CONFIRMED");
                return V5_LINUXCNCRSH_SEND_SENT;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_WORK_ZERO_WAIT_US);
    }
    if (mdi_entered && !restore_manual_mode(config, result)) {
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    result_code(result, "WORK_ZERO_NATIVE_READBACK_NOT_CONFIRMED");
    return V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}
