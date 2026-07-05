#ifndef V5_PROGRAM_RUNTIME_H
#define V5_PROGRAM_RUNTIME_H

#include "v5_command_gate.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5ProgramRuntime {
    unsigned int generation;
    int loaded;
    char *gcode_text;
    size_t gcode_size;
    unsigned int line_count;
    char display_name[128];
} V5ProgramRuntime;

typedef struct V5ProgramOpenResult {
    int ok;
    unsigned int generation;
    size_t byte_count;
    unsigned int line_count;
    const char *display_name;
} V5ProgramOpenResult;

void v5_program_runtime_init(V5ProgramRuntime *runtime);
void v5_program_runtime_destroy(V5ProgramRuntime *runtime);
int v5_program_runtime_open_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramOpenResult *result);
int v5_program_runtime_has_open_program(const V5ProgramRuntime *runtime);
int v5_program_runtime_prepare_start(
    const V5ProgramRuntime *runtime,
    V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
