#include "v5_command_motion.h"

#include <string.h>

static int v5_command_motion_prepare(
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

int v5_command_pause_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_motion_prepare(V5_COMMAND_PAUSE, prepared, request);
}

int v5_command_resume_prepare(V5CommandPrepared *prepared, V5CommandRequest *request)
{
    return v5_command_motion_prepare(V5_COMMAND_RESUME, prepared, request);
}
