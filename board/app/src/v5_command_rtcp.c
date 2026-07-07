#include "v5_command_rtcp.h"

#include <string.h>

int v5_command_rtcp_prepare(int enabled, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = V5_COMMAND_RTCP_SET;
    local_request.enabled_value = enabled ? 1 : 0;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_rtcp_toggle_prepare(const V5NativeReadback *readback, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    int enabled = 0;

    if (!v5_command_rtcp_actual_known(readback, &enabled)) {
        if (prepared) {
            memset(prepared, 0, sizeof(*prepared));
        }
        if (request) {
            memset(request, 0, sizeof(*request));
        }
        return 0;
    }
    return v5_command_rtcp_prepare(!enabled, prepared, request);
}

int v5_command_rtcp_actual_known(const V5NativeReadback *readback, int *enabled_out)
{
    if (!v5_native_readback_rtcp_known(readback)) {
        return 0;
    }
    if (enabled_out) {
        *enabled_out = readback->rtcp_enabled;
    }
    return 1;
}
