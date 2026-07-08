#include "v5_native_rotary_equiv_zero.h"

#include <stdio.h>
#include <string.h>

static int request_is_rotary_equiv_zero(const V5CommandPrepared *prepared, const V5CommandRequest *request)
{
    unsigned int allowed = V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK;
    return prepared && request && prepared->accepted &&
           request->kind == V5_COMMAND_ROTARY_EQUIV_ZERO &&
           prepared->name && strcmp(prepared->name, "rotary_equiv_zero") == 0 &&
           prepared->owner && strcmp(prepared->owner, "native_rotary_gate") == 0 &&
           request->enabled_value == 1 &&
           request->axis_mask != 0U &&
           (request->axis_mask & ~allowed) == 0U;
}

static int append_axis_zero(unsigned int mask, char *out, size_t out_size, size_t *used)
{
    int rc;
    if (!out || !used || *used >= out_size) {
        return 0;
    }
    if ((mask & V5_COMMAND_AXIS_A_MASK) != 0U) {
        rc = snprintf(out + *used, out_size - *used, " A0.000000");
        if (rc < 0 || (size_t)rc >= out_size - *used) {
            return 0;
        }
        *used += (size_t)rc;
    }
    if ((mask & V5_COMMAND_AXIS_C_MASK) != 0U) {
        rc = snprintf(out + *used, out_size - *used, " C0.000000");
        if (rc < 0 || (size_t)rc >= out_size - *used) {
            return 0;
        }
        *used += (size_t)rc;
    }
    return 1;
}

static int build_mdi_line(const V5CommandRequest *request, char *out, size_t out_size)
{
    size_t used;
    int rc;
    if (!request || !out || out_size == 0U) {
        return 0;
    }
    rc = snprintf(out, out_size, "Set MDI G53 G0");
    if (rc < 0 || (size_t)rc >= out_size) {
        return 0;
    }
    used = (size_t)rc;
    return append_axis_zero(request->axis_mask, out, out_size, &used);
}

int v5_native_rotary_equiv_zero_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    size_t used;
    int rc;
    if (!out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!request_is_rotary_equiv_zero(prepared, request)) {
        return 0;
    }
    rc = snprintf(out, out_size, "native_rotary_equiv_zero safe_zero_return");
    if (rc < 0 || (size_t)rc >= out_size) {
        return 0;
    }
    used = (size_t)rc;
    return append_axis_zero(request->axis_mask, out, out_size, &used);
}

V5LinuxcncrshSendStatus v5_native_rotary_equiv_zero_send(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request)
{
    char line[96];
    V5LinuxcncrshSendStatus status;
    if (!request_is_rotary_equiv_zero(prepared, request) || !build_mdi_line(request, line, sizeof(line))) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Mode MDI");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    return v5_linuxcncrsh_send_line(config, line);
}
