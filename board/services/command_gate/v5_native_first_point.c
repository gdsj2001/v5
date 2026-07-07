#include "v5_native_first_point.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int request_is_first_point(const V5CommandPrepared *prepared, const V5CommandRequest *request)
{
    return prepared && request && prepared->accepted && request->kind == V5_COMMAND_FIRST_POINT &&
           prepared->name && strcmp(prepared->name, "first_point") == 0 &&
           prepared->owner && strcmp(prepared->owner, "native_first_point") == 0 &&
           request->mode_value && strcmp(request->mode_value, "AC_XY_Z") == 0 &&
           request->text_value && request->text_value[0] &&
           request->secondary_text_value && strlen(request->secondary_text_value) == 64U &&
           request->index_value > 0 && request->axis_mask;
}

static unsigned int axis_mask_for_index(unsigned int axis_i)
{
    static const unsigned int masks[V5_COMMAND_AXIS_COUNT] = {
        V5_COMMAND_AXIS_X_MASK,
        V5_COMMAND_AXIS_Y_MASK,
        V5_COMMAND_AXIS_Z_MASK,
        V5_COMMAND_AXIS_A_MASK,
        V5_COMMAND_AXIS_C_MASK,
    };
    return axis_i < V5_COMMAND_AXIS_COUNT ? masks[axis_i] : 0U;
}

static char axis_name_for_index(unsigned int axis_i)
{
    static const char names[V5_COMMAND_AXIS_COUNT] = {'X', 'Y', 'Z', 'A', 'C'};
    return axis_i < V5_COMMAND_AXIS_COUNT ? names[axis_i] : '?';
}

static int append_format(char *out, size_t out_size, size_t *used, const char *fmt, ...)
{
    int rc;
    va_list ap;
    if (!out || !used || !fmt || *used >= out_size) {
        return 0;
    }
    va_start(ap, fmt);
    rc = vsnprintf(out + *used, out_size - *used, fmt, ap);
    va_end(ap);
    if (rc < 0 || (size_t)rc >= out_size - *used) {
        return 0;
    }
    *used += (size_t)rc;
    return 1;
}

static int append_axes(char *out, size_t out_size, size_t *used, const V5CommandRequest *request, unsigned int mask)
{
    unsigned int order[V5_COMMAND_AXIS_COUNT] = {3U, 4U, 0U, 1U, 2U};
    unsigned int i;
    int any = 0;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        unsigned int axis_i = order[i];
        if ((mask & axis_mask_for_index(axis_i)) == 0U) {
            continue;
        }
        if (!isfinite(request->point_axis[axis_i])) {
            return 0;
        }
        if (!append_format(out, out_size, used, " %c%.6f", axis_name_for_index(axis_i), request->point_axis[axis_i])) {
            return 0;
        }
        any = 1;
    }
    return any;
}

int v5_native_first_point_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    size_t used = 0U;
    if (!out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!request_is_first_point(prepared, request)) {
        return 0;
    }
    if (!append_format(out, out_size, &used, "native_first_point %s epoch=%d", request->mode_value, request->index_value)) {
        return 0;
    }
    if ((request->axis_mask & (V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK)) != 0U) {
        if (!append_format(out, out_size, &used, " | AC:") ||
            !append_axes(out, out_size, &used, request, request->axis_mask & (V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK))) {
            return 0;
        }
    }
    if ((request->axis_mask & (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK)) != 0U) {
        if (!append_format(out, out_size, &used, " | XY:") ||
            !append_axes(out, out_size, &used, request, request->axis_mask & (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK))) {
            return 0;
        }
    }
    if ((request->axis_mask & V5_COMMAND_AXIS_Z_MASK) != 0U) {
        if (!append_format(out, out_size, &used, " | Z:") ||
            !append_axes(out, out_size, &used, request, request->axis_mask & V5_COMMAND_AXIS_Z_MASK)) {
            return 0;
        }
    }
    return used > 0U;
}

static int build_stage_line(const V5CommandRequest *request, unsigned int mask, char *out, size_t out_size)
{
    size_t used = 0U;
    if (!out || out_size == 0U || !append_format(out, out_size, &used, "Set MDI G0")) {
        return 0;
    }
    if (!append_axes(out, out_size, &used, request, mask)) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static V5LinuxcncrshSendStatus send_stage(
    const V5LinuxcncrshConfig *config,
    const V5CommandRequest *request,
    unsigned int mask,
    int *sent_any)
{
    char line[128];
    V5LinuxcncrshSendStatus status;
    if (!build_stage_line(request, mask, line, sizeof(line))) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    status = v5_linuxcncrsh_send_line(config, line);
    if (status == V5_LINUXCNCRSH_SEND_SENT && sent_any) {
        *sent_any = 1;
    }
    return status;
}

V5LinuxcncrshSendStatus v5_native_first_point_send(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request)
{
    V5LinuxcncrshSendStatus status;
    int sent_any = 0;
    if (!request_is_first_point(prepared, request)) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Mode MDI");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    if ((request->axis_mask & (V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK)) != 0U) {
        status = send_stage(config, request, request->axis_mask & (V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK), &sent_any);
        if (status != V5_LINUXCNCRSH_SEND_SENT) {
            return status;
        }
    }
    if ((request->axis_mask & (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK)) != 0U) {
        status = send_stage(config, request, request->axis_mask & (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK), &sent_any);
        if (status != V5_LINUXCNCRSH_SEND_SENT) {
            return status;
        }
    }
    if ((request->axis_mask & V5_COMMAND_AXIS_Z_MASK) != 0U) {
        status = send_stage(config, request, request->axis_mask & V5_COMMAND_AXIS_Z_MASK, &sent_any);
        if (status != V5_LINUXCNCRSH_SEND_SENT) {
            return status;
        }
    }
    return sent_any ? V5_LINUXCNCRSH_SEND_SENT : V5_LINUXCNCRSH_SEND_INVALID;
}
