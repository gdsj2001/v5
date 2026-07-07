#include "v5_program_runtime.h"
#include "v5_sha256.h"

#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static char *v5_program_runtime_alloc_text(size_t size)
{
    void *mapped;
    if (size == 0U || size > V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES + 1U) {
        return 0;
    }
    mapped = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        return 0;
    }
    return (char *)mapped;
}

static void v5_program_runtime_clear(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->gcode_text) {
        if (runtime->gcode_text_mmap && runtime->gcode_map_size > 0U) {
            (void)munmap(runtime->gcode_text, runtime->gcode_map_size);
        } else {
            free(runtime->gcode_text);
        }
        (void)malloc_trim(0);
    }
    runtime->gcode_text = 0;
    runtime->gcode_size = 0U;
    runtime->gcode_map_size = 0U;
    runtime->gcode_text_mmap = 0;
    runtime->line_count = 0U;
    runtime->preview_trajectory_count = 0U;
    memset(runtime->preview_wcs_indices, 0, sizeof(runtime->preview_wcs_indices));
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
    runtime->mdi_text[0] = '\0';
}

static unsigned int v5_program_runtime_count_lines(const char *text, size_t size)
{
    size_t i;
    unsigned int lines = 0U;

    for (i = 0U; i < size; ++i) {
        if (text[i] == '\n') {
            ++lines;
        }
    }
    if (size > 0U && text[size - 1U] != '\n') {
        ++lines;
    }
    return lines;
}

static int token_is_motion(const char *p, int *motion_mode)
{
    char *endp;
    long value;

    if (!p || (*p != 'G' && *p != 'g')) {
        return 0;
    }
    value = strtol(p + 1, &endp, 10);
    if (endp == p + 1) {
        return 0;
    }
    if (value == 0L || value == 1L) {
        *motion_mode = (int)value;
        return 1;
    }
    return 0;
}

static int axis_index(char axis)
{
    switch (axis) {
    case 'X':
    case 'x':
        return 0;
    case 'Y':
    case 'y':
        return 1;
    case 'Z':
    case 'z':
        return 2;
    case 'A':
    case 'a':
        return 3;
    case 'C':
    case 'c':
        return 4;
    default:
        return -1;
    }
}

static void append_preview_point(
    V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int *count,
    unsigned int *candidate_count,
    int *truncated,
    const double axis[V5_STATUS_AXIS_COUNT],
    int wcs_index)
{
    unsigned int i;
    if (candidate_count) {
        *candidate_count += 1U;
    }
    if (!points || !count || *count >= V5_PROGRAM_PREVIEW_POINT_COUNT) {
        if (truncated) {
            *truncated = 1;
        }
        return;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        points[*count].axis[i] = axis[i];
    }
    if (runtime) {
        if (wcs_index < 0 || wcs_index > 8) {
            wcs_index = 0;
        }
        runtime->preview_wcs_indices[*count] = wcs_index;
        runtime->preview_wcs_mask |= (1U << (unsigned int)wcs_index);
        if (*count == 0U) {
            runtime->preview_program_wcs_index = wcs_index;
            runtime->preview_wcs_mixed = 0;
        } else if (runtime->preview_program_wcs_index != wcs_index) {
            runtime->preview_wcs_mixed = 1;
        }
    }
    *count += 1U;
}

static int token_is_g53(const char *p)
{
    char *endp;
    long value;
    if (!p || (*p != 'G' && *p != 'g')) {
        return 0;
    }
    value = strtol(p + 1, &endp, 10);
    return endp != p + 1 && value == 53L;
}

