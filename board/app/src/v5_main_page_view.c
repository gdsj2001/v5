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
    page->toolpath_view_generation = 1U;
    page->toolpath_fit_generation = 1U;
    page->toolpath_program_plane = page->view_plane;
    page->toolpath_manual_scale = 1.0;
    memset(&page->toolpath_fit, 0, sizeof(page->toolpath_fit));
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
    {
        const V5ToolpathViewport *viewport = v5_toolpath_viewport();
    v5_main_page_internal_clear_obj_style(page->toolpath_clip_layer);
    lv_obj_set_pos(page->toolpath_clip_layer, viewport->x, viewport->y);
    lv_obj_set_size(page->toolpath_clip_layer, viewport->width, viewport->height);
    }
    lv_obj_set_style_bg_opa(page->toolpath_clip_layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page->toolpath_clip_layer, LV_OBJ_FLAG_CLICKABLE);
    page->trajectory_line = v5_main_page_internal_create_toolpath_scene_layer(
        page,
        page->toolpath_clip_layer);
    page->toolpath_dynamic_layer =
        v5_main_page_internal_create_toolpath_scene_layer(
            page,
            page->toolpath_clip_layer);
    page->toolpath_summary_label = v5_main_page_internal_make_label_ex(page->root, 12, 401, 374, 18, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    page->toolpath_detail_label = v5_main_page_internal_make_label_ex(page->root, 12, 420, 374, 18, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_view_label = v5_main_page_internal_make_label_ex(page->root, 18, 358, 260, 24, "", 86, 204, 252, LV_TEXT_ALIGN_LEFT);
    page->toolpath_model_primary_label = v5_main_page_internal_make_label_ex(page->root, 132, 162, 24, 22, "-", 255, 113, 118, LV_TEXT_ALIGN_CENTER);
    page->toolpath_model_child_label = v5_main_page_internal_make_label_ex(page->root, 160, 162, 20, 22, "-", 120, 240, 255, LV_TEXT_ALIGN_CENTER);
    page->toolpath_wcs_label = v5_main_page_internal_make_label_ex(page->root, 18, 326, 42, 22, "", 68, 221, 144, LV_TEXT_ALIGN_LEFT);
    lv_obj_add_flag(page->toolpath_model_primary_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->toolpath_model_child_label, LV_OBJ_FLAG_HIDDEN);
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

    v5_main_page_internal_make_panel(page->root, 746, 342, 174, 194, 5, 24, 39);
    v5_main_page_internal_make_label_ex(page->root, 754, 350, 158, 22, "跟随误差 mm/deg", 210, 220, 226, LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        char text[16];
        snprintf(text, sizeof(text), "%s: --.---", axis_text[i]);
        page->error_labels[i] = v5_main_page_internal_make_label_ex(page->root, 758, 376 + (int)i * 26, 150, 24, text, 238, 245, 248, LV_TEXT_ALIGN_LEFT);
    }
    page->contour_error_label = v5_main_page_internal_make_label_ex(
        page->root, 758, 507, 150, 24, "DD: --.---",
        28, 193, 238, LV_TEXT_ALIGN_LEFT);
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
    if (!page->selection_idle_timer) {
        return 0;
    }
    lv_timer_pause(page->selection_idle_timer);
    v5_main_page_internal_update_main_page_state_button_visuals(page);
    v5_main_page_internal_update_estop_button_text(page);
    return page->button_count == V5_MAIN_PAGE_BUTTON_COUNT;
}

int v5_main_page_apply_status_flags(V5MainPage *page, const V5UiStatusView *status, unsigned int refresh_flags)
{
    V5CoordinatePanelSnapshot panel;
    const V5ProgramRuntime *runtime = page && page->program_controller ? v5_program_controller_runtime(page->program_controller) : 0;
    int panel_ready = 0;
    int pose_refresh_due = 0;
    int structure_refresh_due = 0;
    int runtime_has_program = runtime && v5_program_runtime_has_open_program(runtime);
    unsigned int i;

    if (!page || !page->root) {
        return 0;
    }
    page->last_status_valid = status != 0;
    if (status) {
        page->last_status = *status;
    }
    /* Hidden Main keeps only the immutable SHM snapshot. */
    if (lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        return 1;
    }
    if (refresh_flags == 0U) {
        return 1;
    }
    pose_refresh_due =
        (refresh_flags & (V5_MAIN_PAGE_REFRESH_POSE | V5_MAIN_PAGE_REFRESH_STRUCTURE)) != 0U;
    structure_refresh_due =
        (refresh_flags & V5_MAIN_PAGE_REFRESH_STRUCTURE) != 0U;
    if (pose_refresh_due) {
        page->toolpath_pose_refresh_count += 1U;
    }
    if (structure_refresh_due) {
        page->toolpath_structure_refresh_count += 1U;
    }
    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U) {
        page->toolpath_dynamic_refresh_count += 1U;
    }
    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_COORDINATES) != 0U) {
        v5_coordinate_panel_from_status(status, &panel);
        panel_ready = 1;
        v5_coordinate_digits_begin_update(&page->coordinate_digits);
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
        v5_coordinate_digits_end_update(&page->coordinate_digits);
    }
    if (page->contour_error_label &&
        (refresh_flags & (V5_MAIN_PAGE_REFRESH_COORDINATES |
                          V5_MAIN_PAGE_REFRESH_SCENE |
                          V5_MAIN_PAGE_REFRESH_POSE |
                          V5_MAIN_PAGE_REFRESH_STRUCTURE)) != 0U) {
        char contour_text[32];
        if (!panel_ready) {
            v5_coordinate_panel_from_status(status, &panel);
            panel_ready = 1;
        }
        snprintf(
            contour_text, sizeof(contour_text),
            "DD: %s", panel.tool_tip_contour_error_text);
        v5_main_page_internal_set_label_text_if_changed(
            page->contour_error_label, contour_text);
    }
    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_RATES) != 0U) {
        if (!panel_ready) {
            v5_coordinate_panel_from_status(status, &panel);
            panel_ready = 1;
        }
        v5_main_page_internal_set_label_text_if_changed(
            page->spindle_speed_label, panel.spindle_speed_text);
        v5_main_page_internal_set_label_text_if_changed(
            page->linear_velocity_label, panel.linear_velocity_text);
        v5_main_page_internal_set_label_text_if_changed(
            page->feed_override_label, panel.feed_override_text);
        v5_main_page_internal_set_label_text_if_changed(
            page->spindle_override_label, panel.spindle_override_text);
        v5_main_page_internal_sync_override_sliders(page, status);
    }

    if ((refresh_flags & V5_MAIN_PAGE_REFRESH_SLOW) != 0U) {
        v5_main_page_internal_update_main_page_wcs_header(page);
        v5_main_page_internal_update_toolpath_status_text(page);
        v5_main_page_internal_update_main_page_modal_label(page);
        if (page->cpu0_label && page->cpu1_label) {
            char cpu0_text[24];
            char cpu1_text[24];
            v5_remote_metrics_display_text(status, cpu0_text, sizeof(cpu0_text), cpu1_text, sizeof(cpu1_text));
            v5_main_page_internal_set_label_text_if_changed(page->cpu0_label, cpu0_text);
            v5_main_page_internal_set_label_text_if_changed(page->cpu1_label, cpu1_text);
        }
    }

    if ((refresh_flags & (V5_MAIN_PAGE_REFRESH_SCENE |
                          V5_MAIN_PAGE_REFRESH_POSE |
                          V5_MAIN_PAGE_REFRESH_STRUCTURE)) != 0U) {
        uint64_t expected_program_generation = runtime_has_program ?
            v5_program_runtime_loaded_epoch(runtime) : 0ULL;
        uint64_t expected_source_identity = runtime_has_program ?
            v5_program_scene_source_identity(v5_program_runtime_source_sha256(runtime)) : 0ULL;
        int request_changed =
            page->toolpath_last_request_program_source_identity != expected_source_identity ||
            page->toolpath_last_request_program_generation != expected_program_generation ||
            page->toolpath_last_request_view_generation != page->toolpath_view_generation ||
            page->toolpath_last_request_fit_generation != page->toolpath_fit_generation ||
            page->toolpath_last_request_page_visible !=
                (uint32_t)(page->page_visible ? 1U : 0U);
        int scene_matches = status &&
            (status->valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) != 0U &&
            status->display_scene &&
            status->display_scene->program_generation == expected_program_generation &&
            status->display_scene->program_source_identity == expected_source_identity &&
            status->display_scene->view_generation == page->toolpath_view_generation &&
            status->display_scene->plane == (uint32_t)page->view_plane;
        v5_main_page_internal_coalesce_toolpath_invalidations(page);
        if (request_changed) {
            if (page->program_controller) {
                v5_program_runtime_invalidate_scene_ready(
                    &page->program_controller->runtime);
            }
            (void)v5_main_page_internal_publish_program_scene_request(page);
            page->toolpath_request_last_send_tick = lv_tick_get();
            page->toolpath_request_retry_count = 0U;
            scene_matches = 0;
        }
        if (scene_matches) {
            if (page->toolpath_scene_generation == status->scene_generation) {
                page->toolpath_static_cache_hits += 1U;
            } else {
                v5_main_page_internal_apply_display_scene(
                    page, status->display_scene, status->scene_generation);
                page->toolpath_scene_generation = status->scene_generation;
                page->toolpath_program_generation = (unsigned int)expected_program_generation;
                page->toolpath_program_view_generation = page->toolpath_view_generation;
                page->toolpath_program_plane = page->view_plane;
                page->toolpath_fit.valid = 1;
                page->toolpath_fit.plane = page->view_plane;
                page->toolpath_fit.generation = (unsigned int)status->display_scene->fit_generation;
                page->toolpath_program_rtcp_transform_count =
                    (unsigned int)status->display_scene->rtcp_transform_count;
                page->toolpath_program_fused_frame_count =
                    (unsigned int)status->display_scene->project_count;
                page->toolpath_static_cache_misses += 1U;
            }
            if (runtime_has_program && page->program_controller) {
                (void)v5_program_runtime_publish_scene_ready(
                    &page->program_controller->runtime,
                    (unsigned int)expected_program_generation,
                    status->scene_generation,
                    (unsigned int)status->display_scene->fit_generation);
            }
        } else if (!request_changed) {
            unsigned int retry_delay_ms =
                v5_program_scene_request_retry_delay_ms(
                    page->toolpath_request_retry_count);
            if (retry_delay_ms != 0U &&
                lv_tick_elaps(page->toolpath_request_last_send_tick) >=
                    retry_delay_ms) {
                (void)v5_main_page_internal_publish_program_scene_request(page);
                page->toolpath_request_last_send_tick = lv_tick_get();
                page->toolpath_request_retry_count += 1U;
            }
            if (runtime_has_program && page->program_controller) {
                v5_program_runtime_invalidate_scene_ready(&page->program_controller->runtime);
            }
            v5_main_page_internal_hide_toolpath_program_line(page);
            v5_main_page_internal_hide_toolpath_unproven_geometry(page);
        }
        if (structure_refresh_due) {
            lv_obj_update_layout(page->root);
        }
        v5_main_page_internal_coalesce_toolpath_invalidations(page);
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
