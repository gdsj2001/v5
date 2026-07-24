#include "v5_program_scene_contour.h"

#include <math.h>
#include <string.h>

static void identity(V5ProgramScenePoseMatrix *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->value[0][0] = 1.0;
    matrix->value[1][1] = 1.0;
    matrix->value[2][2] = 1.0;
}

int main(void)
{
    V5StatusPoint points[3] = {{{0}}};
    uint8_t breaks[3] = {0};
    V5ProgramScenePoseMatrix matrix;
    double actual[V5_STATUS_AXIS_COUNT] = {5.0, 3.0, 4.0, 0.0, 0.0};
    double error = 0.0;

    identity(&matrix);
    points[1].axis[0] = 10.0;
    if (!v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error) ||
        fabs(error - 5.0) > 1.0e-9) return 1;

    actual[0] = -2.0;
    actual[1] = 0.0;
    actual[2] = 0.0;
    if (!v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error) ||
        fabs(error - 2.0) > 1.0e-9) return 2;

    matrix.value[0][3] = 1.0;
    matrix.value[1][3] = 2.0;
    matrix.value[2][3] = 3.0;
    actual[0] = 6.0;
    actual[1] = 5.0;
    actual[2] = 3.0;
    if (!v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error) ||
        fabs(error - 3.0) > 1.0e-9) return 3;

    identity(&matrix);
    actual[0] = 5.0;
    actual[1] = 0.0015;
    actual[2] = 0.0;
    if (!v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error) ||
        fabs(error - 0.002) > 1.0e-9) return 4;

    breaks[1] = 1U;
    if (v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error)) return 5;
    breaks[1] = 2U;
    if (v5_program_scene_tool_tip_contour_error(
            points, breaks, 2U, &matrix, actual, &error)) return 6;
    if (v5_program_scene_tool_tip_contour_error(
            points, breaks, 1U, &matrix, actual, &error)) return 7;
    return 0;
}
