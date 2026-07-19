#include "v5_program_runtime.h"
#include "v5_program_preview.h"
#include "v5_sha256.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static char *v5_program_runtime_alloc_text(size_t size)
{
#ifdef _WIN32
    if (size == 0U || size > V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES + 1U) {
        return 0;
    }
    return (char *)malloc(size);
#else
    void *mapped;
    if (size == 0U || size > V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES + 1U) {
        return 0;
    }
    mapped = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        return 0;
    }
    return (char *)mapped;
#endif
}

static void v5_program_runtime_clear(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->gcode_text) {
#ifdef _WIN32
        free(runtime->gcode_text);
#else
        if (runtime->gcode_text_mmap && runtime->gcode_map_size > 0U) {
            (void)munmap(runtime->gcode_text, runtime->gcode_map_size);
        } else {
            free(runtime->gcode_text);
        }
        (void)malloc_trim(0);
#endif
    }
    runtime->gcode_text = 0;
    runtime->gcode_size = 0U;
    runtime->gcode_map_size = 0U;
    runtime->gcode_text_mmap = 0;
    runtime->line_count = 0U;
    runtime->preview_trajectory_count = 0U;
    memset(runtime->preview_wcs_indices, 0, sizeof(runtime->preview_wcs_indices));
    memset(runtime->preview_break_before, 0, sizeof(runtime->preview_break_before));
    runtime->preview_wcs_mask = 0U;
    runtime->preview_program_wcs_index = 0;
    runtime->preview_wcs_mixed = 0;
    runtime->preview_candidate_count = 0U;
    runtime->preview_kept_count = 0U;
    runtime->preview_segment_count = 0U;
    runtime->preview_decimated = 0;
    runtime->preview_truncated = 0;
    snprintf(runtime->preview_strategy, sizeof(runtime->preview_strategy), "none");
    runtime->first_point_valid = 0;
    runtime->first_point_axis_mask = 0U;
    memset(runtime->first_point_axis, 0, sizeof(runtime->first_point_axis));
    runtime->loaded = 0;
    runtime->mode = V5_PROGRAM_RUNTIME_NONE;
    runtime->display_name[0] = '\0';
    runtime->source_path[0] = '\0';
    runtime->source_sha256[0] = '\0';
    runtime->loaded_epoch = 0U;
    runtime->program_scene_ready = 0;
    runtime->ready_program_generation = 0U;
    runtime->ready_scene_generation = 0ULL;
    runtime->ready_fit_generation = 0U;
    runtime->mdi_text[0] = '\0';
}

static const char *v5_program_runtime_basename(const char *path)
{
    const char *name = path;
    const char *p;

    if (!path) {
        return "";
    }

    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

static void v5_program_runtime_set_result(
    const V5ProgramRuntime *runtime,
    V5ProgramOpenResult *result,
    int ok,
    const char *code)
{
    if (!result) {
        return;
    }

    result->ok = ok;
    result->code = code ? code : (ok ? "OK" : "PROGRAM_OPEN_FAILED");
    result->generation = runtime ? runtime->generation : 0U;
    result->byte_count = runtime ? runtime->gcode_size : 0U;
    result->max_gcode_bytes = V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES;
    result->line_count = runtime ? runtime->line_count : 0U;
    result->loaded_epoch = runtime ? runtime->loaded_epoch : 0U;
    result->preview_point_capacity = V5_PROGRAM_PREVIEW_POINT_COUNT;
    result->display_name = runtime ? runtime->display_name : "";
    result->source_path = runtime ? runtime->source_path : "";
    result->source_sha256 = runtime ? runtime->source_sha256 : "";
    result->preview_candidate_count = runtime ? runtime->preview_candidate_count : 0U;
    result->preview_kept_count = runtime ? runtime->preview_kept_count : 0U;
    result->preview_segment_count = runtime ? runtime->preview_segment_count : 0U;
    result->preview_decimated = runtime ? runtime->preview_decimated : 0;
    result->preview_truncated = runtime ? runtime->preview_truncated : 0;
    result->preview_strategy = runtime ? runtime->preview_strategy : "";
}

void v5_program_runtime_init(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

void v5_program_runtime_destroy(V5ProgramRuntime *runtime)
{
    v5_program_runtime_clear(runtime);
}

int v5_program_runtime_open_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramOpenResult *result)
{
    FILE *fp;
    long length;
    size_t size;
    char *buffer;
    size_t read_count;
    const char *display_name;

    if (!runtime || !path || !path[0]) {
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_PATH_INVALID");
        return 0;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_OPEN_FILE_FAILED");
        return 0;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_SEEK_FAILED");
        return 0;
    }

    length = ftell(fp);
    if (length < 0L || fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_SIZE_READ_FAILED");
        return 0;
    }

    size = (size_t)length;
    if (size == 0U || size > V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES) {
        fclose(fp);
        v5_program_runtime_clear(runtime);
        v5_program_runtime_set_result(runtime, result, 0,
                                      size == 0U ? "PROGRAM_EMPTY" : "PROGRAM_GCODE_SIZE_LIMIT_EXCEEDED");
        return 0;
    }
    v5_program_runtime_clear(runtime);
    buffer = v5_program_runtime_alloc_text(size + 1U);
    if (!buffer) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_ALLOC_FAILED");
        return 0;
    }

    read_count = fread(buffer, 1U, size, fp);
    fclose(fp);
    if (read_count != size) {
#ifdef _WIN32
        free(buffer);
#else
        (void)munmap(buffer, size + 1U);
#endif
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_READ_FAILED");
        return 0;
    }

    buffer[size] = '\0';
    runtime->gcode_text = buffer;
    runtime->gcode_size = size;
#ifdef _WIN32
    runtime->gcode_map_size = 0U;
    runtime->gcode_text_mmap = 0;
#else
    runtime->gcode_map_size = size + 1U;
    runtime->gcode_text_mmap = 1;
#endif
    runtime->line_count = v5_program_preview_count_lines(buffer, size);
    runtime->preview_trajectory_count = v5_program_preview_build(runtime, buffer, size, runtime->preview_trajectory);
    runtime->first_point_valid = v5_program_preview_find_first_point(
        buffer,
        size,
        runtime->first_point_axis,
        &runtime->first_point_axis_mask);
    runtime->loaded = 1;
    runtime->mode = V5_PROGRAM_RUNTIME_PROGRAM;
    runtime->mdi_text[0] = '\0';
    ++runtime->generation;
    runtime->loaded_epoch = runtime->generation;
    snprintf(runtime->source_path, sizeof(runtime->source_path), "%s", path);
    v5_sha256_hex((const unsigned char *)buffer, size, runtime->source_sha256);

    display_name = v5_program_runtime_basename(path);
    snprintf(runtime->display_name, sizeof(runtime->display_name), "%s", display_name);

    v5_program_runtime_set_result(runtime, result, 1, "OK");
    return 1;
}

