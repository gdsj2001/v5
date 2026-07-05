#include "v5_command_wcs.h"

#include <ctype.h>
#include <string.h>

static int v5_command_axis_allowed(char axis)
{
    char up = (char)toupper((unsigned char)axis);
    return up == 'X' || up == 'Y' || up == 'Z' || up == 'A' || up == 'B' || up == 'C' ||
           up == 'U' || up == 'V' || up == 'W';
}

int v5_command_wcs_select_prepare(int wcs_index, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared || wcs_index < 0 || wcs_index > 8) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_WCS_SELECT;
    local_request.index_value = wcs_index;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_work_zero_prepare(int wcs_index, char axis, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    static char axis_text[2];
    V5CommandRequest local_request;

    if (!prepared || wcs_index < 0 || wcs_index > 8 || !v5_command_axis_allowed(axis)) {
        return 0;
    }
    axis_text[0] = (char)toupper((unsigned char)axis);
    axis_text[1] = '\0';

    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_WORK_ZERO;
    local_request.index_value = wcs_index + 1;
    local_request.text_value = axis_text;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_g92_clear_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_G92_CLEAR;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}
