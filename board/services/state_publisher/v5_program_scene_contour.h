#ifndef V5_PROGRAM_SCENE_CONTOUR_H
#define V5_PROGRAM_SCENE_CONTOUR_H

#include "v5_program_scene_model.h"

#include <stdint.h>

int v5_program_scene_tool_tip_contour_error(
    const V5StatusPoint *program_points,
    const uint8_t *break_before,
    uint32_t point_count,
    const V5ProgramScenePoseMatrix *pose_matrix,
    const double actual_tip[V5_STATUS_AXIS_COUNT],
    double *display_error_mm);

#endif
