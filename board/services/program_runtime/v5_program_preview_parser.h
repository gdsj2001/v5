#ifndef V5_PROGRAM_PREVIEW_PARSER_H
#define V5_PROGRAM_PREVIEW_PARSER_H

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define V5_PREVIEW_CANDIDATE_LIMIT 65536U
#define V5_PREVIEW_SEGMENT_LIMIT 64U
#define V5_PREVIEW_ARC_STEP_RAD (5.0 * M_PI / 180.0)

typedef struct V5PreviewCandidate {
    V5StatusPoint point;
    int wcs_index;
    unsigned char break_before;
} V5PreviewCandidate;

typedef struct V5PreviewParserState {
    double axis[V5_STATUS_AXIS_COUNT];
    int motion;
    int absolute;
    int arc_absolute;
    int plane;
    int wcs_index;
    double unit_scale;
    int in_cut_segment;
} V5PreviewParserState;

typedef struct V5PreviewLine {
    double axis[V5_STATUS_AXIS_COUNT];
    unsigned char axis_set[V5_STATUS_AXIS_COUNT];
    double center[3];
    unsigned char center_set[3];
    double radius;
    int radius_set;
    int motion;
    int absolute;
    int arc_absolute;
    int plane;
    int wcs_index;
    int unit_code;
    int g53;
} V5PreviewLine;

static int v5_preview_close(double left, double right)
{
    return fabs(left - right) < 1.0e-7;
}

static int v5_preview_axis_index(int letter)
{
    switch (toupper(letter)) {
    case 'X': return 0;
    case 'Y': return 1;
    case 'Z': return 2;
    case 'A':
    case 'B': return 3;
    case 'C': return 4;
    default: return -1;
    }
}

static int v5_preview_center_index(int letter)
{
    switch (toupper(letter)) {
    case 'I': return 0;
    case 'J': return 1;
    case 'K': return 2;
    default: return -1;
    }
}

static int v5_preview_wcs_index(double code)
{
    if (code >= 54.0 && code <= 59.0 && v5_preview_close(code, floor(code))) {
        return (int)code - 54;
    }
    if (v5_preview_close(code, 59.1)) return 6;
    if (v5_preview_close(code, 59.2)) return 7;
    if (v5_preview_close(code, 59.3)) return 8;
    return -1;
}

static void v5_preview_parse_g_code(V5PreviewLine *line, double code)
{
    int wcs;
    if (!line) return;
    if (v5_preview_close(code, 0.0) || v5_preview_close(code, 1.0) ||
        v5_preview_close(code, 2.0) || v5_preview_close(code, 3.0)) {
        line->motion = (int)code;
    } else if (v5_preview_close(code, 17.0) || v5_preview_close(code, 18.0) || v5_preview_close(code, 19.0)) {
        line->plane = (int)code;
    } else if (v5_preview_close(code, 20.0)) {
        line->unit_code = 20;
    } else if (v5_preview_close(code, 21.0)) {
        line->unit_code = 21;
    } else if (v5_preview_close(code, 53.0)) {
        line->g53 = 1;
    } else if (v5_preview_close(code, 90.0)) {
        line->absolute = 1;
    } else if (v5_preview_close(code, 91.0)) {
        line->absolute = 0;
    } else if (v5_preview_close(code, 90.1)) {
        line->arc_absolute = 1;
    } else if (v5_preview_close(code, 91.1)) {
        line->arc_absolute = 0;
    } else if ((wcs = v5_preview_wcs_index(code)) >= 0) {
        line->wcs_index = wcs;
    }
}

static void v5_preview_parse_line(const char *begin, const char *end, const V5PreviewParserState *state, V5PreviewLine *line)
{
    const char *p = begin;
    memset(line, 0, sizeof(*line));
    line->motion = state->motion;
    line->absolute = state->absolute;
    line->arc_absolute = state->arc_absolute;
    line->plane = state->plane;
    line->wcs_index = state->wcs_index;
    while (p < end) {
        int letter;
        char *number_end;
        double value;
        if (*p == ';') break;
        if (*p == '(') {
            while (p < end && *p != ')') ++p;
            if (p < end) ++p;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            ++p;
            continue;
        }
        letter = toupper((unsigned char)*p++);
        value = strtod(p, &number_end);
        if (number_end == p || number_end > end) continue;
        p = number_end;
        if (letter == 'G') {
            v5_preview_parse_g_code(line, value);
        } else if (v5_preview_axis_index(letter) >= 0) {
            int axis_i = v5_preview_axis_index(letter);
            line->axis[axis_i] = value;
            line->axis_set[axis_i] = 1U;
        } else if (v5_preview_center_index(letter) >= 0) {
            int center_i = v5_preview_center_index(letter);
            line->center[center_i] = value;
            line->center_set[center_i] = 1U;
        } else if (letter == 'R') {
            line->radius = value;
            line->radius_set |= 1;
        }
    }
}

static int v5_preview_append(
    V5PreviewCandidate *items,
    unsigned int *count,
    const double axis[V5_STATUS_AXIS_COUNT],
    int wcs_index,
    int break_before)
{
    unsigned int i;
    if (!items || !count || *count >= V5_PREVIEW_CANDIDATE_LIMIT) return 0;
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) items[*count].point.axis[i] = axis[i];
    items[*count].wcs_index = wcs_index;
    items[*count].break_before = break_before ? 1U : 0U;
    *count += 1U;
    return 1;
}

