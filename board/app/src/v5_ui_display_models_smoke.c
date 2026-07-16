#include "v5_coordinate_panel.h"
#include "v5_toolpath_display.h"
#include "v5_ui_status_view.h"

#include <math.h>
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
        V5_STATUS_VALID_SPINDLE_SPEED |
        V5_STATUS_VALID_LINEAR_VELOCITY |
        V5_STATUS_VALID_FEED_OVERRIDE |
        V5_STATUS_VALID_SPINDLE_OVERRIDE;
    for (i = 0; i < V5_STATUS_AXIS_COUNT; ++i) {
        frame.mcs[i] = (double)i + 1.25;
        frame.cmd_mcs[i] = (double)i + 1.00;
    }
    frame.mcs[3] = -720.25;
    frame.mcs[4] = -3599.999;
    frame.cmd_mcs[3] = 720.0;
    frame.cmd_mcs[4] = 360.25;
    frame.trajectory_count = 3u;
    frame.trajectory[0].axis[0] = 0.0;
    frame.trajectory[0].axis[1] = 0.0;
    frame.trajectory[1].axis[0] = 10.0;
    frame.trajectory[1].axis[1] = 0.0;
    frame.trajectory[2].axis[0] = 10.0;
    frame.trajectory[2].axis[1] = 5.0;
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
    V5ToolpathDisplayFit fit;
    V5ToolpathScreenPoint origin;
    V5ToolpathScreenPoint x_axis;
    V5ToolpathScreenPoint y_axis;
    V5ToolpathScreenPoint z_axis;
    double x_len;
    double y_len;
    double z_len;
    double origin_world[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double x_axis_world[V5_STATUS_AXIS_COUNT] = {40.0, 0.0, 0.0, 0.0, 0.0};
    double y_axis_world[V5_STATUS_AXIS_COUNT] = {0.0, 40.0, 0.0, 0.0, 0.0};
    double z_axis_world[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 40.0, 0.0, 0.0};

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
    if (strcmp(panel.modal_text, "--") != 0) {
        return 4;
    }
    if (strcmp(panel.feed_override_text, "110.0%") != 0) {
        return 5;
    }
    if (fabs(status.raw_mcs[3] + 720.25) > 1.0e-9 ||
        fabs(status.raw_mcs[4] + 3599.999) > 1.0e-9 ||
        fabs(status.mcs[3] - 359.75) > 1.0e-9 ||
        fabs(status.mcs[4] - 0.001) > 1.0e-9 ||
        fabs(status.cmd_mcs[3]) > 1.0e-9 ||
        fabs(status.cmd_mcs[4] - 0.25) > 1.0e-9) {
        return 11;
    }
    if (strcmp(panel.lines[3].mcs_text, "+00359.750") != 0 ||
        strcmp(panel.lines[4].mcs_text, "+00000.001") != 0 ||
        v5_ui_status_view_rotary_phase_deg(360.0) != 0.0 ||
        v5_ui_status_view_rotary_phase_deg(-360.0) != 0.0 ||
        v5_ui_status_view_rotary_phase_deg(720.0) != 0.0 ||
        fabs(v5_ui_status_view_rotary_phase_deg(status.mcs[4] - 1.0) - 359.001) > 1.0e-9 ||
        fabs(v5_ui_status_view_rotary_phase_deg(359.999) - 359.999) > 1.0e-9) {
        return 12;
    }

    v5_toolpath_display_from_status(&status, V5_TOOLPATH_DISPLAY_XY, 200.0, 100.0, &display);
    if (!display.trajectory_valid || display.point_count != 3u) {
        return 6;
    }
    if (!display.mcs_valid || !display.cmd_valid) {
        return 7;
    }

    v5_toolpath_display_fit_init(&fit);
    if (!v5_toolpath_display_fit_from_status(&status, V5_TOOLPATH_DISPLAY_3D, &fit) ||
        !v5_toolpath_display_project_world_point(origin_world, &fit, 200.0, 100.0, &origin) ||
        !v5_toolpath_display_project_world_point(x_axis_world, &fit, 200.0, 100.0, &x_axis) ||
        !v5_toolpath_display_project_world_point(y_axis_world, &fit, 200.0, 100.0, &y_axis) ||
        !v5_toolpath_display_project_world_point(z_axis_world, &fit, 200.0, 100.0, &z_axis)) {
        return 8;
    }
    if (!(x_axis.x > origin.x && y_axis.x > origin.x && fabs(z_axis.x - origin.x) < 2.0)) {
        return 9;
    }
    if (!(x_axis.y > origin.y && y_axis.y < origin.y && z_axis.y < origin.y)) {
        return 10;
    }
    x_len = hypot(x_axis.x - origin.x, x_axis.y - origin.y);
    y_len = hypot(y_axis.x - origin.x, y_axis.y - origin.y);
    z_len = hypot(z_axis.x - origin.x, z_axis.y - origin.y);
    if (fabs(x_len - y_len) > 1.0e-9 || fabs(x_len - z_len) > 1.0e-9) {
        return 13;
    }
    fit.plane = V5_TOOLPATH_DISPLAY_XZ;
    if (!v5_toolpath_display_project_world_point(origin_world, &fit, 200.0, 100.0, &origin) ||
        !v5_toolpath_display_project_world_point(x_axis_world, &fit, 200.0, 100.0, &x_axis) ||
        !v5_toolpath_display_project_world_point(z_axis_world, &fit, 200.0, 100.0, &z_axis) ||
        fabs(hypot(x_axis.x - origin.x, x_axis.y - origin.y) -
             hypot(z_axis.x - origin.x, z_axis.y - origin.y)) > 1.0e-9) {
        return 14;
    }
    fit.plane = V5_TOOLPATH_DISPLAY_YZ;
    if (!v5_toolpath_display_project_world_point(origin_world, &fit, 200.0, 100.0, &origin) ||
        !v5_toolpath_display_project_world_point(y_axis_world, &fit, 200.0, 100.0, &y_axis) ||
        !v5_toolpath_display_project_world_point(z_axis_world, &fit, 200.0, 100.0, &z_axis) ||
        fabs(hypot(y_axis.x - origin.x, y_axis.y - origin.y) -
             hypot(z_axis.x - origin.x, z_axis.y - origin.y)) > 1.0e-9) {
        return 15;
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
