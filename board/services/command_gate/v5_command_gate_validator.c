#include "v5_command_gate_validator.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define V5_COMMAND_GATE_AXIS_MASK_ALL \
    (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK | V5_COMMAND_AXIS_Z_MASK | V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK)
#define V5_COMMAND_GATE_MAX_ABS_AXIS_VALUE 100000000.0
#define V5_COMMAND_GATE_MAX_INCREMENT_VALUE 100000000.0

static void set_reason(char *reason, size_t reason_cap, const char *text)
{
    if (reason && reason_cap > 0U) {
        snprintf(reason, reason_cap, "%s", text ? text : "INVALID");
    }
}

static int text_has_nul(const char *text, size_t cap)
{
    return text && memchr(text, '\0', cap) != 0;
}

static int text_is_empty(const char *text)
{
    return !text || text[0] == '\0';
}

static int starts_with_set_command(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p && isspace(*p)) {
        ++p;
    }
    return (tolower(p[0]) == 's' &&
            tolower(p[1]) == 'e' &&
            tolower(p[2]) == 't' &&
            (p[3] == ' ' || p[3] == '\t'));
}

static int text_is_single_line_printable(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!text) {
        return 1;
    }
    while (*p) {
        if (*p < 32U || *p == 127U || *p == '|') {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int command_text_ok(const char *text, int required, int allowed, char *reason, size_t reason_cap)
{
    if (!allowed && !text_is_empty(text)) {
        set_reason(reason, reason_cap, "UNEXPECTED_TEXT");
        return 0;
    }
    if (required && text_is_empty(text)) {
        set_reason(reason, reason_cap, "TEXT_REQUIRED");
        return 0;
    }
    if (!text_is_empty(text)) {
        if (!text_is_single_line_printable(text)) {
            set_reason(reason, reason_cap, "TEXT_NOT_SINGLE_LINE");
            return 0;
        }
        if (starts_with_set_command(text)) {
            set_reason(reason, reason_cap, "RAW_SET_TEXT_REJECTED");
            return 0;
        }
    }
    return 1;
}

static int sha256_hex_ok(const char *text)
{
    size_t i;
    if (!text || strlen(text) != 64U) {
        return 0;
    }
    for (i = 0U; i < 64U; ++i) {
        if (!isxdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

static int axis_name_ok(const char *text)
{
    char c;
    if (!text || !text[0] || text[1]) {
        return 0;
    }
    c = (char)toupper((unsigned char)text[0]);
    return c == 'X' || c == 'Y' || c == 'Z' || c == 'A' || c == 'B' || c == 'C';
}

static int drive_window_run_id_ok(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t length = 0U;
    if (!p || !*p) {
        return 0;
    }
    while (*p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':')) {
            return 0;
        }
        ++length;
        if (length > 64U) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int drive_window_numeric_payload_is_empty(const V5CommandGateIpcRequestFrame *frame)
{
    unsigned int i;
    if (!frame || frame->index_value != 0 || frame->axis_mask != 0U ||
        frame->axis_value != 0.0 || frame->increment_value != 0.0) {
        return 0;
    }
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        if (frame->point_axis[i] != 0.0) {
            return 0;
        }
    }
    return 1;
}

static int finite_bounded(double value, double limit)
{
    return isfinite(value) && value >= -limit && value <= limit;
}

static int finite_all_doubles(const V5CommandGateIpcRequestFrame *frame)
{
    unsigned int i;
    if (!isfinite(frame->axis_value) || !isfinite(frame->increment_value)) {
        return 0;
    }
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        if (!isfinite(frame->point_axis[i])) {
            return 0;
        }
    }
    return 1;
}

static int no_stray_axis_mask(const V5CommandGateIpcRequestFrame *frame, char *reason, size_t reason_cap)
{
    if ((frame->axis_mask & ~V5_COMMAND_GATE_AXIS_MASK_ALL) != 0U) {
        set_reason(reason, reason_cap, "AXIS_MASK_INVALID");
        return 0;
    }
    return 1;
}

static int axis_mask_must_be_zero(const V5CommandGateIpcRequestFrame *frame, char *reason, size_t reason_cap)
{
    if (frame->axis_mask != 0U) {
        set_reason(reason, reason_cap, "AXIS_MASK_UNEXPECTED");
        return 0;
    }
    return 1;
}

int v5_command_gate_validate_envelope(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcOp expected_op,
    char *reason,
    size_t reason_cap)
{
    if (!frame || frame->magic != V5_COMMAND_GATE_IPC_MAGIC ||
        frame->version != V5_COMMAND_GATE_IPC_VERSION ||
        frame->size != (uint32_t)sizeof(*frame)) {
        set_reason(reason, reason_cap, "BAD_ENVELOPE");
        return 0;
    }
    if (frame->op != (uint32_t)expected_op) {
        set_reason(reason, reason_cap, "BAD_OPCODE");
        return 0;
    }
    if (!text_has_nul(frame->text_value, sizeof(frame->text_value)) ||
        !text_has_nul(frame->secondary_text_value, sizeof(frame->secondary_text_value)) ||
        !text_has_nul(frame->mode_value, sizeof(frame->mode_value))) {
        set_reason(reason, reason_cap, "TEXT_NOT_NUL_TERMINATED");
        return 0;
    }
    return 1;
}

int v5_command_gate_validate_execute_frame(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandRequest *request,
    char *reason,
    size_t reason_cap)
{
    V5CommandKind kind;
    unsigned int i;
    if (!request) {
        set_reason(reason, reason_cap, "REQUEST_OUT_MISSING");
        return 0;
    }
    memset(request, 0, sizeof(*request));
    if (!v5_command_gate_validate_envelope(frame, V5_COMMAND_GATE_IPC_OP_EXECUTE, reason, reason_cap)) {
        return 0;
    }
    if (!finite_all_doubles(frame)) {
        set_reason(reason, reason_cap, "NONFINITE_DOUBLE");
        return 0;
    }
    if (!no_stray_axis_mask(frame, reason, reason_cap)) {
        return 0;
    }
    if (frame->kind <= (int32_t)V5_COMMAND_UI_LOCAL || frame->kind > (int32_t)V5_COMMAND_DRIVE_WRITE_ABORT) {
        set_reason(reason, reason_cap, "UNKNOWN_COMMAND_KIND");
        return 0;
    }
    kind = (V5CommandKind)frame->kind;
    if (kind != V5_COMMAND_AXIS_ZERO_POSITION && !text_is_empty(frame->mode_value)) {
        set_reason(reason, reason_cap, "MODE_TEXT_UNSUPPORTED");
        return 0;
    }
    switch (kind) {
    case V5_COMMAND_PROGRAM_OPEN:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            return 0;
        }
        break;
    case V5_COMMAND_MDI_RUN:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            return 0;
        }
        break;
    case V5_COMMAND_JOG_INCREMENT:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !axis_name_ok(frame->text_value) ||
            !finite_bounded(frame->axis_value, V5_COMMAND_GATE_MAX_ABS_AXIS_VALUE) ||
            frame->increment_value <= 0.0 ||
            frame->increment_value > V5_COMMAND_GATE_MAX_INCREMENT_VALUE ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "JOG_INCREMENT_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_JOG_CONTINUOUS:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            frame->axis_value == 0.0 ||
            !finite_bounded(frame->axis_value, V5_COMMAND_GATE_MAX_ABS_AXIS_VALUE) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !axis_name_ok(frame->text_value) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "JOG_CONTINUOUS_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_JOG_STOP:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !axis_name_ok(frame->text_value) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "JOG_STOP_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_WCS_SELECT:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            frame->index_value < 0 || frame->index_value > 8 ||
            !command_text_ok(frame->text_value, 0, 0, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "WCS_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_WORK_ZERO:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            frame->index_value < 1 || frame->index_value > 9 ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !axis_name_ok(frame->text_value) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "WORK_ZERO_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_RTCP_SET:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            (frame->enabled_value != 0 && frame->enabled_value != 1) ||
            !command_text_ok(frame->text_value, 0, 0, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "RTCP_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_FEED_OVERRIDE_SET:
    case V5_COMMAND_SPINDLE_OVERRIDE_SET:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            frame->index_value < 0 || frame->index_value > 200 ||
            !command_text_ok(frame->text_value, 0, 0, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "OVERRIDE_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_FIRST_POINT:
        if (frame->index_value <= 0 || frame->axis_mask == 0U ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 1, 1, reason, reason_cap) ||
            !sha256_hex_ok(frame->secondary_text_value)) {
            set_reason(reason, reason_cap, "FIRST_POINT_INVALID");
            return 0;
        }
        for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
            if ((frame->axis_mask & (1U << i)) != 0U &&
                !finite_bounded(frame->point_axis[i], V5_COMMAND_GATE_MAX_ABS_AXIS_VALUE)) {
                set_reason(reason, reason_cap, "FIRST_POINT_AXIS_INVALID");
                return 0;
            }
        }
        break;
    case V5_COMMAND_AXIS_ZERO_POSITION:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !axis_name_ok(frame->text_value) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            set_reason(reason, reason_cap, "AXIS_ZERO_POSITION_INVALID");
            return 0;
        }
        if (strcmp(frame->mode_value, "mcs") != 0 && strcmp(frame->mode_value, "wcs") != 0) {
            set_reason(reason, reason_cap, "AXIS_ZERO_POSITION_MODE_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_DRIVE_WRITE_BEGIN:
    case V5_COMMAND_DRIVE_WRITE_FINISH:
    case V5_COMMAND_DRIVE_WRITE_ABORT:
        if (!drive_window_numeric_payload_is_empty(frame) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !drive_window_run_id_ok(frame->text_value) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap) ||
            !text_is_empty(frame->mode_value) ||
            (kind != V5_COMMAND_DRIVE_WRITE_FINISH && frame->enabled_value != 0) ||
            (kind == V5_COMMAND_DRIVE_WRITE_FINISH &&
             frame->enabled_value != 0 && frame->enabled_value != 1)) {
            set_reason(reason, reason_cap, "DRIVE_WRITE_WINDOW_INVALID");
            return 0;
        }
        break;
    case V5_COMMAND_PAUSE:
    case V5_COMMAND_RESUME:
    case V5_COMMAND_HOME:
    case V5_COMMAND_ESTOP_FORCE:
    case V5_COMMAND_ESTOP_RESET:
    case V5_COMMAND_G92_CLEAR:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 0, 0, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            return 0;
        }
        break;
    case V5_COMMAND_START:
        if (!axis_mask_must_be_zero(frame, reason, reason_cap) ||
            !command_text_ok(frame->text_value, 1, 1, reason, reason_cap) ||
            !command_text_ok(frame->secondary_text_value, 0, 0, reason, reason_cap)) {
            return 0;
        }
        break;
    default:
        set_reason(reason, reason_cap, "UNHANDLED_COMMAND_KIND");
        return 0;
    }

    request->kind = kind;
    request->index_value = (int)frame->index_value;
    request->enabled_value = (int)frame->enabled_value;
    request->axis_mask = frame->axis_mask;
    request->axis_value = frame->axis_value;
    request->increment_value = frame->increment_value;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        request->point_axis[i] = frame->point_axis[i];
    }
    request->text_value = text_is_empty(frame->text_value) ? 0 : frame->text_value;
    request->secondary_text_value = text_is_empty(frame->secondary_text_value) ? 0 : frame->secondary_text_value;
    request->mode_value = text_is_empty(frame->mode_value) ? 0 : frame->mode_value;
    set_reason(reason, reason_cap, "OK");
    return 1;
}