static int token_wcs_index(const char *p, int *wcs_index_out)
{
    char *endp;
    double value;
    long rounded;
    if (wcs_index_out) {
        *wcs_index_out = -1;
    }
    if (!p || (*p != 'G' && *p != 'g')) {
        return 0;
    }
    value = strtod(p + 1, &endp);
    if (endp == p + 1) {
        return 0;
    }
    rounded = (long)(value >= 0.0 ? value + 0.5 : value - 0.5);
    if (fabs(value - (double)rounded) < 1.0e-6 && rounded >= 54L && rounded <= 59L) {
        if (wcs_index_out) {
            *wcs_index_out = (int)(rounded - 54L);
        }
        return 1;
    }
    if (fabs(value - 59.1) < 1.0e-6) {
        if (wcs_index_out) *wcs_index_out = 6;
        return 1;
    }
    if (fabs(value - 59.2) < 1.0e-6) {
        if (wcs_index_out) *wcs_index_out = 7;
        return 1;
    }
    if (fabs(value - 59.3) < 1.0e-6) {
        if (wcs_index_out) *wcs_index_out = 8;
        return 1;
    }
    return 0;
}

static unsigned int axis_mask_for_index(int axis_i)
{
    switch (axis_i) {
    case 0:
        return V5_COMMAND_AXIS_X_MASK;
    case 1:
        return V5_COMMAND_AXIS_Y_MASK;
    case 2:
        return V5_COMMAND_AXIS_Z_MASK;
    case 3:
        return V5_COMMAND_AXIS_A_MASK;
    case 4:
        return V5_COMMAND_AXIS_C_MASK;
    default:
        return 0U;
    }
}

static int v5_program_runtime_find_first_point(
    const char *text,
    size_t size,
    double axis_out[V5_COMMAND_AXIS_COUNT],
    unsigned int *axis_mask_out)
{
    double axis[V5_COMMAND_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    size_t pos = 0U;
    int modal_motion = -1;

    if (!text || !axis_out || !axis_mask_out) {
        return 0;
    }
    *axis_mask_out = 0U;
    while (pos < size) {
        size_t line_end = pos;
        int line_motion = modal_motion;
        unsigned int line_axis_mask = 0U;
        double next_axis[V5_COMMAND_AXIS_COUNT];
        unsigned int i;

        while (line_end < size && text[line_end] != '\n' && text[line_end] != '\r') {
            ++line_end;
        }
        for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
            next_axis[i] = axis[i];
        }
        for (i = (unsigned int)pos; i < (unsigned int)line_end;) {
            const char *token = text + i;
            if (*token == '(' || *token == ';') {
                break;
            }
            if (isspace((unsigned char)*token)) {
                ++i;
                continue;
            }
            if (token_is_motion(token, &line_motion)) {
                ++i;
                while (i < (unsigned int)line_end && !isspace((unsigned char)text[i])) {
                    ++i;
                }
                continue;
            }
            int axis_i = axis_index(*token);
            if (axis_i >= 0) {
                char *endp;
                double value = strtod(token + 1, &endp);
                if (endp != token + 1) {
                    next_axis[axis_i] = value;
                    line_axis_mask |= axis_mask_for_index(axis_i);
                    i = (unsigned int)(endp - text);
                    continue;
                }
            }
            ++i;
        }
        if (line_motion == 0 || line_motion == 1) {
            modal_motion = line_motion;
        }
        if (line_axis_mask) {
            for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
                axis[i] = next_axis[i];
            }
            if (line_motion == 0 || line_motion == 1) {
                for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
                    axis_out[i] = next_axis[i];
                }
                *axis_mask_out = line_axis_mask;
                return 1;
            }
        }
        pos = line_end;
        while (pos < size && (text[pos] == '\n' || text[pos] == '\r')) {
            ++pos;
        }
    }
    return 0;
}

