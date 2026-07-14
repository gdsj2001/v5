#include "v5_main_page.h"

#include "v5_command_gate_ipc.h"
#include "v5_button_visuals.h"
#include "v5_native_wcs_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_layout_icons.h"
#include "v5_lvgl_clock.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "v5_main_page_internal.h"

void v5_main_page_init(V5MainPage *page)
{
    if (!page) {
        return;
    }
    memset(page, 0, sizeof(*page));
}

void v5_main_page_internal_update_estop_button_text(V5MainPage *page);
void v5_main_page_internal_update_main_page_state_button_visuals(V5MainPage *page);
void v5_main_page_internal_update_axis_all_button_visuals(V5MainPage *page);

int v5_main_page_create(V5MainPage *page, lv_obj_t *parent)
{
    unsigned int i;
    static const char *axis_text[V5_MAIN_PAGE_AXIS_COUNT] = {"X", "Y", "Z", "A", "C"};

    if (!page || !parent) {
        return 0;
    }
    v5_main_page_init(page);

    page->view_plane = V5_TOOLPATH_DISPLAY_3D;
    page->toolpath_program_plane = page->view_plane;
    page->toolpath_manual_scale = 1.0;
    v5_toolpath_display_fit_init(&page->toolpath_fit);
    page->jog_step = 0.001;
    v5_main_page_select_all_axes(page);
    v5_native_readback_set_unavailable(&page->native_readback, "native_readback_unavailable");

    page->root = lv_obj_create(parent);
    lv_obj_add_event_cb(page->root, v5_main_page_internal_main_page_root_delete_event_cb, LV_EVENT_DELETE, page);
    v5_main_page_internal_clear_obj_style(page->root);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 1024, 600);
    lv_obj_set_style_bg_color(page->root, v5_main_page_internal_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);

    v5_main_page_internal_make_panel(page->root, 0, 0, 920, 55, 4, 24, 36);
    v5_main_page_internal_make_label_ex(page->root, 28, 25, 120, 24, "精密数控", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    v5_main_page_internal_make_divider(page->root, 0, 55, 920, 1);
    v5_main_page_internal_make_divider(page->root, 394, 55, 1, 386);
    v5_main_page_internal_make_divider(page->root, 397, 278, 348, 1);
    v5_main_page_internal_make_divider(page->root, 0, 441, 745, 1);
    v5_main_page_internal_make_divider(page->root, 560, 441, 1, 154);
    v5_main_page_internal_make_divider(page->root, 745, 55, 1, 540);
    v5_main_page_internal_make_divider(page->root, 920, 0, 1, 600);

    v5_main_page_internal_make_panel(page->root, 0, 55, 394, 386, 4, 24, 36);
    page->modal_label = v5_main_page_internal_make_label_ex(page->root, 12, 68, 150, 300, "", 0, 142, 146, LV_TEXT_ALIGN_LEFT);
    page->toolpath_clip_layer = lv_obj_create(page->root);
    v5_main_page_internal_clear_obj_style(page->toolpath_clip_layer);
    lv_obj_set_pos(page->toolpath_clip_layer, V5_TOOLPATH_X, V5_TOOLPATH_Y);
    lv_obj_set_size(page->toolpath_clip_layer, V5_TOOLPATH_W, V5_TOOLPATH_H);
    lv_obj_set_style_bg_opa(page->toolpath_clip_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page->toolpath_clip_layer, LV_OBJ_FLAG_CLICKABLE);
    page->toolpath_mcs_origin_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 210, 235, 255, 2);
    page->toolpath_mcs_axis_lines[0] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 255, 150, 156, 1);
    page->toolpath_mcs_axis_lines[1] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 120, 255, 190, 1);
    page->toolpath_mcs_axis_lines[2] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 180, 226, 255, 1);
    page->toolpath_a_axis_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 255, 113, 118, 1);
    page->toolpath_c_axis_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 120, 240, 255, 1);
    page->toolpath_wcs_origin_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 68, 221, 144, 1);
    page->toolpath_wcs_axis_lines[0] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 255, 100, 106, 1);
    page->toolpath_wcs_axis_lines[1] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 0, 232, 150, 1);
    page->toolpath_wcs_axis_lines[2] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 86, 204, 252, 1);
    for (i = 0U; i < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++i) {
        page->toolpath_program_wcs_origin_lines[i] = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 68, 221, 144, 1);
        for (unsigned int axis = 0U; axis < 3U; ++axis) {
            static const uint8_t axis_colors[3][3] = {{255, 100, 106}, {0, 232, 150}, {86, 204, 252}};
            page->toolpath_program_wcs_axis_lines[i][axis] = v5_main_page_internal_make_toolpath_v3_line(
                page->toolpath_clip_layer,
                axis_colors[axis][0],
                axis_colors[axis][1],
                axis_colors[axis][2],
                1);
        }
    }
    page->trajectory_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 255, 214, 64, V5_TOOLPATH_PROGRAM_LINE_WIDTH);
    page->toolpath_line_segments[0] = page->trajectory_line;
    for (i = 1U; i < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++i) {
        page->toolpath_line_segments[i] = v5_main_page_internal_make_toolpath_v3_line(
            page->toolpath_clip_layer,
            255,
            214,
            64,
            V5_TOOLPATH_PROGRAM_LINE_WIDTH);
    }
    page->toolpath_holder_line = v5_main_page_internal_make_toolpath_v3_line(page->toolpath_clip_layer, 96, 176, 255, 5);
    page->toolpath_summary_label = v5_main_page_internal_make_label_ex(page->root, 12, 401, 374, 18, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    page->toolpath_detail_label = v5_main_page_internal_make_label_ex(page->root, 12, 420, 374, 18, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_view_label = v5_main_page_internal_make_label_ex(page->root, 18, 358, 260, 24, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_a_label = v5_main_page_internal_make_label_ex(page->root, 132, 162, 24, 22, "A", 255, 113, 118, LV_TEXT_ALIGN_CENTER);
    page->toolpath_c_label = v5_main_page_internal_make_label_ex(page->root, 160, 162, 20, 22, "C", 120, 240, 255, LV_TEXT_ALIGN_CENTER);
    page->toolpath_wcs_label = v5_main_page_internal_make_label_ex(page->root, 18, 326, 42, 22, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    page->toolpath_mcs_label = v5_main_page_internal_make_label_ex(page->root, 10, 62, 42, 22, "", 96, 176, 255, LV_TEXT_ALIGN_LEFT);
    for (i = 0U; i < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++i) {
        page->toolpath_program_wcs_labels[i] = v5_main_page_internal_make_label_ex(page->root, 18, 326, 42, 22, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    }
    for (i = 0U; i < 3U; ++i) {
        page->toolpath_mcs_axis_labels[i] = v5_main_page_internal_make_label_ex(page->root, 10, 62 + (int)i * 18, 32, 18, "", 180, 226, 255, LV_TEXT_ALIGN_LEFT);
    }
    lv_obj_add_flag(page->toolpath_a_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->toolpath_c_label, LV_OBJ_FLAG_HIDDEN);
    page->toolpath_microkernel_marker_dot = v5_main_page_internal_make_toolpath_v3_dot(page->root, 255, 64, 64, 255, 230, 230);
    page->toolpath_holder_marker_line = v5_main_page_internal_make_toolpath_v3_dot(page->root, 68, 221, 144, 220, 255, 235);
    page->toolpath_a_center_line = v5_main_page_internal_make_toolpath_v3_center_dot(page->root, 255, 113, 118);
    page->toolpath_c_center_line = v5_main_page_internal_make_toolpath_v3_center_dot(page->root, 120, 240, 255);
    v5_main_page_internal_hide_toolpath_unproven_geometry(page);

    v5_main_page_internal_make_panel(page->root, 397, 55, 348, 230, 6, 26, 39);
    v5_main_page_internal_make_label_ex(page->root, 410, 79, 32, 24, "轴", 156, 178, 202, LV_TEXT_ALIGN_CENTER);
    v5_main_page_internal_make_label_ex(page->root, 449, 79, 124, 24, "机械坐标", 88, 204, 255, LV_TEXT_ALIGN_CENTER);
    v5_main_page_internal_make_panel(page->root, 579, 73, 124, 28, 25, 72, 62);
    page->wcs_header_label = v5_main_page_internal_make_label_ex(page->root, 579, 77, 124, 22, "加工 --", 68, 221, 144, LV_TEXT_ALIGN_CENTER);
    v5_coordinate_digits_create_main(&page->coordinate_digits, page->root, page->coordinate_digits_buffer);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        int y = 113 + (int)i * 32;
        page->axis_labels[i] = v5_main_page_internal_make_label_ex(page->root, 414, 115 + (int)i * 32, 32, 24, axis_text[i], 218, 232, 242, LV_TEXT_ALIGN_CENTER);
        page->mcs_targets[i].space = V5_MAIN_PAGE_SELECT_MCS;
        page->mcs_targets[i].axis = axis_text[i][0];
        page->wcs_targets[i].space = V5_MAIN_PAGE_SELECT_WCS;
        page->wcs_targets[i].axis = axis_text[i][0];
        page->mcs_labels[i] = v5_main_page_internal_make_label_ex(page->root, 449, y, 124, 30, "000.000", 88, 204, 255, LV_TEXT_ALIGN_RIGHT);
        page->cmd_labels[i] = v5_main_page_internal_make_label_ex(page->root, 579, y, 124, 30, "000.000", 68, 221, 144, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_style_text_opa(page->mcs_labels[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_opa(page->cmd_labels[i], LV_OPA_TRANSP, 0);
        v5_main_page_internal_make_coordinate_value_clickable(page, page->mcs_labels[i]);
        v5_main_page_internal_make_coordinate_value_clickable(page, page->cmd_labels[i]);
        lv_obj_move_foreground(page->mcs_labels[i]);
        lv_obj_move_foreground(page->cmd_labels[i]);
    }
    v5_main_page_internal_update_coordinate_target_axes(page);
    v5_main_page_internal_update_coordinate_selection_style(page);

    v5_main_page_internal_make_panel(page->root, 746, 56, 174, 276, 5, 24, 39);
    v5_main_page_internal_make_label_ex(page->root, 762, 64, 82, 24, "主轴转速", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_main_page_internal_make_label_ex(page->root, 872, 64, 34, 22, "rpm", 28, 193, 238, LV_TEXT_ALIGN_LEFT);
    page->spindle_speed_label = v5_main_page_internal_make_label_ex(page->root, 762, 88, 142, 30, "--", 28, 193, 238, LV_TEXT_ALIGN_CENTER);
    v5_main_page_internal_make_label_ex(page->root, 762, 124, 76, 24, "主轴倍率", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    page->spindle_override_label = v5_main_page_internal_make_label_ex(page->root, 850, 124, 52, 24, "100%", 28, 193, 238, LV_TEXT_ALIGN_RIGHT);
    page->spindle_override_reset_hit = v5_main_page_internal_make_override_reset_hit(
        page, 762, 64, 154, 58, v5_main_page_internal_spindle_override_reset_event_cb);
    page->spindle_override_slider = v5_main_page_internal_create_override_slider(page, 1, 762, 150, 142);
    v5_main_page_internal_make_divider(page->root, 758, 220, 150, 1);
    v5_main_page_internal_make_label_ex(page->root, 768, 218, 78, 24, "进给速度", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    v5_main_page_internal_make_label_ex(page->root, 850, 218, 66, 22, "mm/min", 28, 193, 238, LV_TEXT_ALIGN_LEFT);
    page->linear_velocity_label = v5_main_page_internal_make_label_ex(page->root, 762, 240, 142, 30, "0.0", 28, 193, 238, LV_TEXT_ALIGN_CENTER);
    v5_main_page_internal_make_label_ex(page->root, 762, 275, 76, 24, "进给倍率", 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    page->feed_override_label = v5_main_page_internal_make_label_ex(page->root, 850, 275, 52, 24, "100%", 28, 193, 238, LV_TEXT_ALIGN_RIGHT);
    page->feed_override_reset_hit = v5_main_page_internal_make_override_reset_hit(
        page, 762, 218, 154, 53, v5_main_page_internal_feed_override_reset_event_cb);
    page->feed_override_slider = v5_main_page_internal_create_override_slider(page, 0, 762, 307, 142);

    v5_main_page_internal_make_panel(page->root, 746, 342, 174, 188, 5, 24, 39);
    v5_main_page_internal_make_label_ex(page->root, 754, 350, 158, 22, "跟随误差 mm/deg", 210, 220, 226, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "%s: --.---", axis_text[i]);
        page->error_labels[i] = v5_main_page_internal_make_label_ex(page->root, 758, 378 + (int)i * 28, 150, 26, text, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    }
    v5_main_page_internal_make_panel(page->root, 746, 540, 174, 60, 5, 24, 39);
    page->cpu0_label = v5_main_page_internal_make_label_ex(page->root, 754, 545, 158, 24, "cpu0  --%", 255, 86, 86, LV_TEXT_ALIGN_LEFT);
    page->cpu1_label = v5_main_page_internal_make_label_ex(page->root, 754, 571, 158, 24, "cpu1  --%", 42, 221, 128, LV_TEXT_ALIGN_LEFT);

    v5_main_page_internal_make_panel(page->root, 0, 441, 560, 154, 7, 31, 48);
    v5_main_page_internal_make_label_ex(page->root, 18, 456, 110, 24, "行号  程序名:", 156, 178, 202, LV_TEXT_ALIGN_LEFT);
    page->program_name_label = v5_main_page_internal_make_label_ex(page->root, 132, 456, 180, 24, "手动输入", 218, 232, 242, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < V5_PROGRAM_PREVIEW_ROWS; ++i) {
        int y = 475 + (int)i * 26 + (i > 0U ? 1 : 0);
        page->program_line_bg[i] = v5_main_page_internal_make_panel(page->root, 14, y, 540, 26, i == 0U ? 43 : 7, i == 0U ? 133 : 31, i == 0U ? 83 : 48);
        page->program_line_labels[i] = v5_main_page_internal_make_label_ex(page->root, 24, 480 + (int)i * 26 + (i > 0U ? 1 : 0), 520, 20, "", i == 0U ? 226 : 156, i == 0U ? 238 : 178, i == 0U ? 246 : 202, LV_TEXT_ALIGN_LEFT);
    }
    v5_main_page_internal_create_main_program_edit_hit_area(page);

    v5_main_page_internal_make_v3_main_buttons(page);
    v5_main_page_internal_create_power_on_home_popup(page);
    page->selection_idle_timer = lv_timer_create(
        v5_main_page_internal_selection_idle_timer_cb,
        V5_MAIN_PAGE_SELECTION_IDLE_MS,
        page);
    page->jog_hold_timer = lv_timer_create(v5_main_page_internal_jog_hold_timer_cb, V5_MAIN_PAGE_JOG_HOLD_MS, page);
    if (!page->selection_idle_timer || !page->jog_hold_timer) {
        if (page->selection_idle_timer) {
            lv_timer_del(page->selection_idle_timer);
            page->selection_idle_timer = 0;
        }
        if (page->jog_hold_timer) {
            lv_timer_del(page->jog_hold_timer);
            page->jog_hold_timer = 0;
        }
        return 0;
    }
    lv_timer_pause(page->selection_idle_timer);
    lv_timer_pause(page->jog_hold_timer);
    v5_main_page_internal_update_main_page_state_button_visuals(page);
    v5_main_page_internal_update_estop_button_text(page);
    return page->button_count == V5_MAIN_PAGE_BUTTON_COUNT;
}

int v5_main_page_apply_status_flags(V5MainPage *page, const V5UiStatusView *status, unsigned int refresh_flags)
{
    V5CoordinatePanelSnapshot panel;
    V5ToolpathDisplaySnapshot dynamic_display;
    const V5ProgramRuntime *runtime = page && page->program_controller ? v5_program_controller_runtime(page->program_controller) : 0;
    unsigned int preview_count = 0U;
    unsigned int runtime_generation = 0U;
    int program_wcs_index = -1;
    double program_wcs_offset[3] = {0.0, 0.0, 0.0};
    int program_wcs_changed = 0;
    int program_fit_dirty = 0;
    int program_projection_dirty = 0;
    int static_toolpath_due = 0;
    int program_ac_changed = 0;
    int static_pose_changed = 0;
    int program_refresh_due = 0;
    int fit_overflow_checked = 0;
    int fit_overflow_changed = 0;
    int runtime_has_program = runtime && v5_program_runtime_has_open_program(runtime);
    unsigned int i;

    if (!page || !page->root) {
        return 0;
    }
    page->last_status_valid = status != 0;
    if (status) {
        page->last_status = *status;
    }
    if (refresh_flags == 0U) {
        return 1;
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U) {
        v5_coordinate_panel_from_status(status, &panel);
        for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
            const char display_axis = v5_main_page_internal_main_page_axis_display_char(page, i);
            if (page->axis_labels[i]) {
                char axis[2] = {display_axis, '\0'};
                v5_main_page_internal_set_label_text_if_changed(page->axis_labels[i], axis);
            }
            {
                char wcs_text[32];
                v5_main_page_internal_format_main_page_wcs_coordinate(wcs_text, sizeof(wcs_text), status, &page->native_readback, i);
                v5_coordinate_digits_set_value(
                    &page->coordinate_digits,
                    0U,
                    i,
                    panel.lines[i].mcs_text,
                    v5_main_page_internal_main_coordinate_digit_color(page, i, 0));
                v5_coordinate_digits_set_value(
                    &page->coordinate_digits,
                    1U,
                    i,
                    wcs_text,
                    v5_main_page_internal_main_coordinate_digit_color(page, i, 1));
            }
            if (page->error_labels[i]) {
                char error_text[32];
                snprintf(error_text, sizeof(error_text), "%c: %s", display_axis, panel.lines[i].following_error_text);
                v5_main_page_internal_set_label_text_if_changed(page->error_labels[i], error_text);
            }
        }
        v5_main_page_internal_set_label_text_if_changed(page->spindle_speed_label, panel.spindle_speed_text);
        v5_main_page_internal_set_label_text_if_changed(page->linear_velocity_label, panel.linear_velocity_text);
        v5_main_page_internal_set_label_text_if_changed(page->feed_override_label, panel.feed_override_text);
        v5_main_page_internal_set_label_text_if_changed(page->spindle_override_label, panel.spindle_override_text);
        v5_main_page_internal_sync_override_sliders(page, status);
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) {
        v5_main_page_internal_update_main_page_wcs_header(page);
        v5_main_page_internal_update_toolpath_status_text(page);
        v5_main_page_internal_update_main_page_modal_label(page);
        if (page->cpu0_label && page->cpu1_label) {
            char cpu0_text[24];
            char cpu1_text[24];
            v5_remote_metrics_display_text(cpu0_text, sizeof(cpu0_text), cpu1_text, sizeof(cpu1_text));
            v5_main_page_internal_set_label_text_if_changed(page->cpu0_label, cpu0_text);
            v5_main_page_internal_set_label_text_if_changed(page->cpu1_label, cpu1_text);
        }
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U) {
        static_toolpath_due =
            ((refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) ||
            (runtime_has_program && !page->toolpath_program_visible) ||
            !page->toolpath_fit.valid ||
            page->toolpath_program_view_generation != page->toolpath_view_generation;
        program_ac_changed = v5_main_page_internal_main_page_program_ac_projection_changed(page, status);
        static_pose_changed = v5_main_page_internal_main_page_static_pose_changed(page, status);
        program_refresh_due = static_toolpath_due || program_ac_changed;

        if (!page->toolpath_fit.valid && status) {
            if (v5_toolpath_display_fit_from_status(status, page->view_plane, &page->toolpath_fit)) {
                v5_main_page_internal_main_page_expand_visible_toolpath_fit(page, status, &page->toolpath_fit);
                page->toolpath_static_cache_misses += 1U;
            }
        }

        if (program_refresh_due && runtime_has_program) {
            if (static_toolpath_due) {
                runtime_generation = v5_program_runtime_loaded_epoch(runtime);
                preview_count = v5_program_runtime_preview_trajectory(
                    runtime,
                    page->toolpath_program_points,
                    V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT);
                for (i = 0U; i < preview_count; ++i) {
                    page->toolpath_program_break_before[i] =
                        v5_program_runtime_preview_break_before(runtime, i) ? 1U : 0U;
                }
                if (preview_count > 0U &&
                    !v5_main_page_internal_main_page_apply_program_preview_wcs_offset(
                        page, runtime, page->toolpath_program_points, preview_count, &program_wcs_index, program_wcs_offset)) {
                    preview_count = 0U;
                }
                page->toolpath_program_point_count = preview_count;
            } else {
                runtime_generation = page->toolpath_program_generation;
                preview_count = page->toolpath_program_point_count;
                program_wcs_index = page->toolpath_program_wcs_index;
                program_wcs_offset[0] = page->toolpath_program_wcs_offset[0];
                program_wcs_offset[1] = page->toolpath_program_wcs_offset[1];
                program_wcs_offset[2] = page->toolpath_program_wcs_offset[2];
            }
            if (preview_count > 0U) {
                if (!v5_main_page_internal_main_page_update_program_project_points(page, status, preview_count)) {
                    preview_count = 0U;
                }
            }
            if (preview_count > 0U) {
                program_wcs_changed =
                    !page->toolpath_program_wcs_valid ||
                    page->toolpath_program_wcs_epoch != page->native_readback.wcs_offsets_epoch ||
                    page->toolpath_program_wcs_index != program_wcs_index ||
                    fabs(page->toolpath_program_wcs_offset[0] - program_wcs_offset[0]) > 0.0005 ||
                    fabs(page->toolpath_program_wcs_offset[1] - program_wcs_offset[1]) > 0.0005 ||
                    fabs(page->toolpath_program_wcs_offset[2] - program_wcs_offset[2]) > 0.0005;
            }
            program_fit_dirty =
                static_toolpath_due &&
                (!page->toolpath_program_visible ||
                 page->toolpath_program_generation != runtime_generation ||
                 page->toolpath_program_plane != page->view_plane ||
                 !page->toolpath_fit.valid);
            if (preview_count > 0U && !program_fit_dirty &&
                (program_ac_changed || program_wcs_changed || static_pose_changed)) {
                fit_overflow_checked = 1;
                if (v5_main_page_internal_main_page_program_outside_fit_window(page) ||
                    v5_main_page_internal_main_page_dynamic_toolpath_outside_fit_window(page, status) ||
                    v5_main_page_internal_main_page_static_geometry_outside_fit_window(page, status)) {
                    fit_overflow_changed = v5_main_page_internal_main_page_expand_fit_on_overflow(page, status);
                }
            }
            program_projection_dirty =
                program_fit_dirty ||
                program_ac_changed ||
                program_wcs_changed ||
                fit_overflow_changed ||
                page->toolpath_program_view_generation != page->toolpath_view_generation;
            if (preview_count > 0U && program_projection_dirty) {
                if (!program_fit_dirty ||
                    v5_toolpath_display_fit_from_points(
                        page->toolpath_program_project_points,
                        preview_count,
                        page->view_plane,
                        &page->toolpath_fit)) {
                    if (program_fit_dirty) {
                        v5_main_page_internal_main_page_expand_visible_toolpath_fit(page, status, &page->toolpath_fit);
                    }
                    page->toolpath_program_point_count = preview_count;
                    v5_main_page_internal_main_page_project_program_with_current_fit(page);
                    page->toolpath_program_generation = runtime_generation;
                    page->toolpath_program_view_generation = page->toolpath_view_generation;
                    page->toolpath_program_plane = page->view_plane;
                    page->toolpath_program_wcs_valid = 1;
                    page->toolpath_program_wcs_index = program_wcs_index;
                    page->toolpath_program_wcs_epoch = page->native_readback.wcs_offsets_epoch;
                    page->toolpath_program_wcs_offset[0] = program_wcs_offset[0];
                    page->toolpath_program_wcs_offset[1] = program_wcs_offset[1];
                    page->toolpath_program_wcs_offset[2] = program_wcs_offset[2];
                    if (program_fit_dirty) {
                        page->toolpath_static_cache_misses += 1U;
                    }
                } else {
                    v5_main_page_internal_hide_toolpath_program_line(page);
                }
            } else if (preview_count > 0U && page->toolpath_program_visible) {
                page->toolpath_static_cache_hits += 1U;
            } else {
                v5_main_page_internal_hide_toolpath_program_line(page);
            }
    } else if (static_toolpath_due) {
        v5_main_page_internal_hide_toolpath_program_line(page);
        page->toolpath_program_generation = 0U;
        page->toolpath_program_view_generation = 0U;
        page->toolpath_program_wcs_valid = 0;
        page->toolpath_program_wcs_index = -1;
        page->toolpath_program_wcs_epoch = 0U;
        memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
        page->toolpath_program_point_count = 0U;
        page->toolpath_program_ac_valid = 0;
        page->toolpath_program_model_kind = 0;
        page->toolpath_program_ac_a_deg = 0.0;
        page->toolpath_program_ac_c_deg = 0.0;
    }

    if (!fit_overflow_checked &&
        (v5_main_page_internal_main_page_dynamic_toolpath_outside_fit_window(page, status) ||
         ((static_toolpath_due || static_pose_changed) &&
          v5_main_page_internal_main_page_static_geometry_outside_fit_window(page, status)))) {
        fit_overflow_changed = v5_main_page_internal_main_page_expand_fit_on_overflow(page, status);
        if (fit_overflow_changed && runtime_has_program && page->toolpath_program_visible) {
            v5_main_page_internal_main_page_project_program_with_current_fit(page);
        }
    }

    if (static_toolpath_due || program_ac_changed || static_pose_changed || fit_overflow_changed ||
        (refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) {
        v5_main_page_internal_update_toolpath_state_lines(page, status);
        v5_main_page_internal_main_page_store_static_pose(page, status);
    }

    v5_toolpath_display_from_status_with_fit(
        status,
        &page->toolpath_fit,
        (double)V5_TOOLPATH_W,
        (double)V5_TOOLPATH_H,
        &dynamic_display);
    v5_main_page_internal_apply_toolpath_view_transform_to_snapshot(page, &dynamic_display);
    {
        V5ToolpathScreenPoint cmd_tip_point;
        int cmd_tip_valid = v5_main_page_internal_main_page_project_cmd_tip(page, status, &cmd_tip_point);
        v5_main_page_internal_set_toolpath_v3_dot_center(page->toolpath_microkernel_marker_dot, &cmd_tip_point, cmd_tip_valid);
    }
    v5_main_page_internal_set_toolpath_v3_dot_center(page->toolpath_holder_marker_line, &dynamic_display.mcs_point, dynamic_display.mcs_valid);
    v5_main_page_internal_update_toolpath_holder_line(page, status, dynamic_display.mcs_valid ? &dynamic_display.mcs_point : 0);
    if (!dynamic_display.mcs_valid) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
    }
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_BUTTONS) != 0U) {
        v5_main_page_internal_update_main_page_state_button_visuals(page);
    }
    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_ESTOP) != 0U) {
        v5_main_page_internal_update_estop_button_text(page);
    }

    return 1;
}

int v5_main_page_apply_status(V5MainPage *page, const V5UiStatusView *status)
{
    return v5_main_page_apply_status_flags(page, status, V5_MAIN_PAGE_REFRESH_ALL);
}
