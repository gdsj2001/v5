#include "v5_command_safety.h"

#include <string.h>

static int v5_command_safety_prepare(
    V5CommandKind kind,
    V5CommandPrepared *prepared,
    V5CommandRequest *request)
{
    V5CommandRequest local_request;

    if (!prepared) {
        return 0;
    }
    memset(&local_request, 0, sizeof(local_request));
    local_request.kind = kind;
    if (request) {
        *request = local_request;
    }
    return v5_command_gate_prepare(&local_request, prepared);
}

int v5_command_estop_force_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_safety_prepare(V5_COMMAND_ESTOP_FORCE, prepared, request);
}

int v5_command_estop_reset_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_safety_prepare(V5_COMMAND_ESTOP_RESET, prepared, request);
}