static unsigned int v5_program_runtime_build_preview(
    V5ProgramRuntime *runtime,
    const char *text,
    size_t size,
    V5StatusPoint *points)
{
    double axis[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    unsigned int count = 0U;
    unsigned int candidate_count = 0U;
    unsigned int segment_count = 0U;
    size_t pos = 0U;
    int modal_motion = -1;
    int current_wcs_index = 0;
    int in_cut_segment = 0;
    int truncated = 0;

    while (text && pos < size) {
        size_t line_end = pos;
        int line_motion = modal_motion;
        int line_g53 = 0;
        int has_axis = 0;
        double next_axis[V5_STATUS_AXIS_COUNT];
        unsigned int i;

        while (line_end < size && text[line_end] != '\n' && text[line_end] != '\r') {
            ++line_end;
        }
        for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
            next_axis[i] = axis[i];
        }
        for (i = (unsigned int)pos; i < (unsigned int)line_end;) {
            const char *p = text + i;
            if (*p == '(' || *p == ';') {
                break;
            }
            if (isspace((unsigned char)*p)) {
                ++i;
                continue;
            }
            if (token_is_g53(p)) {
                line_g53 = 1;
                ++i;
                while (i < (unsigned int)line_end && !isspace((unsigned char)text[i])) {
                    ++i;
                }
                continue;
            }
            {
                int next_wcs_index = -1;
                if (token_wcs_index(p, &next_wcs_index)) {
                    current_wcs_index = next_wcs_index;
                    ++i;
                    while (i < (unsigned int)line_end && !isspace((unsigned char)text[i])) {
                        ++i;
                    }
                    continue;
                }
            }
            if (token_is_motion(p, &line_motion)) {
                ++i;
                while (i < (unsigned int)line_end && !isspace((unsigned char)text[i])) {
                    ++i;
                }
                continue;
            }
            int axis_i = axis_index(*p);
            if (axis_i >= 0) {
                char *endp;
                double value = strtod(p + 1, &endp);
                if (endp != p + 1) {
                    next_axis[axis_i] = value;
                    has_axis = 1;
                    i = (unsigned int)(endp - text);
                    continue;
                }
            }
            ++i;
        }
        if (line_motion == 0 || line_motion == 1) {
            modal_motion = line_motion;
        }
        if (has_axis) {
            if (line_motion == 1 && !line_g53) {
                if (!in_cut_segment) {
                    append_preview_point(runtime, points, &count, &candidate_count, &truncated, axis, current_wcs_index);
                    in_cut_segment = 1;
                    segment_count += 1U;
                    if (truncated) {
                        break;
                    }
                }
                append_preview_point(runtime, points, &count, &candidate_count, &truncated, next_axis, current_wcs_index);
                if (truncated) {
                    break;
                }
            } else {
                in_cut_segment = 0;
            }
            for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
                axis[i] = next_axis[i];
            }
        }
        pos = line_end;
        while (pos < size && (text[pos] == '\n' || text[pos] == '\r')) {
            ++pos;
        }
    }
    if (runtime) {
        if (count == 0U) {
            runtime->preview_program_wcs_index = current_wcs_index;
            runtime->preview_wcs_mask = 0U;
            runtime->preview_wcs_mixed = 0;
        }
        runtime->preview_candidate_count = candidate_count;
        runtime->preview_kept_count = count;
        runtime->preview_segment_count = segment_count;
        runtime->preview_truncated = truncated;
        runtime->preview_decimated = truncated || candidate_count > count;
        snprintf(
            runtime->preview_strategy,
            sizeof(runtime->preview_strategy),
            "%s",
            truncated ? "multi_segment_g1_no_g53_truncated" : (count ? "multi_segment_g1_no_g53" : "none"));
    }
    return count;
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
        (void)munmap(buffer, size + 1U);
        v5_program_runtime_set_result(runtime, result, 0, "PROGRAM_READ_FAILED");
        return 0;
    }

    buffer[size] = '\0';
    runtime->gcode_text = buffer;
    runtime->gcode_size = size;
    runtime->gcode_map_size = size + 1U;
    runtime->gcode_text_mmap = 1;
    runtime->line_count = v5_program_runtime_count_lines(buffer, size);
    runtime->preview_trajectory_count = v5_program_runtime_build_preview(runtime, buffer, size, runtime->preview_trajectory);
    runtime->first_point_valid = v5_program_runtime_find_first_point(buffer, size, runtime->first_point_axis, &runtime->first_point_axis_mask);
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
    if (v5_program_runtime_has_open_program(runtime)) {
        request->kind = V5_COMMAND_START;
        return 1;
    }
    return 0;
}
