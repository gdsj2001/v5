#include "v5_program_preview.h"
#include "v5_program_preview_parser.h"

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
    V5PreviewCandidate *candidates;
    V5PreviewParserState state;
    unsigned int candidate_count = 0U, count = 0U, segment_count = 0U;
    size_t pos = 0U;
    int incomplete = 0;

    if (!runtime || !text || !points) return 0U;
    candidates = (V5PreviewCandidate *)calloc(V5_PREVIEW_CANDIDATE_LIMIT, sizeof(*candidates));
    if (!candidates) return 0U;
    memset(&state, 0, sizeof(state));
    state.motion = -1;
    state.absolute = 1;
    state.arc_absolute = 0;
    state.plane = 17;
    state.unit_scale = 1.0;
    while (pos < size) {
        size_t line_end = pos;
        V5PreviewLine line;
        double target[V5_STATUS_AXIS_COUNT];
        int wcs_changed;
        int break_before;
        int move_complete = 1;
        int has_axis = 0;
        unsigned int i;

        while (line_end < size && text[line_end] != '\n' && text[line_end] != '\r') {
            ++line_end;
        }
        v5_preview_parse_line(text + pos, text + line_end, &state, &line);
        wcs_changed = line.wcs_index != state.wcs_index;
        if (line.unit_code == 20) state.unit_scale = 25.4;
        else if (line.unit_code == 21) state.unit_scale = 1.0;
        for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
            double scale = i < 3U ? state.unit_scale : 1.0;
            target[i] = state.axis[i];
            if (line.axis_set[i]) {
                target[i] = (line.absolute || line.g53) ? line.axis[i] * scale : state.axis[i] + line.axis[i] * scale;
                has_axis = 1;
            }
        }
        state.motion = line.motion;
        state.absolute = line.absolute;
        state.arc_absolute = line.arc_absolute;
        state.plane = line.plane;
        if (!has_axis) {
            if (wcs_changed) state.in_cut_segment = 0;
            state.wcs_index = line.wcs_index;
            goto next_line;
        }
        break_before = !state.in_cut_segment || wcs_changed ||
            fabs(target[3] - state.axis[3]) >= 180.0 || fabs(target[4] - state.axis[4]) >= 180.0;
        if (line.g53 || line.motion == 0 || (line.motion != 1 && line.motion != 2 && line.motion != 3)) {
            state.in_cut_segment = 0;
        } else {
            if (break_before) {
                if (segment_count >= V5_PREVIEW_SEGMENT_LIMIT) {
                    incomplete = 1;
                    break;
                }
                if (!v5_preview_append(candidates, &candidate_count, state.axis, state.wcs_index, 1)) {
                    incomplete = 1;
                    break;
                }
                ++segment_count;
            }
            if (line.motion == 1) {
                if (!v5_preview_append(candidates, &candidate_count, target, line.wcs_index, 0)) {
                    incomplete = 1;
                    break;
                }
            } else if (!v5_preview_append_arc(
                           candidates, &candidate_count, &state, &line, target, 0)) {
                incomplete = 1;
                move_complete = 0;
                state.in_cut_segment = 0;
            }
            if (move_complete) state.in_cut_segment = 1;
        }
        memcpy(state.axis, target, sizeof(state.axis));
        state.wcs_index = line.wcs_index;
next_line:
        pos = line_end;
        while (pos < size && (text[pos] == '\n' || text[pos] == '\r')) {
            ++pos;
        }
    }
    count = v5_preview_select_lod(runtime, candidates, candidate_count, points);
    free(candidates);
    runtime->preview_wcs_mask = 0U;
    runtime->preview_wcs_mixed = 0;
    runtime->preview_program_wcs_index = count ? runtime->preview_wcs_indices[0] : state.wcs_index;
    for (unsigned int i = 0U; i < count; ++i) {
        int wcs = runtime->preview_wcs_indices[i];
        if (wcs >= 0 && wcs <= 8) runtime->preview_wcs_mask |= 1U << (unsigned int)wcs;
        if (wcs != runtime->preview_program_wcs_index) runtime->preview_wcs_mixed = 1;
    }
    runtime->preview_candidate_count = candidate_count;
    runtime->preview_kept_count = count;
    runtime->preview_segment_count = segment_count;
    runtime->preview_truncated |= incomplete;
    runtime->preview_decimated = candidate_count > count;
    snprintf(runtime->preview_strategy, sizeof(runtime->preview_strategy), "%s",
             incomplete ? "lod_modal_g123_incomplete" :
             (runtime->preview_decimated ? "lod_modal_g123_decimated" : (count ? "modal_g123" : "none")));
    return count;
}
