#ifndef V5_COMMAND_FIRST_POINT_H
#define V5_COMMAND_FIRST_POINT_H

#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_FIRST_POINT_SEQUENCE "AC_XY_Z"

int v5_command_first_point_prepare(const V5ProgramRuntime *runtime, V5CommandPrepared *prepared, V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
