#ifndef V5_COMMAND_START_H
#define V5_COMMAND_START_H

#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_start_prepare(
    const V5ProgramRuntime *runtime,
    V5CommandPrepared *prepared);

#ifdef __cplusplus
}
#endif

#endif
