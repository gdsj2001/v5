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
    after.raw_mcs[3] = 360.0;
    after.cmd_mcs[3] = 15.0;
    after.raw_cmd_mcs[4] = -720.0;
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
    printf("V5_UI_SHELL_REFRESH_SMOKE_OK\n");
    return 0;
}
