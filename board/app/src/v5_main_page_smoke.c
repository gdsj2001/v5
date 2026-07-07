#include "v5_main_page.h"
#include "v5_lvgl_headless.h"
#include "v5_ui_status_view.h"
#include "v5_settings_axis_table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
    if (lv_obj_get_style_line_width(page.trajectory_line, 0) != 2 ||
        lv_obj_get_style_line_width(page.toolpath_line_segments[1], 0) != 2) {
        return 33;
    }
    if (page.trajectory_point_count != 0u ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[1], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_origin_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_axis_lines[8][2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_labels[8], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_a_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_c_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_a_center_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_c_center_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_a_label, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_c_label, LV_OBJ_FLAG_HIDDEN)) {
        return 19;
    }
    if (!lv_label_get_text(page.toolpath_summary_label) ||
        strcmp(lv_label_get_text(page.toolpath_summary_label), "") != 0 ||
        !lv_label_get_text(page.toolpath_detail_label) ||
        strcmp(lv_label_get_text(page.toolpath_detail_label), "") != 0) {
        return 35;
    }
    if (line_cross_abs(page.toolpath_wcs_axis_points[0], page.toolpath_mcs_axis_points[0]) > 250L ||
        line_cross_abs(page.toolpath_wcs_axis_points[1], page.toolpath_mcs_axis_points[1]) > 250L ||
        line_cross_abs(page.toolpath_wcs_axis_points[2], page.toolpath_mcs_axis_points[2]) > 250L ||
        page.toolpath_mcs_axis_points[0][1].x <= page.toolpath_mcs_axis_points[0][0].x ||
        page.toolpath_mcs_axis_points[0][1].y <= page.toolpath_mcs_axis_points[0][0].y ||
        page.toolpath_mcs_axis_points[1][1].x <= page.toolpath_mcs_axis_points[1][0].x ||
        page.toolpath_mcs_axis_points[1][1].y >= page.toolpath_mcs_axis_points[1][0].y ||
        page.toolpath_mcs_axis_points[2][1].y >= page.toolpath_mcs_axis_points[2][0].y ||
        line_length_sq(page.toolpath_mcs_axis_points[0]) > 2200L ||
        line_length_sq(page.toolpath_mcs_axis_points[1]) > 2200L ||
        line_length_sq(page.toolpath_mcs_axis_points[2]) > 2200L) {
        return 21;
    }
    memset(&unavailable_status, 0, sizeof(unavailable_status));
    if (!v5_main_page_apply_status(&page, &unavailable_status) ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_holder_marker_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_microkernel_marker_dot, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_origin_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_mcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_wcs_axis_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_origin_lines[0], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_program_wcs_axis_lines[8][2], LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_a_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(page.toolpath_c_axis_line, LV_OBJ_FLAG_HIDDEN)) {
        return 20;
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
    v5_native_readback_set_modal_actual(&readback, "G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97");
    v5_native_readback_set_tool_actual(&readback, 7, 1, 123.456);
    v5_main_page_set_native_readback(&page, &readback);
    v5_main_page_bind_program_controller(&page, &controller);
    if (!build_status(&status)) {
        return 3;
    }
    if (!v5_main_page_apply_status(&page, &status)) {
        return 4;
    }
    {
        const double a_rad = status.mcs[3] * 3.14159265358979323846 / 180.0;
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
        double c_start_world[V5_STATUS_AXIS_COUNT] = {50.0, 20.0 + (sin(a_rad) * 40.0), 8.0 - (cos(a_rad) * 40.0), 0.0, 0.0};
        double c_end_world[V5_STATUS_AXIS_COUNT] = {50.0, 20.0 - (sin(a_rad) * 40.0), 8.0 + (cos(a_rad) * 40.0), 0.0, 0.0};
        V5ToolpathScreenPoint a_start_expected;
        V5ToolpathScreenPoint a_end_expected;
        V5ToolpathScreenPoint c_start_expected;
        V5ToolpathScreenPoint c_end_expected;
        if (line_cross_abs(page.toolpath_wcs_axis_points[0], page.toolpath_mcs_axis_points[0]) > 250L ||
            line_cross_abs(page.toolpath_wcs_axis_points[1], page.toolpath_mcs_axis_points[1]) > 250L ||
            line_cross_abs(page.toolpath_wcs_axis_points[2], page.toolpath_mcs_axis_points[2]) > 250L ||
            line_cross_abs(page.toolpath_ac_axis_points[0], page.toolpath_mcs_axis_points[0]) > 250L ||
            line_cross_vector_abs(page.toolpath_ac_axis_points[1], c_dx, c_dy) > 250L ||
            line_cross_abs(page.toolpath_ac_axis_points[1], page.toolpath_mcs_axis_points[2]) <= 2L) {
            return 22;
        }
        if (!project_expected(&page, a_start_world, &a_start_expected) ||
            !project_expected(&page, a_end_world, &a_end_expected) ||
            !project_expected(&page, c_start_world, &c_start_expected) ||
            !project_expected(&page, c_end_world, &c_end_expected)) {
            return 30;
        }
        if (point_delta_abs(&page.toolpath_ac_axis_points[0][0], &a_start_expected) > 4L ||
            point_delta_abs(&page.toolpath_ac_axis_points[0][1], &a_end_expected) > 4L ||
            point_delta_abs(&page.toolpath_ac_axis_points[1][0], &c_start_expected) > 4L ||
            point_delta_abs(&page.toolpath_ac_axis_points[1][1], &c_end_expected) > 4L) {
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

    mcs_text = lv_label_get_text(page.mcs_labels[0]);
    wcs_text = lv_label_get_text(page.cmd_labels[0]);
    modal_text = lv_label_get_text(page.modal_label);
    if (!mcs_text || strcmp(mcs_text, "+00002.500") != 0) {
        return 5;
    }
    if (!wcs_text || strcmp(wcs_text, "-00007.500") != 0) {
        return 6;
    }
    if (!modal_text || strcmp(modal_text, "当前模态\nT7\nL123.456\nG0\nG17\nG21\nG40\nG49\nG54\nG64\nG80\nG90\nG94\nG97") != 0) {
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
        strcmp(lv_label_get_text(page.mcs_labels[3]), "+00005.500") != 0 ||
        strcmp(lv_label_get_text(page.mcs_labels[4]), "+00006.500") != 0 ||
        strcmp(lv_label_get_text(page.cmd_labels[3]), "+00005.500") != 0 ||
        strcmp(lv_label_get_text(page.cmd_labels[4]), "+00006.500") != 0) {
        return 14;
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
        lv_obj_has_flag(page.toolpath_a_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_c_axis_line, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_a_label, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(page.toolpath_c_label, LV_OBJ_FLAG_HIDDEN)) {
        v5_program_controller_destroy(&controller);
        return 7;
    }
    if (!v5_main_page_open_program(&page, "gcode/golden/cc.ngc", &open_result) || !open_result.ok) {
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
        const double program_first_world[V5_STATUS_AXIS_COUNT] = {10.0, 22.0, 8.0, 45.0, 0.0};
        V5ToolpathScreenPoint program_first_expected;
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
        if (!page.toolpath_program_wcs_valid || page.toolpath_program_wcs_index != 0 ||
            fabs(page.toolpath_program_wcs_offset[0] - 10.0) > 0.0005 ||
            fabs(page.toolpath_program_wcs_offset[1] - 12.0) > 0.0005 ||
            fabs(page.toolpath_program_wcs_offset[2] - 8.0) > 0.0005 ||
            page.toolpath_fit.bounds.max_u < 40.0 ||
            page.toolpath_fit.bounds.min_v > -80.0 ||
            !project_expected(&page, program_first_world, &program_first_expected) ||
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

    printf(
        "v5 main page controls: mcs=%s modal=%s points=%u feed=%s\n",
        mcs_text_copy,
        modal_text_copy,
        page.trajectory_point_count,
        lv_label_get_text(page.feed_override_label));
    v5_program_controller_destroy(&controller);
    return 0;
}