static void v5_preview_plane_axes(int plane, int *u, int *v, int *cu, int *cv)
{
    if (plane == 18) {
        *u = 2; *v = 0; *cu = 2; *cv = 0;
    } else if (plane == 19) {
        *u = 1; *v = 2; *cu = 1; *cv = 2;
    } else {
        *u = 0; *v = 1; *cu = 0; *cv = 1;
    }
}

static int v5_preview_radius_center(
    double su, double sv, double eu, double ev, double radius, int clockwise, double *cu, double *cv)
{
    double dx = eu - su;
    double dy = ev - sv;
    double chord = hypot(dx, dy);
    double r = fabs(radius);
    double h;
    double sign;
    if (chord <= 1.0e-12 || r < chord * 0.5) return 0;
    h = sqrt(fmax(0.0, (r * r) - (chord * chord * 0.25)));
    sign = (clockwise ? -1.0 : 1.0) * (radius < 0.0 ? -1.0 : 1.0);
    *cu = (su + eu) * 0.5 + sign * (-dy / chord) * h;
    *cv = (sv + ev) * 0.5 + sign * (dx / chord) * h;
    return 1;
}

static int v5_preview_append_arc(
    V5PreviewCandidate *items,
    unsigned int *count,
    const V5PreviewParserState *state,
    const V5PreviewLine *line,
    const double target[V5_STATUS_AXIS_COUNT],
    int break_before)
{
    int u, v, center_u, center_v;
    double cu, cv, start_angle, end_angle, sweep, radius;
    unsigned int step, steps, axis_i;
    int clockwise = line->motion == 2;
    v5_preview_plane_axes(line->plane, &u, &v, &center_u, &center_v);
    if ((line->radius_set & 1) != 0) {
        if (!v5_preview_radius_center(state->axis[u], state->axis[v], target[u], target[v],
                                      line->radius * state->unit_scale, clockwise, &cu, &cv)) return 0;
    } else if (line->center_set[center_u] || line->center_set[center_v]) {
        cu = line->center[center_u] * state->unit_scale;
        cv = line->center[center_v] * state->unit_scale;
        if (!line->arc_absolute) {
            cu += state->axis[u];
            cv += state->axis[v];
        }
    } else {
        return 0;
    }
    radius = hypot(state->axis[u] - cu, state->axis[v] - cv);
    if (radius <= 1.0e-12) return 0;
    start_angle = atan2(state->axis[v] - cv, state->axis[u] - cu);
    end_angle = atan2(target[v] - cv, target[u] - cu);
    sweep = end_angle - start_angle;
    if (clockwise) {
        while (sweep >= 0.0) sweep -= 2.0 * M_PI;
    } else {
        while (sweep <= 0.0) sweep += 2.0 * M_PI;
    }
    steps = (unsigned int)ceil(fabs(sweep) / V5_PREVIEW_ARC_STEP_RAD);
    if (steps < 1U) steps = 1U;
    if (steps > 360U) steps = 360U;
    for (step = 1U; step <= steps; ++step) {
        double ratio = (double)step / (double)steps;
        double angle = start_angle + sweep * ratio;
        double axis[V5_STATUS_AXIS_COUNT];
        for (axis_i = 0U; axis_i < V5_STATUS_AXIS_COUNT; ++axis_i) {
            axis[axis_i] = state->axis[axis_i] + (target[axis_i] - state->axis[axis_i]) * ratio;
        }
        axis[u] = cu + cos(angle) * radius;
        axis[v] = cv + sin(angle) * radius;
        if (!v5_preview_append(items, count, axis, line->wcs_index, break_before && step == 1U)) return 0;
    }
    return 1;
}

static int v5_preview_candidate_important(const V5PreviewCandidate *items, unsigned int count, unsigned int index)
{
    return index == 0U || index + 1U == count || items[index].break_before ||
           (index + 1U < count && items[index + 1U].break_before);
}

static unsigned int v5_preview_select_lod(
    V5ProgramRuntime *runtime,
    const V5PreviewCandidate *items,
    unsigned int count,
    V5StatusPoint *points)
{
    unsigned int important = 0U, ordinary = 0U, ordinary_seen = 0U, out = 0U, i;
    unsigned int ordinary_budget;
    for (i = 0U; i < count; ++i) {
        if (v5_preview_candidate_important(items, count, i)) ++important; else ++ordinary;
    }
    if (important > V5_PROGRAM_PREVIEW_POINT_COUNT) {
        runtime->preview_truncated = 1;
        important = V5_PROGRAM_PREVIEW_POINT_COUNT;
    }
    ordinary_budget = V5_PROGRAM_PREVIEW_POINT_COUNT - important;
    for (i = 0U; i < count && out < V5_PROGRAM_PREVIEW_POINT_COUNT; ++i) {
        int keep = v5_preview_candidate_important(items, count, i);
        if (!keep && ordinary && ordinary_budget) {
            unsigned int before = (ordinary_seen * ordinary_budget) / ordinary;
            unsigned int after = ((ordinary_seen + 1U) * ordinary_budget) / ordinary;
            keep = after > before;
            ++ordinary_seen;
        } else if (!keep) {
            ++ordinary_seen;
        }
        if (!keep) continue;
        points[out] = items[i].point;
        runtime->preview_wcs_indices[out] = items[i].wcs_index;
        runtime->preview_break_before[out] = items[i].break_before;
        ++out;
    }
    return out;
}

#endif
