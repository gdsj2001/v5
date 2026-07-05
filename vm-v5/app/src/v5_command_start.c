#include "v5_command_start.h"

#include <string.h>

int v5_command_start_prepare(
    const V5ProgramRuntime *runtime,
    V5CommandPrepared *prepared)
{
    V5CommandRequest request;

    if (!prepared) {
        return 0;
    }
    memset(prepared, 0, sizeof(*prepared));

    if (!v5_program_runtime_prepare_start(runtime, &request)) {
        return 0;
    }
    return v5_command_gate_prepare(&request, prepared);
}