static void v5_program_runtime_set_delete_result(
    const V5ProgramRuntime *runtime,
    V5ProgramDeleteResult *result,
    int ok,
    const char *code,
    int removed,
    int cleared_loaded_program)
{
    if (!result) {
        return;
    }
    result->ok = ok;
    result->code = code ? code : (ok ? "OK" : "PROGRAM_DELETE_FAILED");
    result->removed = removed;
    result->cleared_loaded_program = cleared_loaded_program;
    result->generation = runtime ? runtime->generation : 0U;
}

int v5_program_runtime_delete_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramDeleteResult *result)
{
    struct stat st;
    int clears_loaded_program;
    if (!runtime || !path || !path[0]) {
        v5_program_runtime_set_delete_result(
            runtime, result, 0, "PROGRAM_DELETE_PATH_INVALID", 0, 0);
        return 0;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        v5_program_runtime_set_delete_result(
            runtime, result, 0, "PROGRAM_DELETE_NOT_REGULAR", 0, 0);
        return 0;
    }
    clears_loaded_program = v5_program_runtime_has_open_program(runtime) &&
        strcmp(runtime->source_path, path) == 0;
    if (remove(path) != 0) {
        v5_program_runtime_set_delete_result(
            runtime, result, 0, "PROGRAM_DELETE_FAILED", 0, 0);
        return 0;
    }
    if (clears_loaded_program) {
        v5_program_runtime_clear(runtime);
        ++runtime->generation;
    }
    v5_program_runtime_set_delete_result(
        runtime, result, 1, "OK", 1, clears_loaded_program);
    return 1;
}

int v5_program_runtime_has_open_program(const V5ProgramRuntime *runtime)
{
    return runtime && runtime->loaded && runtime->mode == V5_PROGRAM_RUNTIME_PROGRAM && runtime->gcode_text && runtime->gcode_size > 0U;
}

int v5_program_runtime_has_mdi(const V5ProgramRuntime *runtime)
{
    return runtime && runtime->loaded && runtime->mode == V5_PROGRAM_RUNTIME_MDI && runtime->mdi_text[0];
}

const char *v5_program_runtime_mdi_text(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_mdi(runtime) ? runtime->mdi_text : "";
}

const char *v5_program_runtime_source_path(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) ? runtime->source_path : "";
}

const char *v5_program_runtime_source_sha256(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) ? runtime->source_sha256 : "";
}

unsigned int v5_program_runtime_loaded_epoch(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) ? runtime->loaded_epoch : 0U;
}

int v5_program_runtime_has_first_point_metadata(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) && runtime->source_path[0] &&
           runtime->source_sha256[0] && runtime->loaded_epoch > 0U &&
           runtime->first_point_valid && runtime->first_point_axis_mask;
}

int v5_program_runtime_first_point_axes(
    const V5ProgramRuntime *runtime,
    double axis_out[V5_COMMAND_AXIS_COUNT],
    unsigned int *axis_mask_out)
{
    unsigned int i;
    if (!v5_program_runtime_has_first_point_metadata(runtime) || !axis_out || !axis_mask_out) {
        return 0;
    }
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        axis_out[i] = runtime->first_point_axis[i];
    }
    *axis_mask_out = runtime->first_point_axis_mask;
    return 1;
}

