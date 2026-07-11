#include "v5_program_preview.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int v5_program_preview_count_lines(const char *text, size_t size)
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
        if (wcs_index_out) {
            *wcs_index_out = 6;
        }
        return 1;
    }
    if (fabs(value - 59.2) < 1.0e-6) {
        if (wcs_index_out) {
            *wcs_index_out = 7;
        }
        return 1;
    }
    if (fabs(value - 59.3) < 1.0e-6) {
        if (wcs_index_out) {
            *wcs_index_out = 8;
        }
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

int v5_program_preview_find_first_point(
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
            int axis_i;
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
            axis_i = axis_index(*token);
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

unsigned int v5_program_preview_build(
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
            int axis_i;
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
            axis_i = axis_index(*p);
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
