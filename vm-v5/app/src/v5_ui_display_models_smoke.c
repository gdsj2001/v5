#include "v5_coordinate_panel.h"
#include "v5_toolpath_display.h"
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
        frame.mcs[i] = (double)i + 1.25;
        frame.cmd_mcs[i] = (double)i + 1.00;
    }
    frame.trajectory_count = 3u;
    frame.trajectory[0].axis[0] = 0.0;
    frame.trajectory[0].axis[1] = 0.0;
    frame.trajectory[1].axis[0] = 10.0;
    frame.trajectory[1].axis[1] = 0.0;
    frame.trajectory[2].axis[0] = 10.0;
    frame.trajectory[2].axis[1] = 5.0;
    snprintf(frame.runtime_modal_text, sizeof(frame.runtime_modal_text), "G0 G17 G54");
    frame.spindle_speed_rpm = 1200.0;
    frame.linear_velocity_mm_per_min = 345.6;
    frame.feedrate_override = 110.0;
    frame.spindle_override = 95.0;
    return v5_ui_status_view_from_frame(status, &frame);
}

int main(void)
{
    V5UiStatusView status;
    V5CoordinatePanelSnapshot panel;
    V5ToolpathDisplaySnapshot display;

    if (!build_status(&status)) {
        return 1;
    }
    v5_coordinate_panel_from_status(&status, &panel);
    if (panel.lines[0].axis != 'X' || strcmp(panel.lines[0].mcs_text, "+00001.250") != 0) {
        return 2;
    }
    if (strcmp(panel.lines[0].following_error_text, "+00000.250") != 0) {
        return 3;
    }
    if (strcmp(panel.modal_text, "G0 G17 G54") != 0) {
        return 4;
    }
    if (strcmp(panel.feed_override_text, "110.0%") != 0) {
        return 5;
    }

    v5_toolpath_display_from_status(&status, V5_TOOLPATH_DISPLAY_XY, 200.0, 100.0, &display);
    if (!display.trajectory_valid || display.point_count != 3u) {
        return 6;
    }
    if (!display.mcs_valid || !display.cmd_valid) {
        return 7;
    }

    printf(
        "v5 ui display models: axis=%c mcs=%s err=%s modal=%s points=%u feed=%s\n",
        panel.lines[0].axis,
        panel.lines[0].mcs_text,
        panel.lines[0].following_error_text,
        panel.modal_text,
        display.point_count,
        panel.feed_override_text);
    return 0;
}
