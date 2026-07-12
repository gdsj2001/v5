#ifndef V5_PROGRAM_RUNTIME_H
#define V5_PROGRAM_RUNTIME_H

#include "v5_command_gate.h"
#include "v5_status_shm.h"

#include <stddef.h>

#define V5_PROGRAM_PREVIEW_POINT_COUNT 512u
#define V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES (2u * 1024u * 1024u)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5ProgramRuntimeMode {
    V5_PROGRAM_RUNTIME_NONE = 0,
    V5_PROGRAM_RUNTIME_PROGRAM = 1,
    V5_PROGRAM_RUNTIME_MDI = 2
} V5ProgramRuntimeMode;

typedef struct V5ProgramRuntime {
    unsigned int generation;
    int loaded;
    V5ProgramRuntimeMode mode;
    char *gcode_text;
    size_t gcode_size;
    size_t gcode_map_size;
    int gcode_text_mmap;
    unsigned int line_count;
    V5StatusPoint preview_trajectory[V5_PROGRAM_PREVIEW_POINT_COUNT];
    int preview_wcs_indices[V5_PROGRAM_PREVIEW_POINT_COUNT];
    unsigned char preview_break_before[V5_PROGRAM_PREVIEW_POINT_COUNT];
    unsigned int preview_trajectory_count;
    unsigned int preview_wcs_mask;
    int preview_program_wcs_index;
    int preview_wcs_mixed;
    unsigned int preview_candidate_count;
    unsigned int preview_kept_count;
    unsigned int preview_segment_count;
    int preview_decimated;
    int preview_truncated;
    char preview_strategy[64];
    int first_point_valid;
    unsigned int first_point_axis_mask;
    double first_point_axis[V5_COMMAND_AXIS_COUNT];
    char display_name[128];
    char source_path[384];
    char source_sha256[65];
    unsigned int loaded_epoch;
    char mdi_text[128];
} V5ProgramRuntime;

typedef struct V5ProgramOpenResult {
    int ok;
    const char *code;
    unsigned int generation;
    size_t byte_count;
    size_t max_gcode_bytes;
    unsigned int line_count;
    unsigned int loaded_epoch;
    unsigned int preview_point_capacity;
    const char *display_name;
    const char *source_path;
    const char *source_sha256;
    unsigned int preview_candidate_count;
    unsigned int preview_kept_count;
    unsigned int preview_segment_count;
    int preview_decimated;
    int preview_truncated;
    const char *preview_strategy;
} V5ProgramOpenResult;

typedef struct V5ProgramDeleteResult {
    int ok;
    const char *code;
    int removed;
    int cleared_loaded_program;
    unsigned int generation;
} V5ProgramDeleteResult;

void v5_program_runtime_init(V5ProgramRuntime *runtime);
void v5_program_runtime_destroy(V5ProgramRuntime *runtime);
int v5_program_runtime_open_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramOpenResult *result);
int v5_program_runtime_delete_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramDeleteResult *result);
int v5_program_runtime_has_open_program(const V5ProgramRuntime *runtime);
int v5_program_runtime_has_mdi(const V5ProgramRuntime *runtime);
const char *v5_program_runtime_mdi_text(const V5ProgramRuntime *runtime);
const char *v5_program_runtime_source_path(const V5ProgramRuntime *runtime);
const char *v5_program_runtime_source_sha256(const V5ProgramRuntime *runtime);
unsigned int v5_program_runtime_loaded_epoch(const V5ProgramRuntime *runtime);
int v5_program_runtime_has_first_point_metadata(const V5ProgramRuntime *runtime);
int v5_program_runtime_first_point_axes(
    const V5ProgramRuntime *runtime,
    double axis_out[V5_COMMAND_AXIS_COUNT],
    unsigned int *axis_mask_out);
int v5_program_runtime_set_mdi_line(V5ProgramRuntime *runtime, const char *line);
unsigned int v5_program_runtime_preview_trajectory(
    const V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int capacity);
int v5_program_runtime_preview_wcs_index(
    const V5ProgramRuntime *runtime,
    unsigned int point_index,
    int *wcs_index_out);
int v5_program_runtime_preview_break_before(
    const V5ProgramRuntime *runtime,
    unsigned int point_index);
int v5_program_runtime_preview_program_wcs_index(const V5ProgramRuntime *runtime);
unsigned int v5_program_runtime_preview_wcs_mask(const V5ProgramRuntime *runtime);
int v5_program_runtime_prepare_start(
    const V5ProgramRuntime *runtime,
    V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
