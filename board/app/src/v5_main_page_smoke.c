#include "v5_main_page.h"
#include "v5_motion_model_registry.h"
#include "v5_main_page_internal.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_headless.h"
#include "v5_settings_page.h"
#include "v5_settings_page_internal.h"
#include "v5_ui_status_view.h"
#include "v5_settings_axis_table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SettingsNavigationProbe {
    unsigned int calls;
    V5MainPageActionKind action;
} SettingsNavigationProbe;

static void settings_navigation_probe(void *user_data, V5MainPageActionKind action)
{
    SettingsNavigationProbe *probe = (SettingsNavigationProbe *)user_data;
    if (!probe) {
        return;
    }
    probe->calls += 1U;
    probe->action = action;
}


static long point_delta_abs(const lv_point_t *point, const V5ToolpathScreenPoint *expected)
{
    const long dx = (long)point->x - (long)(expected->x + (expected->x >= 0.0 ? 0.5 : -0.5));
    const long dy = (long)point->y - (long)(expected->y + (expected->y >= 0.0 ? 0.5 : -0.5));
    return labs(dx) + labs(dy);
}

static int project_expected(const V5MainPage *page, const double world[V5_STATUS_AXIS_COUNT], V5ToolpathScreenPoint *point)
{
    return v5_toolpath_display_project_world_point(world, &page->toolpath_fit, 388.0, 378.0, point);
}

static void ac_pose_expected(
    const double source[V5_STATUS_AXIS_COUNT],
    const double a_center[3],
    const double c_center[3],
    double a_deg,
    double c_deg,
    double expected[V5_STATUS_AXIS_COUNT])
{
    const double a_rad = a_deg * 3.14159265358979323846 / 180.0;
    const double c_rad = c_deg * 3.14159265358979323846 / 180.0;
    const double a_c = cos(a_rad);
    const double a_s = sin(a_rad);
    const double c_c = cos(c_rad);
    const double c_s = sin(c_rad);
    double dx;
    double dy;
    double dz;

    memcpy(expected, source, sizeof(double) * V5_STATUS_AXIS_COUNT);
    dx = expected[0] - c_center[0];
    dy = expected[1] - c_center[1];
    expected[0] = c_center[0] + (c_c * dx) - (c_s * dy);
    expected[1] = c_center[1] + (c_s * dx) + (c_c * dy);

    dy = expected[1] - a_center[1];
    dz = expected[2] - a_center[2];
    expected[1] = a_center[1] + (a_c * dy) - (a_s * dz);
    expected[2] = a_center[2] + (a_s * dy) + (a_c * dz);
}

static void bc_pose_expected(
    const double source[V5_STATUS_AXIS_COUNT],
    const double b_center[3],
    const double c_center[3],
    double b_deg,
    double c_deg,
    double expected[V5_STATUS_AXIS_COUNT])
{
    const double b_rad = b_deg * 3.14159265358979323846 / 180.0;
    const double c_rad = c_deg * 3.14159265358979323846 / 180.0;
    const double b_c = cos(b_rad);
    const double b_s = sin(b_rad);
    const double c_c = cos(c_rad);
    const double c_s = sin(c_rad);
    double dx;
    double dy;
    double dz;

    memcpy(expected, source, sizeof(double) * V5_STATUS_AXIS_COUNT);
    dx = expected[0] - c_center[0];
    dy = expected[1] - c_center[1];
    expected[0] = c_center[0] + (c_c * dx) - (c_s * dy);
    expected[1] = c_center[1] + (c_s * dx) + (c_c * dy);

    dx = expected[0] - b_center[0];
    dz = expected[2] - b_center[2];
    expected[0] = b_center[0] + (b_c * dx) + (b_s * dz);
    expected[2] = b_center[2] - (b_s * dx) + (b_c * dz);
}

static int wcs_axes_match_world(
    const V5MainPage *page,
    const double world[4][V5_STATUS_AXIS_COUNT])
{
    V5ToolpathScreenPoint expected[4];
    unsigned int i;

    if (!page || !world) {
        return 0;
    }
    for (i = 0U; i < 4U; ++i) {
        if (!project_expected(page, world[i], &expected[i])) {
            return 0;
        }
    }
    for (i = 0U; i < 3U; ++i) {
        if (point_delta_abs(&page->toolpath_wcs_axis_points[i][0], &expected[0]) > 4L ||
            point_delta_abs(&page->toolpath_wcs_axis_points[i][1], &expected[i + 1U]) > 4L) {
            return 0;
        }
    }
    return 1;
}

static long line_cross_abs(const lv_point_t a[2], const lv_point_t b[2])
{
    const long ax = (long)a[1].x - (long)a[0].x;
    const long ay = (long)a[1].y - (long)a[0].y;
    const long bx = (long)b[1].x - (long)b[0].x;
    const long by = (long)b[1].y - (long)b[0].y;
    const long cross = (ax * by) - (ay * bx);
    return labs(cross);
}

static long line_cross_vector_abs(const lv_point_t a[2], double bx, double by)
{
    const double ax = (double)a[1].x - (double)a[0].x;
    const double ay = (double)a[1].y - (double)a[0].y;
    const double cross = (ax * by) - (ay * bx);
    return labs((long)cross);
}

