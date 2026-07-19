#include "v5_command_program.h"

#include <malloc.h>
#include <string.h>

void v5_program_controller_init(V5ProgramController *controller)
{
    if (!controller) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    v5_program_runtime_init(&controller->runtime);
}

void v5_program_controller_destroy(V5ProgramController *controller)
{
    if (!controller) {
        return;
    }
    v5_program_runtime_destroy(&controller->runtime);
#ifndef _WIN32
    malloc_trim(0);
#endif
    memset(&controller->last_open, 0, sizeof(controller->last_open));
}

int v5_command_program_open(
    V5ProgramController *controller,
    const char *path,
    V5ProgramOpenResult *result)
{
    int ok;

    if (!controller) {
        if (result) {
            memset(result, 0, sizeof(*result));
        }
        return 0;
    }

    ok = v5_program_runtime_open_file(&controller->runtime, path, &controller->last_open);
    if (result) {
        *result = controller->last_open;
    }
    return ok;
}

int v5_command_program_delete(
    V5ProgramController *controller,
    const char *path,
    V5ProgramDeleteResult *result)
{
    int ok;
    if (!controller) {
        if (result) {
            memset(result, 0, sizeof(*result));
            result->code = "PROGRAM_DELETE_OWNER_UNAVAILABLE";
        }
        return 0;
    }
    ok = v5_program_runtime_delete_file(&controller->runtime, path, result);
    if (ok && result && result->cleared_loaded_program) {
        memset(&controller->last_open, 0, sizeof(controller->last_open));
    }
    return ok;
}

const V5ProgramRuntime *v5_program_controller_runtime(
    const V5ProgramController *controller)
{
    return controller ? &controller->runtime : 0;
}
