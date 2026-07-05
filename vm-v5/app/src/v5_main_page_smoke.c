#include "v5_main_page.h"
#include "v5_lvgl_headless.h"
#include "v5_ui_status_view.h"

#include <stdio.h>
#include <string.h>

static int build_status(V5UiStatusView *status)
{
    V5StatusShmFrame frame;
    unsigned int i;

    v5_status_shm_frame_init(&frame);
    frame.typed_valid_mask =
        V5_STATUS_VALID_MCS |
        V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_TRAJECTORY |
        V5_STATUS_VALID_MODAL |
        V5_STATUS_VALID_SPINDLE_SPEED |
        V5_STATUS_VALID_LINEAR_VELOCITY |
        V5_STATUS_VALID_FEED_OVERRIDE |
        V5_STATUS_VALID_SPINDLE_OVERRIDE;
    for (i = 0; i < V5_STATUS_AXIS_COUNT; ++i) {
        frame.mcs[i] = (double)i + 2.5;
        frame.cmd_mcs[i] = (double)i + 2.0;
    }
    frame.trajectory_count = 3u;
    frame.trajectory[0].axis[0] = 0.0;
    frame.trajectory[0].axis[1] = 0.0;
    frame.trajectory[1].axis[0] = 4.0;
    frame.trajectory[1].axis[1] = 0.0;
    frame.trajectory[2].axis[0] = 4.0;
    frame.trajectory[2].axis[1] = 4.0;
    snprintf(frame.runtime_modal_text, sizeof(frame.runtime_modal_text), "G0 G17");
    frame.spindle_speed_rpm = 800.0;
    frame.linear_velocity_mm_per_min = 120.0;
    frame.feedrate_override = 100.0;
    frame.spindle_override = 90.0;
    return v5_ui_status_view_from_frame(status, &frame);
}

int main(void)
{
    V5MainPage page;
    V5UiStatusView status;
    lv_obj_t *screen;
    const char *mcs_text;
    const char *modal_text;

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    screen = lv_scr_act();
    if (!screen) {
        return 1;
    }
    if (!v5_main_page_create(&page, screen)) {
        return 2;
    }
    if (!build_status(&status)) {
        return 3;
    }
    if (!v5_main_page_apply_status(&page, &status)) {
        return 4;
    }

    mcs_text = lv_label_get_text(page.mcs_labels[0]);
    modal_text = lv_label_get_text(page.modal_label);
    if (!mcs_text || strcmp(mcs_text, "+00002.500") != 0) {
        return 5;
    }
    if (!modal_text || strcmp(modal_text, "G0 G17") != 0) {
        return 6;
    }
    if (page.trajectory_point_count != 3u) {
        return 7;
    }

    printf(
        "v5 main page controls: mcs=%s modal=%s points=%u feed=%s\n",
        mcs_text,
        modal_text,
        page.trajectory_point_count,
        lv_label_get_text(page.feed_override_label));
    return 0;
}
