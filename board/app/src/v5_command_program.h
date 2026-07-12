#ifndef V5_COMMAND_PROGRAM_H
#define V5_COMMAND_PROGRAM_H

#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5ProgramController {
    V5ProgramRuntime runtime;
    V5ProgramOpenResult last_open;
} V5ProgramController;

void v5_program_controller_init(V5ProgramController *controller);
void v5_program_controller_destroy(V5ProgramController *controller);
int v5_command_program_open(
    V5ProgramController *controller,
    const char *path,
    V5ProgramOpenResult *result);
int v5_command_program_delete(
    V5ProgramController *controller,
    const char *path,
    V5ProgramDeleteResult *result);
const V5ProgramRuntime *v5_program_controller_runtime(
    const V5ProgramController *controller);

#ifdef __cplusplus
}
#endif

#endif