static long line_length_sq(const lv_point_t a[2])
{
    const long dx = (long)a[1].x - (long)a[0].x;
    const long dy = (long)a[1].y - (long)a[0].y;
    return (dx * dx) + (dy * dy);
}

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
    V5UiStatusView unavailable_status;
    V5ProgramController controller;
    V5ProgramOpenResult open_result;
    V5NativeReadback readback;
    V5NativeReadback settings_readback;
    V5SettingsPage settings_page;
    double wcs_offsets[V5_NATIVE_READBACK_WCS_OFFSET_COUNT] = {10.0, 12.0, 8.0, 0.0, 0.0};
    double g53_centers[V5_NATIVE_READBACK_G53_CENTER_COUNT][V5_NATIVE_READBACK_G53_AXIS_COUNT] = {
        {0.0, 20.0, -50.0},
        {0.0, 0.0, -25.0},
        {50.0, 20.0, 0.0},
    };
    lv_obj_t *screen;
    const char *mcs_text;
    const char *wcs_text;
    const char *modal_text;
    char mcs_text_copy[32];
    char modal_text_copy[160];
    unsigned int rewrite_count_after_program;
    unsigned int rewrite_count_after_gesture;
    unsigned int fit_generation_after_program;
    lv_point_t gesture_start[2];
    lv_point_t gesture_move[2];
    V5MainPageActionReport view_report;
    SettingsNavigationProbe settings_navigation = {0};
    int gesture_changed = 0;

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    v5_settings_axis_table_load_readback(".");
    screen = lv_scr_act();
    if (!screen) {
        return 1;
    }
    if (!v5_main_page_create(&page, screen)) {
        return 2;
    }
    if (!page.spindle_override_slider || !page.feed_override_slider ||
        !page.spindle_override_reset_hit || !page.feed_override_reset_hit ||
        lv_slider_get_min_value(page.spindle_override_slider) != 0 ||
        lv_slider_get_max_value(page.spindle_override_slider) != 200 ||
        lv_slider_get_value(page.spindle_override_slider) != 100 ||
        lv_slider_get_value(page.feed_override_slider) != 100) {
        return 70;
    }
    lv_slider_set_value(page.feed_override_slider, 135, LV_ANIM_OFF);
    lv_event_send(page.feed_override_slider, LV_EVENT_PRESSED, 0);
    if (page.last_action.action != V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET ||
        page.last_action.request.kind != V5_COMMAND_FEED_OVERRIDE_SET ||
        page.last_action.request.index_value != 135) {
        return 71;
    }
    lv_slider_set_value(page.feed_override_slider, 140, LV_ANIM_OFF);
    lv_event_send(page.feed_override_slider, LV_EVENT_VALUE_CHANGED, 0);
    if (page.last_action.request.index_value != 135) {
        return 75;
    }
    lv_event_send(page.feed_override_slider, LV_EVENT_RELEASED, 0);
    if (page.last_action.request.index_value != 140) {
        return 76;
    }
    lv_event_send(page.spindle_override_reset_hit, LV_EVENT_CLICKED, 0);
    if (page.last_action.action != V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_SET ||
        page.last_action.request.kind != V5_COMMAND_SPINDLE_OVERRIDE_SET ||
        page.last_action.request.index_value != 100) {
        return 72;
    }
    lv_event_send(page.feed_override_reset_hit, LV_EVENT_CLICKED, 0);
    if (page.last_action.action != V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET ||
        page.last_action.request.kind != V5_COMMAND_FEED_OVERRIDE_SET ||
        page.last_action.request.index_value != 100) {
        return 77;
    }
    if (lv_obj_get_style_line_width(page.trajectory_line, 0) != 1 ||
        lv_obj_get_style_line_width(page.toolpath_line_segments[1], 0) != 1) {
        return 33;
    }
    if (page.trajectory_point_count != 0u ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_origin_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_axis_lines[8][2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_labels[8], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_primary_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_child_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_primary_center_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_child_center_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_primary_label, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_child_label, LV_OBJ_FLAG_HIDDEN)) {
        return 19;
    }
    if (!lv_label_get_text(page.toolpath_summary_label) ||
        strcmp(lv_label_get_text(page.toolpath_summary_label), "") != 0 ||
        !lv_label_get_text(page.toolpath_detail_label) ||
        strcmp(lv_label_get_text(page.toolpath_detail_label), "") != 0) {
        return 35;
    }
    memset(&unavailable_status, 0, sizeof(unavailable_status));
    if (!v5_main_page_apply_status(&page, &unavailable_status) ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_mcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_wcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_origin_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_axis_lines[8][2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_primary_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_model_child_axis_line, LV_OBJ_FLAG_HIDDEN)) {
        return 20;
    }
    if (!lv_obj_has_state(page.spindle_override_slider, LV_STATE_DISABLED) ||
        !lv_obj_has_state(page.feed_override_slider, LV_STATE_DISABLED)) {
        return 73;
    }
    v5_program_controller_init(&controller);
    v5_native_readback_init(&readback);
    v5_native_readback_set_wcs_actual_offsets(&readback, 0, wcs_offsets, V5_NATIVE_READBACK_WCS_OFFSET_COUNT);
    v5_native_readback_set_g53_geometry(
        &readback,
        &g53_centers[0][0],
        V5_NATIVE_READBACK_G53_CENTER_COUNT,
        V5_NATIVE_READBACK_G53_AXIS_COUNT,
        23U);
    v5_native_readback_set_motion_model(&readback, "XYZAC_TRT");
    v5_native_readback_set_rtcp_actual(&readback, 0);
    v5_native_readback_set_modal_actual(&readback, "G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97");
    v5_native_readback_set_tool_actual(&readback, 7, 1, 123.456);
    v5_main_page_set_native_readback(&page, &readback);
    v5_main_page_bind_program_controller(&page, &controller);
    if (!build_status(&status)) {
        return 3;
    }
    if (!v5_settings_page_create(&settings_page, screen)) {
        return 60;
    }
    v5_settings_page_set_navigation_callback(
        &settings_page, settings_navigation_probe, &settings_navigation);
    if (!settings_page.save_return_button || !settings_page.save_return_label ||
        settings_page.restart_pending ||
        strcmp(lv_label_get_text(settings_page.save_return_label), "返回主页") != 0 ||
        settings_page.button_actions[V5_SETTINGS_PAGE_BUTTON_COUNT - 1U] != V5_MAIN_PAGE_ACTION_NAV_MAIN) {
        return 73;
    }
    lv_event_send(settings_page.save_return_button, LV_EVENT_CLICKED, 0);
    if (settings_navigation.calls != 1U ||
        settings_navigation.action != V5_MAIN_PAGE_ACTION_NAV_MAIN ||
        settings_page.last_action.action != V5_MAIN_PAGE_ACTION_NAV_MAIN ||
        !settings_page.last_action.prepared || !settings_page.last_action.local_only) {
        return 74;
    }
    v5_settings_page_parameter_changed_cb(&settings_page);
    if (!settings_page.restart_pending ||
        strcmp(lv_label_get_text(settings_page.save_return_label), "保存并重启") != 0 ||
        settings_page.button_actions[V5_SETTINGS_PAGE_BUTTON_COUNT - 1U] !=
            V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN) {
        return 75;
    }
    v5_settings_page_popup_show(
        &settings_page,
        "smoke_action",
        "smoke running",
        "正在处理",
        0,
        0);
    if (!settings_page.popup_active ||
        lv_obj_has_flag(settings_page.popup_overlay, LV_OBJ_FLAG_HIDDEN) ||
        !settings_page.popup_confirm ||
        !lv_obj_has_state(settings_page.popup_confirm, LV_STATE_DISABLED) ||
        !lv_obj_has_state(settings_page.popup_close, LV_STATE_DISABLED)) {
        return 70;
    }
    v5_settings_page_popup_show(
        &settings_page,
        "smoke_action",
        "smoke final",
        "动作完成",
        1,
        1);
    if (!settings_page.popup_final ||
        !lv_obj_has_state(settings_page.popup_confirm, LV_STATE_DISABLED) ||
        lv_obj_has_state(settings_page.popup_close, LV_STATE_DISABLED)) {
        return 71;
    }
    lv_event_send(settings_page.popup_close, LV_EVENT_RELEASED, 0);
    if (settings_page.popup_active ||
        !lv_obj_has_flag(settings_page.popup_overlay, LV_OBJ_FLAG_HIDDEN)) {
        return 72;
    }
    v5_settings_page_popup_show(
        &settings_page,
        "settings_save_and_restart",
        "settings restart scheduled",
        "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED",
        1,
        1);
    lv_event_send(settings_page.popup_close, LV_EVENT_RELEASED, 0);
    if (settings_page.popup_active ||
        v5_lvgl_remote_display_output_suppressed()) {
        return 76;
    }
    v5_settings_page_popup_show(
        &settings_page,
        "settings_save_and_restart",
        "settings restart scheduled",
        "SETTINGS_SAVE_RESTART_BOARD_REBOOT_SCHEDULED",
        1,
        1);
    settings_page.popup_restart_handoff_accepted = 1;
    snprintf(settings_page.popup_run_id, sizeof(settings_page.popup_run_id), "%s", "smoke-run");
    lv_event_send(settings_page.popup_close, LV_EVENT_RELEASED, 0);
    if (!settings_page.popup_active ||
        v5_lvgl_remote_display_output_suppressed() ||
        settings_page.popup_restart_handoff_accepted ||
        strstr(lv_label_get_text(settings_page.popup_message),
               "SETTINGS_SAVE_RESTART_COMMIT_REJECTED") == 0) {
        return 77;
    }
    lv_event_send(settings_page.popup_close, LV_EVENT_RELEASED, 0);
    if (settings_page.popup_active ||
        !v5_lvgl_remote_display_blackout_for_restart() ||
        !v5_lvgl_remote_display_output_suppressed()) {
        return 78;
    }
    (void)v5_lvgl_remote_display_set_output_suppressed(0);
    v5_native_readback_init(&settings_readback);
    v5_native_readback_set_motion_model(&settings_readback, "XYZBC_TRT");
    if (!v5_settings_page_set_native_readback(&settings_page, &settings_readback) ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[3]), "B") != 0 ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[4]), "C") != 0 ||
        settings_page.mcs_status_slots[3] != 3U || settings_page.mcs_status_slots[4] != 4U ||
        !v5_settings_page_apply_status(&settings_page, &status) ||
        strstr(lv_label_get_text(settings_page.mcs_labels[3]), "5.500") == 0) {
        return 61;
    }
    v5_native_readback_set_motion_model(&settings_readback, "UNREGISTERED_MODEL");
    if (v5_settings_page_set_native_readback(&settings_page, &settings_readback) ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[3]), "-") != 0 ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[4]), "-") != 0 ||
        !v5_settings_page_apply_status(&settings_page, &status) ||
        strcmp(lv_label_get_text(settings_page.mcs_labels[3]), "--") != 0) {
        return 62;
    }
    v5_native_readback_set_motion_model(&settings_readback, "XYZAC_TRT");
    if (!v5_settings_page_set_native_readback(&settings_page, &settings_readback) ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[3]), "A") != 0 ||
        strcmp(lv_label_get_text(settings_page.mcs_axis_labels[4]), "C") != 0) {
        return 63;
    }
    if (!v5_main_page_apply_status(&page, &status)) {
        return 4;
    }
    if (lv_obj_has_state(page.spindle_override_slider, LV_STATE_DISABLED) ||
        lv_obj_has_state(page.feed_override_slider, LV_STATE_DISABLED) ||
        lv_slider_get_value(page.spindle_override_slider) != 90 ||
        lv_slider_get_value(page.feed_override_slider) != 100) {
        return 74;
    }
    {
        const double a_rad = status.mcs[3] * 3.14159265358979323846 / 180.0;
        const double posed_c_y = 20.0 - (sin(a_rad) * 58.0);
        const double posed_c_z = -50.0 + (cos(a_rad) * 58.0);
        const double y_dx = (double)page.toolpath_mcs_axis_points[1][1].x - (double)page.toolpath_mcs_axis_points[1][0].x;
        const double y_dy = (double)page.toolpath_mcs_axis_points[1][1].y - (double)page.toolpath_mcs_axis_points[1][0].y;
        const double z_dx = (double)page.toolpath_mcs_axis_points[2][1].x - (double)page.toolpath_mcs_axis_points[2][0].x;
        const double z_dy = (double)page.toolpath_mcs_axis_points[2][1].y - (double)page.toolpath_mcs_axis_points[2][0].y;
        const double c_dx = (-sin(a_rad) * y_dx) + (cos(a_rad) * z_dx);
        const double c_dy = (-sin(a_rad) * y_dy) + (cos(a_rad) * z_dy);
        lv_point_t fixed_wcs_origin[5];
        memcpy(fixed_wcs_origin, page.toolpath_wcs_origin_points, sizeof(fixed_wcs_origin));
        double a_start_world[V5_STATUS_AXIS_COUNT] = {-30.0, 20.0, -50.0, 0.0, 0.0};
        double a_end_world[V5_STATUS_AXIS_COUNT] = {50.0, 20.0, -50.0, 0.0, 0.0};
        double c_start_world[V5_STATUS_AXIS_COUNT] = {50.0, posed_c_y + (sin(a_rad) * 40.0), posed_c_z - (cos(a_rad) * 40.0), 0.0, 0.0};
        double c_end_world[V5_STATUS_AXIS_COUNT] = {50.0, posed_c_y - (sin(a_rad) * 40.0), posed_c_z + (cos(a_rad) * 40.0), 0.0, 0.0};
        V5ToolpathScreenPoint a_start_expected;
        V5ToolpathScreenPoint a_end_expected;
        V5ToolpathScreenPoint c_start_expected;
        V5ToolpathScreenPoint c_end_expected;
        if (line_cross_abs(page.toolpath_wcs_axis_points[0], page.toolpath_mcs_axis_points[0]) > 250L ||
            line_cross_abs(page.toolpath_wcs_axis_points[1], page.toolpath_mcs_axis_points[1]) > 250L ||
            line_cross_abs(page.toolpath_wcs_axis_points[2], page.toolpath_mcs_axis_points[2]) > 250L ||
            line_cross_abs(page.toolpath_model_axis_points[0], page.toolpath_mcs_axis_points[0]) > 250L ||
            line_cross_vector_abs(page.toolpath_model_axis_points[1], c_dx, c_dy) > 250L ||
            line_cross_abs(page.toolpath_model_axis_points[1], page.toolpath_mcs_axis_points[2]) <= 2L) {
            return 22;
        }
        if (!project_expected(&page, a_start_world, &a_start_expected) ||
            !project_expected(&page, a_end_world, &a_end_expected) ||
            !project_expected(&page, c_start_world, &c_start_expected) ||
            !project_expected(&page, c_end_world, &c_end_expected)) {
            return 30;
        }
        if (point_delta_abs(&page.toolpath_model_axis_points[0][0], &a_start_expected) > 4L ||
            point_delta_abs(&page.toolpath_model_axis_points[0][1], &a_end_expected) > 4L ||
            point_delta_abs(&page.toolpath_model_axis_points[1][0], &c_start_expected) > 4L ||
            point_delta_abs(&page.toolpath_model_axis_points[1][1], &c_end_expected) > 4L) {
            return 31;
        }
        status.mcs[0] += 25.0;
        status.mcs[1] += 30.0;
        status.mcs[2] += 35.0;
        if (!v5_main_page_apply_status(&page, &status) || memcmp(fixed_wcs_origin, page.toolpath_wcs_origin_points, sizeof(fixed_wcs_origin)) != 0) {
            return 28;
        }
        status.mcs[0] -= 25.0;
        status.mcs[1] -= 30.0;
        status.mcs[2] -= 35.0;
        if (!v5_main_page_apply_status(&page, &status)) {
            return 29;
        }
    }

    mcs_text = page.coordinate_digits.value_valid[0][0] ? page.coordinate_digits.value_text[0][0] : "";
    wcs_text = page.coordinate_digits.value_valid[1][0] ? page.coordinate_digits.value_text[1][0] : "";
    modal_text = lv_label_get_text(page.modal_label);
    if (!mcs_text || strcmp(mcs_text, "+00002.500") != 0) {
        fprintf(stderr, "main_page_smoke mcs0 mismatch valid=%u text=%s\n", page.coordinate_digits.value_valid[0][0], mcs_text ? mcs_text : "(null)");
        return 5;
    }
    if (!wcs_text || strcmp(wcs_text, "-00007.500") != 0) {
        fprintf(stderr, "main_page_smoke wcs0 mismatch valid=%u text=%s\n", page.coordinate_digits.value_valid[1][0], wcs_text ? wcs_text : "(null)");
        return 6;
    }
    if (!modal_text || strcmp(modal_text, "当前模态\nT7\nL123.456\nRTCP OFF\nG0\nG17\nG21\nG40\nG49\nG54\nG64\nG80\nG90\nG94\nG97") != 0) {
        return 13;
    }
    if (!lv_label_get_text(page.toolpath_summary_label) ||
        strcmp(lv_label_get_text(page.toolpath_summary_label), "G53 A 10.00,20.00,-50.00  C 50.00,20.00,8.00") != 0 ||
        !lv_label_get_text(page.toolpath_detail_label) ||
        strcmp(lv_label_get_text(page.toolpath_detail_label), "G54偏置 X10.00 Y12.00 Z8.00 A0.00 C0.00") != 0) {
        return 36;
    }
    if (!lv_label_get_text(page.axis_labels[3]) || strcmp(lv_label_get_text(page.axis_labels[3]), "A") != 0 ||
        !lv_label_get_text(page.axis_labels[4]) || strcmp(lv_label_get_text(page.axis_labels[4]), "C") != 0 ||
        strcmp(page.coordinate_digits.value_text[0][3], "+00005.500") != 0 ||
        strcmp(page.coordinate_digits.value_text[0][4], "+00006.500") != 0 ||
        strcmp(page.coordinate_digits.value_text[1][3], "+00005.500") != 0 ||
        strcmp(page.coordinate_digits.value_text[1][4], "+00006.500") != 0) {
        return 14;
    }
    {
        char selected_wcs_text[24];
        snprintf(selected_wcs_text, sizeof(selected_wcs_text), "%s", page.coordinate_digits.value_text[1][4]);
        lv_refr_now(NULL);
        v5_lvgl_headless_reset_flush_count();
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_WCS, 'C')) {
            return 42;
        }
        lv_refr_now(NULL);
        if (v5_lvgl_headless_flush_count() == 0U) {
            return 45;
        }
        if (strcmp(page.coordinate_digits.value_text[1][4], selected_wcs_text) != 0) {
            fprintf(stderr, "main_page_smoke selected WCS text lost: before=%s after=%s\n",
                    selected_wcs_text, page.coordinate_digits.value_text[1][4]);
            return 43;
        }
        v5_lvgl_headless_reset_flush_count();
        v5_main_page_select_all_axes(&page);
        lv_refr_now(NULL);
        if (page.selection.space != V5_MAIN_PAGE_SELECT_MCS ||
            page.selection.axis != '*' || !page.selection.all_axes) {
            return 46;
        }
        if (strcmp(page.coordinate_digits.value_text[1][4], selected_wcs_text) != 0) {
            fprintf(stderr, "main_page_smoke deselected WCS text lost: before=%s after=%s\n",
                    selected_wcs_text, page.coordinate_digits.value_text[1][4]);
            return 44;
        }
    }
    snprintf(mcs_text_copy, sizeof(mcs_text_copy), "%s", mcs_text);
    snprintf(modal_text_copy, sizeof(modal_text_copy), "%s", modal_text);
    if (page.trajectory_point_count != 0u ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_origin_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_axis_lines[8][2], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_model_primary_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_model_child_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_model_primary_label, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_model_child_label, LV_OBJ_FLAG_HIDDEN)) {
        v5_program_controller_destroy(&controller);
        return 7;
    }
    if (!v5_main_page_open_program(&page, "gcode/golden/cc-ac.ngc", &open_result) || !open_result.ok) {
        v5_program_controller_destroy(&controller);
        return 8;
    }
    if (!v5_main_page_apply_status(&page, &status)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (page.trajectory_point_count < 80u ||
        lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN)) {
        v5_program_controller_destroy(&controller);
        return 10;
    }
    {
        V5ToolpathScreenPoint program_first_expected;
        const double ac_first_center[3] = {10.0, 20.0, -50.0};
        const double c_center[3] = {50.0, 20.0, 8.0};
        double nested_expected[V5_STATUS_AXIS_COUNT];
        lv_coord_t min_x = page.trajectory_points[0].x;
        lv_coord_t max_x = page.trajectory_points[0].x;
        lv_coord_t min_y = page.trajectory_points[0].y;
        lv_coord_t max_y = page.trajectory_points[0].y;
        unsigned int pi;
        for (pi = 1U; pi < page.trajectory_point_count; ++pi) {
            if (page.trajectory_points[pi].x < min_x) min_x = page.trajectory_points[pi].x;
            if (page.trajectory_points[pi].x > max_x) max_x = page.trajectory_points[pi].x;
            if (page.trajectory_points[pi].y < min_y) min_y = page.trajectory_points[pi].y;
            if (page.trajectory_points[pi].y > max_y) max_y = page.trajectory_points[pi].y;
        }
        ac_pose_expected(
            page.toolpath_program_points[0].axis,
            ac_first_center,
            c_center,
            status.mcs[3],
            status.mcs[4],
            nested_expected);
        if (fabs(page.toolpath_program_project_points[0].axis[0] - nested_expected[0]) > 0.000001 ||
            fabs(page.toolpath_program_project_points[0].axis[1] - nested_expected[1]) > 0.000001 ||
            fabs(page.toolpath_program_project_points[0].axis[2] - nested_expected[2]) > 0.000001) {
            v5_program_controller_destroy(&controller);
            return 64;
        }
        if (!page.toolpath_program_wcs_valid || page.toolpath_program_wcs_index != 0 ||
            fabs(page.toolpath_program_wcs_offset[0] - 10.0) > 0.0005 ||
            fabs(page.toolpath_program_wcs_offset[1] - 12.0) > 0.0005 ||
            fabs(page.toolpath_program_wcs_offset[2] - 8.0) > 0.0005 ||
            page.toolpath_fit.bounds.max_u < 40.0 ||
            page.toolpath_fit.bounds.min_v > -80.0 ||
            !project_expected(&page, page.toolpath_program_project_points[0].axis, &program_first_expected) ||
            point_delta_abs(&page.trajectory_points[0], &program_first_expected) > 4L ||
            (long)max_x - (long)min_x < 80L || (long)max_y - (long)min_y < 80L) {
            v5_program_controller_destroy(&controller);
            return 32;
        }
    }
    rewrite_count_after_program = page.toolpath_line_rewrite_count;
    fit_generation_after_program = page.toolpath_fit.generation;
    if (!v5_main_page_apply_status(&page, &status) ||
        page.toolpath_line_rewrite_count != rewrite_count_after_program ||
        page.toolpath_static_cache_hits == 0U) {
        v5_program_controller_destroy(&controller);
        return 12;
    }
    {
        const unsigned int rewrite_before = page.toolpath_line_rewrite_count;
        const unsigned int set_points_before = page.toolpath_line_set_points_count;
        const double expected_primary = page.toolpath_program_model_scene.primary_deg + 0.001;
        const double expected_child = page.toolpath_program_model_scene.child_deg - 0.001;
        status.mcs[3] = expected_primary;
        status.mcs[4] = expected_child;
        if (!v5_main_page_apply_status(&page, &status) ||
            fabs(page.toolpath_program_model_scene.primary_deg - expected_primary) > 0.0000001 ||
            fabs(page.toolpath_program_model_scene.child_deg - expected_child) > 0.0000001 ||
            page.toolpath_line_rewrite_count != rewrite_before ||
            page.toolpath_line_set_points_count != set_points_before ||
            page.toolpath_line_last_dirty_rect_count != 0U ||
            page.toolpath_line_last_dirty_pixels != 0U ||
            page.toolpath_line_last_dirty_max_pixels != 0U) {
            v5_program_controller_destroy(&controller);
            return 68;
        }
    }
    {
        lv_point_t initial_points[2] = {{10, 10}, {20, 20}};
        lv_point_t shifted_points[2] = {{10, 10}, {21, 20}};
        unsigned int test_segment;
        unsigned int set_points_after_show;
        for (test_segment = 0U; test_segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++test_segment) {
            if (lv_obj_has_flag(page.toolpath_line_segments[test_segment], LV_OBJ_FLAG_HIDDEN)) {
                break;
            }
        }
        if (test_segment >= V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS) {
            v5_program_controller_destroy(&controller);
            return 69;
        }
        page.toolpath_line_last_dirty_rect_count = 0U;
        page.toolpath_line_last_dirty_pixels = 0U;
        page.toolpath_line_last_dirty_max_pixels = 0U;
        if (!v5_main_page_internal_update_toolpath_program_segment(
                &page, test_segment, initial_points, 2U)) {
            v5_program_controller_destroy(&controller);
            return 70;
        }
        set_points_after_show = page.toolpath_line_set_points_count;
        page.toolpath_line_last_dirty_rect_count = 0U;
        page.toolpath_line_last_dirty_pixels = 0U;
        page.toolpath_line_last_dirty_max_pixels = 0U;
        if (v5_main_page_internal_update_toolpath_program_segment(
                &page, test_segment, initial_points, 2U) ||
            page.toolpath_line_set_points_count != set_points_after_show ||
            page.toolpath_line_last_dirty_rect_count != 0U) {
            v5_program_controller_destroy(&controller);
            return 71;
        }
        if (!v5_main_page_internal_update_toolpath_program_segment(
                &page, test_segment, shifted_points, 2U) ||
            page.toolpath_line_set_points_count != set_points_after_show ||
            page.toolpath_line_last_dirty_rect_count != 2U ||
            page.toolpath_line_last_dirty_pixels == 0U ||
            page.toolpath_line_last_dirty_max_pixels == 0U ||
            page.toolpath_line_last_dirty_max_pixels >= (uint64_t)(388U * 378U)) {
            v5_program_controller_destroy(&controller);
            return 72;
        }
        if (!v5_main_page_internal_update_toolpath_program_segment(
                &page, test_segment, 0, 0U)) {
            v5_program_controller_destroy(&controller);
            return 73;
        }
    }
    {
        const double base_wcs_world[4][V5_STATUS_AXIS_COUNT] = {
            {10.0, 12.0, 8.0, 0.0, 0.0},
            {50.0, 12.0, 8.0, 0.0, 0.0},
            {10.0, 52.0, 8.0, 0.0, 0.0},
            {10.0, 12.0, 48.0, 0.0, 0.0},
        };
        double posed_wcs_world[4][V5_STATUS_AXIS_COUNT];
        lv_point_t wcs_origin_before[5];
        lv_point_t wcs_axes_before[3][2];
        double projected_before[3];
        unsigned int rewrite_before = page.toolpath_line_rewrite_count;
        unsigned int fit_before = page.toolpath_fit.generation;
        unsigned int wi;
        memcpy(wcs_origin_before, page.toolpath_wcs_origin_points, sizeof(wcs_origin_before));
        memcpy(wcs_axes_before, page.toolpath_wcs_axis_points, sizeof(wcs_axes_before));
        projected_before[0] = page.toolpath_program_project_points[0].axis[0];
        projected_before[1] = page.toolpath_program_project_points[0].axis[1];
        projected_before[2] = page.toolpath_program_project_points[0].axis[2];
        status.mcs[3] += 20.0;
        status.mcs[4] += 35.0;
        if (!v5_main_page_apply_status(&page, &status) ||
            page.toolpath_line_rewrite_count <= rewrite_before ||
            page.toolpath_fit.generation > fit_before + 1U ||
            !wcs_axes_match_world(&page, base_wcs_world) ||
            (fabs(page.toolpath_program_project_points[0].axis[0] - projected_before[0]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[1] - projected_before[1]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[2] - projected_before[2]) < 0.0005)) {
            v5_program_controller_destroy(&controller);
            return 37;
        }
        {
            unsigned int stable_fit_generation = page.toolpath_fit.generation;
            unsigned int stable_rewrite_count = page.toolpath_line_rewrite_count;
            if (!v5_main_page_apply_status(&page, &status) ||
                page.toolpath_fit.generation != stable_fit_generation ||
                page.toolpath_line_rewrite_count != stable_rewrite_count) {
                v5_program_controller_destroy(&controller);
                return 66;
            }
        }

        rewrite_before = page.toolpath_line_rewrite_count;
        fit_before = page.toolpath_fit.generation;
        memcpy(wcs_origin_before, page.toolpath_wcs_origin_points, sizeof(wcs_origin_before));
        memcpy(wcs_axes_before, page.toolpath_wcs_axis_points, sizeof(wcs_axes_before));
        projected_before[0] = page.toolpath_program_project_points[0].axis[0];
        projected_before[1] = page.toolpath_program_project_points[0].axis[1];
        projected_before[2] = page.toolpath_program_project_points[0].axis[2];
        v5_native_readback_set_rtcp_actual(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        status.mcs[3] += 15.0;
        status.mcs[4] -= 20.0;
        for (wi = 0U; wi < 4U; ++wi) {
            const double first_center[3] = {10.0, 20.0, -50.0};
            const double second_center[3] = {50.0, 20.0, 8.0};
            ac_pose_expected(
                base_wcs_world[wi],
                first_center,
                second_center,
                status.mcs[3],
                status.mcs[4],
                posed_wcs_world[wi]);
        }
        if (!v5_main_page_apply_status(&page, &status) ||
            page.toolpath_line_rewrite_count <= rewrite_before ||
            page.toolpath_fit.generation > fit_before + 1U ||
            !wcs_axes_match_world(&page, posed_wcs_world) ||
            (fabs(page.toolpath_program_project_points[0].axis[0] - projected_before[0]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[1] - projected_before[1]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[2] - projected_before[2]) < 0.0005)) {
            v5_program_controller_destroy(&controller);
            return 38;
        }
        {
            unsigned int stable_fit_generation = page.toolpath_fit.generation;
            unsigned int stable_rewrite_count = page.toolpath_line_rewrite_count;
            if (!v5_main_page_apply_status(&page, &status) ||
                page.toolpath_fit.generation != stable_fit_generation ||
                page.toolpath_line_rewrite_count != stable_rewrite_count) {
                v5_program_controller_destroy(&controller);
                return 67;
            }
        }

        v5_native_readback_set_motion_model(&readback, "XYZBC_TRT");
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_apply_status(&page, &status) || !page.toolpath_program_model_scene_valid ||
            !page.toolpath_model_scene_valid ||
            !page.toolpath_model_scene_fresh ||
            page.toolpath_model_scene.registry_id != V5_MOTION_MODEL_ID_XYZBC_TRT ||
            strcmp(lv_label_get_text(page.axis_labels[3]), "B") != 0 ||
            strcmp(lv_label_get_text(page.toolpath_model_primary_label), "B") != 0 ||
            strcmp(lv_label_get_text(page.toolpath_model_child_label), "C") != 0 ||
            lv_obj_has_flag(page.toolpath_model_primary_axis_line, LV_OBJ_FLAG_HIDDEN) ||
            lv_obj_has_flag(page.toolpath_model_child_axis_line, LV_OBJ_FLAG_HIDDEN)) {
            v5_program_controller_destroy(&controller);
            return 39;
        }
        {
            const double b_rad = status.mcs[3] * 3.14159265358979323846 / 180.0;
            const double x_dx =
                (double)page.toolpath_mcs_axis_points[0][1].x -
                (double)page.toolpath_mcs_axis_points[0][0].x;
            const double x_dy =
                (double)page.toolpath_mcs_axis_points[0][1].y -
                (double)page.toolpath_mcs_axis_points[0][0].y;
            const double z_dx =
                (double)page.toolpath_mcs_axis_points[2][1].x -
                (double)page.toolpath_mcs_axis_points[2][0].x;
            const double z_dy =
                (double)page.toolpath_mcs_axis_points[2][1].y -
                (double)page.toolpath_mcs_axis_points[2][0].y;
            const double c_dx = (sin(b_rad) * x_dx) + (cos(b_rad) * z_dx);
            const double c_dy = (sin(b_rad) * x_dy) + (cos(b_rad) * z_dy);
            if (line_cross_abs(
                    page.toolpath_model_axis_points[0],
                    page.toolpath_mcs_axis_points[1]) > 250L ||
                line_cross_vector_abs(
                    page.toolpath_model_axis_points[1],
                    c_dx,
                    c_dy) > 250L) {
                v5_program_controller_destroy(&controller);
                return 75;
            }
        }
        {
            const double bc_first_center[3] = {0.0, 12.0, -25.0};
            const double c_center[3] = {50.0, 20.0, 8.0};
            double nested_expected[V5_STATUS_AXIS_COUNT];
            bc_pose_expected(
                page.toolpath_program_points[0].axis,
                bc_first_center,
                c_center,
                status.mcs[3],
                status.mcs[4],
                nested_expected);
            if (fabs(page.toolpath_program_project_points[0].axis[0] - nested_expected[0]) > 0.000001 ||
                fabs(page.toolpath_program_project_points[0].axis[1] - nested_expected[1]) > 0.000001 ||
                fabs(page.toolpath_program_project_points[0].axis[2] - nested_expected[2]) > 0.000001) {
                v5_program_controller_destroy(&controller);
                return 65;
            }
        }
        memcpy(wcs_origin_before, page.toolpath_wcs_origin_points, sizeof(wcs_origin_before));
        memcpy(wcs_axes_before, page.toolpath_wcs_axis_points, sizeof(wcs_axes_before));
        projected_before[0] = page.toolpath_program_project_points[0].axis[0];
        projected_before[1] = page.toolpath_program_project_points[0].axis[1];
        projected_before[2] = page.toolpath_program_project_points[0].axis[2];
        status.mcs[3] += 10.0;
        status.mcs[4] += 10.0;
        if (!v5_main_page_apply_status(&page, &status) ||
            !page.toolpath_program_model_scene_valid ||
            (memcmp(wcs_origin_before, page.toolpath_wcs_origin_points, sizeof(wcs_origin_before)) == 0 &&
             memcmp(wcs_axes_before, page.toolpath_wcs_axis_points, sizeof(wcs_axes_before)) == 0) ||
            (fabs(page.toolpath_program_project_points[0].axis[0] - projected_before[0]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[1] - projected_before[1]) < 0.0005 &&
             fabs(page.toolpath_program_project_points[0].axis[2] - projected_before[2]) < 0.0005)) {
            v5_program_controller_destroy(&controller);
            return 40;
        }
        v5_native_readback_set_motion_model(&readback, "XYZAC_TRT");
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_apply_status(&page, &status) || !page.toolpath_program_model_scene_valid ||
            strcmp(lv_label_get_text(page.axis_labels[3]), "A") != 0) {
            v5_program_controller_destroy(&controller);
            return 41;
        }
        {
            const V5MainPageModelScene last_good_ac_scene = page.toolpath_model_scene;
            V5NativeReadback transient_readback = readback;

            v5_native_readback_set_g53_geometry(&transient_readback, 0, 0U, 0U, 0U);
            v5_main_page_set_native_readback(&page, &transient_readback);
            if (!page.toolpath_model_scene_valid ||
                page.toolpath_model_scene_fresh ||
                !v5_main_page_model_scene_pose_equal(
                    &page.toolpath_model_scene,
                    &last_good_ac_scene,
                    0.0) ||
                !page.toolpath_program_visible) {
                v5_program_controller_destroy(&controller);
                return 76;
            }

            v5_native_readback_set_motion_model(&transient_readback, "XYZ_UNKNOWN");
            v5_main_page_set_native_readback(&page, &transient_readback);
            if (page.toolpath_model_scene_valid ||
                page.toolpath_model_scene_fresh ||
                page.toolpath_program_visible ||
                !lv_obj_has_flag(
                    page.toolpath_model_primary_axis_line,
                    LV_OBJ_FLAG_HIDDEN) ||
                !lv_obj_has_flag(
                    page.toolpath_model_child_axis_line,
                    LV_OBJ_FLAG_HIDDEN) ||
                lv_label_get_text(page.toolpath_summary_label)[0] != '\0' ||
                lv_label_get_text(page.toolpath_detail_label)[0] != '\0') {
                v5_program_controller_destroy(&controller);
                return 77;
            }

            v5_main_page_set_native_readback(&page, &readback);
            if (!v5_main_page_apply_status(&page, &status) ||
                !page.toolpath_model_scene_valid ||
                !page.toolpath_model_scene_fresh ||
                !page.toolpath_program_visible) {
                v5_program_controller_destroy(&controller);
                return 78;
            }
        }
        fit_generation_after_program = page.toolpath_fit.generation;
    }
    gesture_start[0].x = 160;
    gesture_start[0].y = 180;
    gesture_start[1].x = 260;
    gesture_start[1].y = 180;
    gesture_move[0].x = 145;
    gesture_move[0].y = 190;
    gesture_move[1].x = 285;
    gesture_move[1].y = 190;
    if (!v5_main_page_handle_touch_points(&page, gesture_start, 2, 1, &gesture_changed) || gesture_changed) {
        v5_program_controller_destroy(&controller);
        return 15;
    }
    if (!v5_main_page_handle_touch_points(&page, gesture_move, 2, 1, &gesture_changed) ||
        !gesture_changed || page.toolpath_manual_scale <= 1.0 || page.toolpath_view_generation == 0U) {
        v5_program_controller_destroy(&controller);
        return 16;
    }
    rewrite_count_after_gesture = page.toolpath_line_rewrite_count;
    if (!v5_main_page_apply_status(&page, &status) ||
        page.toolpath_program_view_generation != page.toolpath_view_generation ||
        page.toolpath_fit.generation != fit_generation_after_program ||
        page.toolpath_line_rewrite_count <= rewrite_count_after_gesture ||
        lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN)) {
        v5_program_controller_destroy(&controller);
        return 17;
    }
    page.toolpath_manual_rotate_deg = 31.0;
    page.toolpath_gesture_active_count = 2;
    memset(&view_report, 0, sizeof(view_report));
    if (!v5_main_page_trigger_action(&page, V5_MAIN_PAGE_ACTION_VIEW_XY, &view_report) ||
        page.view_plane != V5_TOOLPATH_DISPLAY_XY ||
        fabs(page.toolpath_manual_rotate_deg) > 1.0e-9 ||
        page.toolpath_gesture_active_count != 0) {
        v5_program_controller_destroy(&controller);
        return 34;
    }
    if (v5_main_page_handle_touch_points(&page, 0, 0, 0, &gesture_changed)) {
        v5_program_controller_destroy(&controller);
        return 18;
    }
    status.valid_mask &= ~V5_STATUS_VALID_CMD_MCS;
    if (!v5_main_page_apply_status(&page, &status) ||
        !lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN)) {
        v5_program_controller_destroy(&controller);
        return 11;
    }
    {
        V5ToolpathDisplayFit fit_before = page.toolpath_fit;
        unsigned int expanded_generation;
        status.mcs[0] += 10000.0;
        if (!v5_main_page_apply_status(&page, &status) ||
            page.toolpath_fit.generation != fit_before.generation + 1U ||
            page.toolpath_fit.bounds.min_u > fit_before.bounds.min_u ||
            page.toolpath_fit.bounds.min_v > fit_before.bounds.min_v ||
            page.toolpath_fit.bounds.max_v < fit_before.bounds.max_v ||
            page.toolpath_fit.bounds.max_u <= fit_before.bounds.max_u) {
            v5_program_controller_destroy(&controller);
            return 68;
        }
        expanded_generation = page.toolpath_fit.generation;
        if (!v5_main_page_apply_status(&page, &status) ||
            page.toolpath_fit.generation != expanded_generation) {
            v5_program_controller_destroy(&controller);
            return 69;
        }
    }

    printf(
        "v5 main page controls: mcs=%s modal=%s points=%u feed=%s\n",
        mcs_text_copy,
        modal_text_copy,
        page.trajectory_point_count,
        lv_label_get_text(page.feed_override_label));
    v5_program_controller_destroy(&controller);
    return 0;
}
