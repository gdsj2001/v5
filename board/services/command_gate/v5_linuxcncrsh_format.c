#include "v5_linuxcncrsh_client.h"

#include <stdio.h>

static int v5_linuxcncrsh_format_ok(int rc, size_t out_size)
{
    return rc > 0 && (size_t)rc < out_size;
}

int v5_linuxcncrsh_format_line(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    int rc;
    static const char *wcs_codes[] = {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};

    if (!prepared || !request || !out || out_size == 0U || !prepared->accepted) {
        return 0;
    }

    switch (request->kind) {
    case V5_COMMAND_PROGRAM_OPEN:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Open %s", request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_START:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Open %s\nSet Mode Auto\nSet Run 0\nSet Resume", request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_MDI_RUN:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set MDI %s", request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_RESUME:
        rc = snprintf(out, out_size, "Set Resume");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_HOME:
        return 0;
    case V5_COMMAND_JOG_INCREMENT:
        if (!request->text_value || !request->text_value[0] || request->increment_value <= 0.0) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Jog_Incr %s %.3f %.3f", request->text_value, request->axis_value, request->increment_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_JOG_CONTINUOUS:
        if (request->index_value < 0 || request->index_value >= (int)V5_COMMAND_AXIS_COUNT || request->axis_value == 0.0) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Jog %d %.6f", request->index_value, request->axis_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_JOG_STOP:
        if (request->index_value < 0 || request->index_value >= (int)V5_COMMAND_AXIS_COUNT) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Jog %d 0", request->index_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_PAUSE:
        rc = snprintf(out, out_size, "Set Pause");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_ESTOP_FORCE:
    case V5_COMMAND_ESTOP_RESET:
        return 0;
    case V5_COMMAND_WCS_SELECT:
        if (request->index_value < 0 || request->index_value > 8) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set MDI %s", wcs_codes[request->index_value]);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_WORK_ZERO:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        if (request->index_value < 1 || request->index_value > 9) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set MDI G10 L20 P%d %s0", request->index_value, request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_G92_CLEAR:
        rc = snprintf(out, out_size, "Set MDI G92.1");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_RTCP_SET:
        return 0;
    case V5_COMMAND_FEED_OVERRIDE_SET:
        if (request->index_value < 0 || request->index_value > 200) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Feed_Override %d", request->index_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_SPINDLE_OVERRIDE_SET:
        if (request->index_value < 0 || request->index_value > 200) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Spindle_Override %d", request->index_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    default:
        return 0;
    }
}
