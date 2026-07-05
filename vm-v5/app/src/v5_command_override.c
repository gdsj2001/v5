#include "v5_command_override.h"

#include <string.h>

static int v5_command_override_prepare(
    V5CommandKind kind,
    int percent,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared || percent < 0 || percent > 200) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = kind;
    local_request.index_value = percent;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_feed_override_prepare(int percent, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_override_prepare(V5_COMMAND_FEED_OVERRIDE_SET, percent, prepared, request);
}

int v5_command_spindle_override_prepare(int percent, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_override_prepare(V5_COMMAND_SPINDLE_OVERRIDE_SET, percent, prepared, request);
}
