#define V5_UI_SHELL_REFRESH_CLASSIFIER_ONLY
#include "v5_ui_shell_internal.h"

#define V5_MAIN_PAGE_REFRESH_DYNAMIC (1U << 0)
#define V5_MAIN_PAGE_REFRESH_POSE (1U << 4)

#include <stdio.h>

int main(void)
{
    unsigned int flags;
    V5UiStatusView before = {0};
    V5UiStatusView after = {0};
    V5StatusDisplayScene before_scene = {0};
    V5StatusDisplayScene after_scene = {0};

    flags = shell_refresh_classify_changes(1, 0, 0, 1);
    if (flags != V5_MAIN_PAGE_REFRESH_DYNAMIC) {
        return 1;
    }
    flags = shell_refresh_classify_changes(1, 1, 0, 1);
    if (flags != (V5_MAIN_PAGE_REFRESH_DYNAMIC | V5_MAIN_PAGE_REFRESH_POSE)) {
        return 2;
    }
    flags = shell_refresh_classify_changes(0, 0, 1, 1);
    if (flags != V5_MAIN_PAGE_REFRESH_POSE) {
        return 3;
    }
    flags = shell_refresh_classify_changes(1, 1, 1, 0);
    if (flags != V5_MAIN_PAGE_REFRESH_DYNAMIC) {
        return 4;
    }
    before.valid_mask = V5_STATUS_VALID_MCS |
        V5_STATUS_VALID_CMD_MCS | V5_STATUS_VALID_TRAJECTORY;
    after = before;
    after.mcs[0] = 12.5;
    after.mcs[1] = -7.25;
    after.mcs[2] = 3.0;
    after.cmd_mcs[3] = 15.0;
    after.trajectory_count = 1U;
    after.trajectory[0].axis[0] = 9.0;
    if (!shell_refresh_model_pose_equal(&before, &after)) {
        return 5;
    }
    after.mcs[3] = 0.0001;
    if (!shell_refresh_model_pose_equal(&before, &after)) {
        return 6;
    }
    after.mcs[3] = 0.001;
    if (shell_refresh_model_pose_equal(&before, &after)) {
        return 7;
    }
    after.mcs[3] = 0.0;
    after.mcs[4] = -0.001;
    if (shell_refresh_model_pose_equal(&before, &after)) {
        return 8;
    }
    after.mcs[4] = 0.0;
    after.valid_mask &= ~V5_STATUS_VALID_MCS;
    if (shell_refresh_model_pose_equal(&before, &after)) {
        return 9;
    }
    before = (V5UiStatusView){0};
    after = before;
    if (!shell_refresh_display_scene_equal(&before, &after)) {
        return 10;
    }
    before.valid_mask = V5_STATUS_VALID_DISPLAY_SCENE;
    before.position_writer_identity = 17U;
    before.scene_generation = 41ULL;
    before.display_scene = &before_scene;
    before_scene.program_source_identity = 101ULL;
    before_scene.program_generation = 7ULL;
    before_scene.native_generation = 31ULL;
    before_scene.active_model_generation = 11ULL;
    before_scene.rtcp_generation = 31ULL;
    before_scene.wcs_generation = 13ULL;
    before_scene.view_generation = 3ULL;
    before_scene.fit_generation = 3ULL;
    before_scene.build_count = 19ULL;
    before_scene.project_count = 19ULL;
    before_scene.active_model_id = 1U;
    before_scene.flags = V5_STATUS_SCENE_FLAG_VALID |
        V5_STATUS_SCENE_FLAG_PROGRAM;
    before_scene.point_count = 3U;
    before_scene.segment_count = 2U;
    before_scene.marker_count = 1U;
    before_scene.program_wcs_mask = 1U;
    after = before;
    after_scene = before_scene;
    after.display_scene = &after_scene;
    if (!shell_refresh_display_scene_equal(&before, &after)) {
        return 11;
    }
    after.scene_generation += 1ULL;
    if (shell_refresh_display_scene_equal(&before, &after)) {
        return 12;
    }
    after = before;
    after_scene = before_scene;
    after.display_scene = &after_scene;
    after_scene.program_generation += 1ULL;
    if (shell_refresh_display_scene_equal(&before, &after)) {
        return 13;
    }
    after = before;
    after.valid_mask &= ~V5_STATUS_VALID_DISPLAY_SCENE;
    if (shell_refresh_display_scene_equal(&before, &after)) {
        return 14;
    }
    printf("V5_UI_SHELL_REFRESH_SMOKE_OK\n");
    return 0;
}