int v5_program_runtime_set_mdi_line(V5ProgramRuntime *runtime, const char *line)
{
    size_t i;
    size_t j = 0U;

    if (!runtime || !line || !line[0]) {
        return 0;
    }
    v5_program_runtime_clear(runtime);
    for (i = 0U; line[i] && j + 1U < sizeof(runtime->mdi_text); ++i) {
        unsigned char ch = (unsigned char)line[i];
        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (ch >= 32U && ch < 127U) {
            runtime->mdi_text[j++] = (char)ch;
        }
    }
    runtime->mdi_text[j] = '\0';
    if (!runtime->mdi_text[0]) {
        return 0;
    }
    runtime->loaded = 1;
    runtime->mode = V5_PROGRAM_RUNTIME_MDI;
    runtime->line_count = 1U;
    snprintf(runtime->display_name, sizeof(runtime->display_name), "MDI");
    ++runtime->generation;
    return 1;
}

unsigned int v5_program_runtime_preview_trajectory(
    const V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int capacity)
{
    unsigned int i;
    unsigned int count;

    if (!v5_program_runtime_has_open_program(runtime) || !points || capacity == 0U) {
        return 0U;
    }
    count = runtime->preview_trajectory_count;
    if (count > capacity) {
        count = capacity;
    }
    for (i = 0U; i < count; ++i) {
        points[i] = runtime->preview_trajectory[i];
    }
    return count;
}

int v5_program_runtime_preview_wcs_index(
    const V5ProgramRuntime *runtime,
    unsigned int point_index,
    int *wcs_index_out)
{
    if (wcs_index_out) {
        *wcs_index_out = -1;
    }
    if (!v5_program_runtime_has_open_program(runtime) || point_index >= runtime->preview_trajectory_count ||
        point_index >= V5_PROGRAM_PREVIEW_POINT_COUNT) {
        return 0;
    }
    if (wcs_index_out) {
        *wcs_index_out = runtime->preview_wcs_indices[point_index];
    }
    return 1;
}

int v5_program_runtime_preview_break_before(
    const V5ProgramRuntime *runtime,
    unsigned int point_index)
{
    if (!v5_program_runtime_has_open_program(runtime) ||
        point_index >= runtime->preview_trajectory_count ||
        point_index >= V5_PROGRAM_PREVIEW_POINT_COUNT) {
        return 0;
    }
    return runtime->preview_break_before[point_index] ? 1 : 0;
}

int v5_program_runtime_preview_program_wcs_index(const V5ProgramRuntime *runtime)
{
    if (!v5_program_runtime_has_open_program(runtime)) {
        return -1;
    }
    return runtime->preview_wcs_mixed ? -1 : runtime->preview_program_wcs_index;
}

unsigned int v5_program_runtime_preview_wcs_mask(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) ? runtime->preview_wcs_mask : 0U;
}

int v5_program_runtime_publish_scene_ready(
    V5ProgramRuntime *runtime,
    unsigned int program_generation,
    unsigned long long scene_generation,
    unsigned int fit_generation)
{
    if (!v5_program_runtime_has_open_program(runtime) ||
        program_generation == 0U ||
        program_generation != runtime->loaded_epoch ||
        scene_generation == 0ULL ||
        fit_generation == 0U) {
        if (runtime) {
            runtime->program_scene_ready = 0;
        }
        return 0;
    }
    runtime->ready_program_generation = program_generation;
    runtime->ready_scene_generation = scene_generation;
    runtime->ready_fit_generation = fit_generation;
    runtime->program_scene_ready = 1;
    return 1;
}

void v5_program_runtime_invalidate_scene_ready(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    runtime->program_scene_ready = 0;
    runtime->ready_program_generation = 0U;
    runtime->ready_scene_generation = 0ULL;
    runtime->ready_fit_generation = 0U;
}

int v5_program_runtime_scene_ready(const V5ProgramRuntime *runtime)
{
    return v5_program_runtime_has_open_program(runtime) &&
        runtime->program_scene_ready &&
        runtime->ready_program_generation == runtime->loaded_epoch &&
        runtime->ready_scene_generation != 0ULL &&
        runtime->ready_fit_generation != 0U;
}

int v5_program_runtime_prepare_start(
    const V5ProgramRuntime *runtime,
    V5CommandRequest *request)
{
    if (!runtime || !request) {
        return 0;
    }

    memset(request, 0, sizeof(*request));
    if (v5_program_runtime_has_mdi(runtime)) {
        request->kind = V5_COMMAND_MDI_RUN;
        request->text_value = runtime->mdi_text;
        return 1;
    }
    if (v5_program_runtime_scene_ready(runtime)) {
        request->kind = V5_COMMAND_START;
        request->text_value = runtime->source_path;
        return 1;
    }
    return 0;
}
