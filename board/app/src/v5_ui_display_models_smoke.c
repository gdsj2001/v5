#include "v5_coordinate_panel.h"
#include "v5_remote_metrics.h"
#include "v5_ui_status_view.h"

#include <string.h>

int main(void)
{
    unsigned int axis;
    V5StatusShmFrame frame;
    V5UiStatusView status;
    V5CoordinatePanelSnapshot panel;
    char cpu0[24];
    char cpu1[24];
    v5_status_shm_frame_init(&frame);
    frame.status_epoch = 10000000000ULL;
    frame.position_writer_identity = 17U;
    frame.source_acquired_mono_ns = frame.status_epoch;
    frame.source_generation = 5ULL;
    frame.scene_generation = 9ULL;
    frame.typed_valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_CPU_USAGE | V5_STATUS_VALID_DISPLAY_SCENE;
    frame.mcs[0] = 1.25;
    frame.cmd_mcs[0] = 1.0;
    frame.cpu0_percent = 21.4;
    frame.cpu1_percent = 30.6;
    frame.cpu_sample_generation = 7ULL;
    frame.cpu_sample_monotonic_ns = 9000000000ULL;
    for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
        frame.unit_per_count[axis] = 0.0001;
        frame.following_error[axis] = frame.mcs[axis] - frame.cmd_mcs[axis];
        frame.display_digits[axis] = 3U;
    }
    frame.display_scene.flags = V5_STATUS_SCENE_FLAG_VALID | V5_STATUS_SCENE_FLAG_PROGRAM;
    frame.display_scene.native_generation = 5ULL;
    frame.display_scene.view_generation = 1ULL;
    frame.display_scene.fit_generation = 1ULL;
    frame.display_scene.build_count = 1ULL;
    frame.display_scene.project_count = 1ULL;
    frame.display_scene.point_count = 2U;
    frame.display_scene.points[0].x = 10.0f;
    frame.display_scene.points[0].y = 20.0f;
    frame.display_scene.points[1].x = 30.0f;
    frame.display_scene.points[1].y = 40.0f;
    if (!v5_ui_status_view_from_frame(&status, &frame) ||
        (status.valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) == 0U ||
        !status.display_scene ||
        status.display_scene->point_count != 2U ||
        status.display_scene->points[1].x != 30.0f) return 1;
    v5_coordinate_panel_from_status(&status, &panel);
    if (strcmp(panel.lines[0].mcs_text, "+00001.250") != 0) return 2;
    v5_remote_metrics_display_text(&status, cpu0, sizeof(cpu0), cpu1, sizeof(cpu1));
    if (strstr(cpu0, "21%") == 0 || strstr(cpu1, "31%") == 0) return 3;
    frame.display_scene.point_count = V5_STATUS_SCENE_POINT_COUNT + 1U;
    if (!v5_ui_status_view_from_frame(&status, &frame) ||
        (status.valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) != 0U) return 4;
    return 0;
}
