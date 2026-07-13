#include "v5_linuxcncrsh_client.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int v5_linuxcncrsh_format_ok(int rc, size_t out_size)
{
    return rc > 0 && (size_t)rc < out_size;
}

static int v5_linuxcncrsh_program_from_response(
    const char *response,
    char *program,
    size_t program_size)
{
    const char *cursor;
    if (!response || !program || program_size == 0U) {
        return 0;
    }
    program[0] = '\0';
    cursor = response;
    while (*cursor) {
        const char *start;
        const char *end;
        const char *value;
        size_t value_len;
        while (*cursor == '\r' || *cursor == '\n') {
            ++cursor;
        }
        start = cursor;
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
        end = cursor;
        while (start < end && isspace((unsigned char)*start)) {
            ++start;
        }
        while (end > start && isspace((unsigned char)end[-1])) {
            --end;
        }
        if ((size_t)(end - start) <= 8U || memcmp(start, "PROGRAM ", 8U) != 0) {
            continue;
        }
        value = start + 8U;
        while (value < end && isspace((unsigned char)*value)) {
            ++value;
        }
        value_len = (size_t)(end - value);
        if (value_len == 0U || value_len >= program_size) {
            return 0;
        }
        memcpy(program, value, value_len);
        program[value_len] = '\0';
        return 1;
    }
    return 0;
}

int v5_linuxcncrsh_format_start_transaction(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    const char *program_response,
    char *out,
    size_t out_size)
{
    char current_program[384];
    int rc;
    if (!prepared || !request || !out || out_size == 0U ||
        !prepared->accepted || prepared->kind != V5_COMMAND_START ||
        request->kind != V5_COMMAND_START ||
        !request->text_value || !request->text_value[0] ||
        !v5_linuxcncrsh_program_from_response(
            program_response, current_program, sizeof(current_program))) {
        return 0;
    }
    if (strcmp(current_program, request->text_value) == 0) {
        rc = snprintf(out, out_size, "Set Mode Auto\nSet Run 0");
    } else {
        rc = snprintf(
            out,
            out_size,
            "Set Open %s\nSet Mode Auto\nSet Run 0",
            request->text_value);
    }
    return v5_linuxcncrsh_format_ok(rc, out_size);
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
        rc = snprintf(out, out_size, "Set Open %s\nSet Mode Auto\nSet Run 0", request->text_value);
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
        if (!request->text_value || !request->text_value[0] || request->axis_value == 0.0) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Jog %s %.6f", request->text_value, request->axis_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_JOG_STOP:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Jog_Stop %s", request->text_value);
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
    case V5_COMMAND_AXIS_ZERO_POSITION:
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
