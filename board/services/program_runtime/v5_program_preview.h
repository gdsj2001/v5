#ifndef V5_PROGRAM_PREVIEW_H
#define V5_PROGRAM_PREVIEW_H

#include "v5_program_runtime.h"

#include <stddef.h>

unsigned int v5_program_preview_count_lines(const char *text, size_t size);
int v5_program_preview_find_first_point(
    const char *text,
    size_t size,
    double axis_out[V5_COMMAND_AXIS_COUNT],
    unsigned int *axis_mask_out);
unsigned int v5_program_preview_build(
    V5ProgramRuntime *runtime,
    const char *text,
    size_t size,
    V5StatusPoint *points);

#endif
