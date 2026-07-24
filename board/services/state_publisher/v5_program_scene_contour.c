#include "v5_program_scene_contour.h"

#include <math.h>

#define V5_CONTOUR_DISTANCE_EPSILON 1.0e-18
#define V5_CONTOUR_DISPLAY_SCALE 1000.0

static int transform_point(
    const V5ProgramScenePoseMatrix *matrix,
    const double source[V5_STATUS_AXIS_COUNT],
    double transformed[3])
{
    unsigned int row;
    if (!matrix || !source || !transformed) return 0;
    for (row = 0U; row < 3U; ++row) {
        transformed[row] =
            matrix->value[row][0] * source[0] +
            matrix->value[row][1] * source[1] +
            matrix->value[row][2] * source[2] +
            matrix->value[row][3];
        if (!isfinite(transformed[row])) return 0;
    }
    return 1;
}

static double display_projection(double value)
{
    double scaled;
    double bucket;
    if (!isfinite(value) || value < 0.0) return value;
    scaled = value * V5_CONTOUR_DISPLAY_SCALE;
    if (!isfinite(scaled)) return value;
    bucket = floor(scaled + 0.5 + 1.0e-9);
    return bucket / V5_CONTOUR_DISPLAY_SCALE;
}

int v5_program_scene_tool_tip_contour_error(
    const V5StatusPoint *program_points,
    const uint8_t *break_before,
    uint32_t point_count,
    const V5ProgramScenePoseMatrix *pose_matrix,
    const double actual_tip[V5_STATUS_AXIS_COUNT],
    double *display_error_mm)
{
    double previous[3];
    double minimum_squared = 0.0;
    uint32_t index;
    int found = 0;

    if (display_error_mm) *display_error_mm = 0.0;
    if (!program_points || !break_before || point_count < 2U ||
        point_count > V5_STATUS_SCENE_POINT_COUNT || !pose_matrix ||
        !actual_tip || !display_error_mm ||
        !isfinite(actual_tip[0]) || !isfinite(actual_tip[1]) ||
        !isfinite(actual_tip[2]) ||
        !transform_point(pose_matrix, program_points[0].axis, previous)) {
        return 0;
    }
    for (index = 1U; index < point_count; ++index) {
        double current[3];
        double segment[3];
        double relative[3];
        double closest[3];
        double length_squared = 0.0;
        double dot = 0.0;
        double distance_squared = 0.0;
        double ratio;
        unsigned int axis;

        if (break_before[index] > 1U ||
            !transform_point(
                pose_matrix, program_points[index].axis, current)) {
            return 0;
        }
        if (break_before[index]) {
            for (axis = 0U; axis < 3U; ++axis) previous[axis] = current[axis];
            continue;
        }
        for (axis = 0U; axis < 3U; ++axis) {
            segment[axis] = current[axis] - previous[axis];
            relative[axis] = actual_tip[axis] - previous[axis];
            length_squared += segment[axis] * segment[axis];
            dot += relative[axis] * segment[axis];
        }
        if (length_squared > V5_CONTOUR_DISTANCE_EPSILON) {
            ratio = dot / length_squared;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            for (axis = 0U; axis < 3U; ++axis) {
                double delta;
                closest[axis] = previous[axis] + ratio * segment[axis];
                delta = actual_tip[axis] - closest[axis];
                distance_squared += delta * delta;
            }
            if (!isfinite(distance_squared)) return 0;
            if (!found || distance_squared < minimum_squared) {
                minimum_squared = distance_squared;
                found = 1;
            }
        }
        for (axis = 0U; axis < 3U; ++axis) previous[axis] = current[axis];
    }
    if (!found) return 0;
    *display_error_mm = display_projection(sqrt(minimum_squared));
    return isfinite(*display_error_mm) && *display_error_mm >= 0.0;
}
