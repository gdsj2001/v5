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

static unsigned int program_preview_total_lines_for_runtime(const V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return 0U;
    }
    if (v5_program_runtime_has_mdi(runtime)) {
        return v5_main_page_internal_count_preview_source_lines(v5_program_runtime_mdi_text(runtime));
    }
    if (v5_program_runtime_has_open_program(runtime) && runtime->gcode_text) {
        return v5_main_page_internal_count_preview_source_lines(runtime->gcode_text);
    }
    return 0U;
}

static int main_page_program_preview_has_active_line(V5MainPage *page, const V5ProgramRuntime *runtime)
{
    unsigned int total = program_preview_total_lines_for_runtime(runtime);
    return v5_main_page_internal_active_preview_line_from_readback(page, runtime, 0) > 0 ||
           v5_main_page_internal_remembered_program_preview_highlight_line(page, runtime, total) > 0;
}

static int main_page_scroll_program_preview_rows(V5MainPage *page, int rows)
{
    const V5ProgramRuntime *runtime;
    unsigned int total;
    unsigned int start;
    if (!page || rows == 0 || !page->program_controller) {
        return 0;
    }
    runtime = v5_program_controller_runtime(page->program_controller);
    if (main_page_program_preview_has_active_line(page, runtime)) {
        return 0;
    }
    total = program_preview_total_lines_for_runtime(runtime);
    if (total <= V5_PROGRAM_PREVIEW_ROWS) {
        page->program_preview_scroll_start_line = 1U;
        return 0;
    }
    start = page->program_preview_scroll_start_line ? page->program_preview_scroll_start_line : 1U;
    if (rows > 0) {
        start += (unsigned int)rows;
    } else {
        unsigned int up = (unsigned int)(-rows);
        start = up >= start ? 1U : start - up;
    }
    start = v5_main_page_internal_clamp_preview_start_line(total, start);
    if (start == page->program_preview_scroll_start_line) {
        return 0;
    }
    page->program_preview_scroll_start_line = start;
    v5_main_page_internal_refresh_program_preview_rows(page, runtime);
    return 1;
}

int v5_main_page_internal_main_page_handle_program_preview_touch(
    V5MainPage *page,
    const lv_point_t *point,
    int pressed,
    int *changed)
{
    int local_changed = 0;
    if (changed) {
        *changed = 0;
    }
    if (!page) {
        return 0;
    }
    if (!pressed || !point) {
        int was_active = page->program_preview_touch_active;
        page->program_preview_touch_active = 0;
        page->program_preview_touch_accum_y = 0;
        return was_active;
    }
    if (!page->program_preview_touch_active) {
        if (!v5_main_page_internal_point_in_program_preview_zone(point)) {
            return 0;
        }
        page->program_preview_touch_active = 1;
        page->program_preview_touch_last_point = *point;
        page->program_preview_touch_accum_y = 0;
        page->program_preview_dragged = 0;
        return 1;
    }
    page->program_preview_touch_accum_y += point->y - page->program_preview_touch_last_point.y;
    page->program_preview_touch_last_point = *point;
    while (page->program_preview_touch_accum_y <= -V5_PROGRAM_PREVIEW_LINE_STEP) {
        if (main_page_scroll_program_preview_rows(page, 1)) {
            local_changed = 1;
        }
        page->program_preview_touch_accum_y += V5_PROGRAM_PREVIEW_LINE_STEP;
        page->program_preview_dragged = 1;
    }
    while (page->program_preview_touch_accum_y >= V5_PROGRAM_PREVIEW_LINE_STEP) {
        if (main_page_scroll_program_preview_rows(page, -1)) {
            local_changed = 1;
        }
        page->program_preview_touch_accum_y -= V5_PROGRAM_PREVIEW_LINE_STEP;
        page->program_preview_dragged = 1;
    }
    if (changed) {
        *changed = local_changed;
    }
    return 1;
}

int v5_main_page_handle_touch_points(V5MainPage *page, const lv_point_t *points, int count, int pressed, int *changed)
{
    double mid_x;
    double mid_y;
    double distance;
    double angle;
    double ratio;
    int local_changed = 0;
    if (changed) {
        *changed = 0;
    }
    if (!page || !page->root) {
        return 0;
    }
    if (!pressed || !points || count <= 0) {
        const int was_active = page->toolpath_gesture_active_count > 0;
        const int preview_was_active = page->program_preview_touch_active;
        (void)v5_main_page_internal_main_page_handle_program_preview_touch(page, 0, 0, 0);
        page->toolpath_gesture_active_count = 0;
        return was_active || preview_was_active;
    }
    if (count > 2) {
        count = 2;
    }
    if (count == 1 && v5_main_page_internal_main_page_handle_program_preview_touch(page, &points[0], pressed, &local_changed)) {
        page->toolpath_gesture_active_count = 0;
        if (changed) {
            *changed = local_changed;
        }
        return 1;
    }
    page->program_preview_touch_active = 0;
    page->program_preview_touch_accum_y = 0;
    if (!v5_main_page_internal_toolpath_points_in_graphics_zone(points, count)) {
        page->toolpath_gesture_active_count = 0;
        return 0;
    }
    if (count == 1) {
        if (page->toolpath_gesture_active_count != 1) {
            page->toolpath_gesture_active_count = 1;
            page->toolpath_gesture_last_points[0] = points[0];
            return 1;
        }
        page->toolpath_manual_pan_x += (double)points[0].x - (double)page->toolpath_gesture_last_points[0].x;
        page->toolpath_manual_pan_y += (double)points[0].y - (double)page->toolpath_gesture_last_points[0].y;
        local_changed = points[0].x != page->toolpath_gesture_last_points[0].x || points[0].y != page->toolpath_gesture_last_points[0].y;
        page->toolpath_gesture_last_points[0] = points[0];
    } else {
        mid_x = ((double)points[0].x + (double)points[1].x) * 0.5;
        mid_y = ((double)points[0].y + (double)points[1].y) * 0.5;
        distance = v5_main_page_internal_point_distance(&points[0], &points[1]);
        angle = v5_main_page_internal_point_angle_deg(&points[0], &points[1]);
        if (page->toolpath_gesture_active_count != 2 || page->toolpath_gesture_last_distance <= 1.0) {
            page->toolpath_gesture_active_count = 2;
            page->toolpath_gesture_last_points[0] = points[0];
            page->toolpath_gesture_last_points[1] = points[1];
            page->toolpath_gesture_last_mid_x = mid_x;
            page->toolpath_gesture_last_mid_y = mid_y;
            page->toolpath_gesture_last_distance = distance > 1.0 ? distance : 1.0;
            page->toolpath_gesture_last_angle_deg = angle;
            return 1;
        }
        ratio = distance > 1.0 ? distance / page->toolpath_gesture_last_distance : 1.0;
        page->toolpath_manual_scale = v5_main_page_internal_clamp_double(
            (page->toolpath_manual_scale > 0.0 ? page->toolpath_manual_scale : 1.0) * ratio,
            V5_TOOLPATH_GESTURE_MIN_SCALE,
            V5_TOOLPATH_GESTURE_MAX_SCALE);
        page->toolpath_manual_pan_x += mid_x - page->toolpath_gesture_last_mid_x;
        page->toolpath_manual_pan_y += mid_y - page->toolpath_gesture_last_mid_y;
        page->toolpath_manual_rotate_deg = v5_main_page_internal_normalize_deg(page->toolpath_manual_rotate_deg + angle - page->toolpath_gesture_last_angle_deg);
        local_changed = fabs(ratio - 1.0) > 1.0e-6 ||
            fabs(mid_x - page->toolpath_gesture_last_mid_x) > 1.0e-6 ||
            fabs(mid_y - page->toolpath_gesture_last_mid_y) > 1.0e-6 ||
            fabs(angle - page->toolpath_gesture_last_angle_deg) > 1.0e-6;
        page->toolpath_gesture_last_points[0] = points[0];
        page->toolpath_gesture_last_points[1] = points[1];
        page->toolpath_gesture_last_mid_x = mid_x;
        page->toolpath_gesture_last_mid_y = mid_y;
        page->toolpath_gesture_last_distance = distance > 1.0 ? distance : 1.0;
        page->toolpath_gesture_last_angle_deg = angle;
    }
    if (local_changed) {
        page->toolpath_view_generation += 1U;
        if (page->toolpath_view_generation == 0U) {
            page->toolpath_view_generation = 1U;
        }
        if (changed) {
            *changed = 1;
        }
    }
    return 1;
}

void v5_main_page_refresh_program_status(V5MainPage *page)
{
    const V5ProgramRuntime *runtime;

    if (!page || !page->root ||
        lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    runtime = page->program_controller ? v5_program_controller_runtime(page->program_controller) : 0;
    v5_main_page_internal_refresh_program_preview_rows(page, runtime);
    if (runtime && v5_program_runtime_has_mdi(runtime)) {
        if (page->program_name_label) {
            v5_main_page_internal_set_label_text_if_changed(page->program_name_label, "手动输入");
        }
    } else if (runtime && v5_program_runtime_has_open_program(runtime)) {
        if (page->program_name_label) {
            v5_main_page_internal_set_label_text_if_changed(page->program_name_label, runtime->display_name);
        }
    } else {
        if (page->program_name_label) {
            v5_main_page_internal_set_label_text_if_changed(page->program_name_label, "手动输入");
        }
    }
}


void v5_main_page_select_all_axes(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->selection.space = V5_MAIN_PAGE_SELECT_MCS;
    page->selection.axis = '*';
    page->selection.all_axes = 1;
    if (page->selection_idle_timer) {
        lv_timer_pause(page->selection_idle_timer);
    }
    v5_main_page_internal_refresh_coordinate_selection_now(page);
    v5_main_page_internal_update_axis_all_button_visuals(page);
}

int v5_main_page_select_axis(V5MainPage *page, V5MainPageSelectionSpace space, char axis)
{
    char up;
    unsigned int i;
    int active_axis = 0;
    if (!page) {
        return 0;
    }
    up = (char)toupper((unsigned char)axis);
    if ((space != V5_MAIN_PAGE_SELECT_MCS && space != V5_MAIN_PAGE_SELECT_WCS) ||
        (up != 'X' && up != 'Y' && up != 'Z' && up != 'A' && up != 'B' && up != 'C')) {
        return 0;
    }
    for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        if (page->mcs_targets[i].axis == up && page->wcs_targets[i].axis == up) {
            active_axis = 1;
            break;
        }
    }
    if (!active_axis) {
        return 0;
    }
    page->selection.space = space;
    page->selection.axis = up;
    page->selection.all_axes = 0;
    v5_main_page_internal_reset_selection_idle_timer(page);
    v5_main_page_internal_refresh_coordinate_selection_now(page);
    v5_main_page_internal_update_axis_all_button_visuals(page);
    return 1;
}

static int native_readback_requests_estop_reset(const V5NativeReadback *readback)
{
    if (v5_native_readback_safety_estop_known(readback) && readback->safety_estop_active) {
        return 1;
    }
    if (v5_native_readback_machine_enable_known(readback) && !readback->machine_enabled) {
        return 1;
    }
    return 0;
}

void v5_main_page_internal_update_estop_button_text(V5MainPage *page)
{
    const char *text = "急停";
    unsigned int i;
    if (!page) {
        return;
    }
    if (native_readback_requests_estop_reset(&page->native_readback)) {
        text = "取消急停";
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == V5_MAIN_PAGE_ACTION_ESTOP_FORCE && page->button_labels[i]) {
            v5_main_page_internal_set_label_text_if_changed(page->button_labels[i], text);
            return;
        }
    }
}

void v5_main_page_bind_program_controller(V5MainPage *page, V5ProgramController *controller)
{
    if (!page) {
        return;
    }
    page->program_controller = controller;
    v5_main_page_refresh_program_status(page);
}

void v5_main_page_set_command_execution_enabled(V5MainPage *page, int enabled)
{
    if (!page) {
        return;
    }
    page->command_execution_enabled = enabled ? 1 : 0;
}

static int v5_main_page_native_model_equal(
    const V5NativeReadback *left,
    const V5NativeReadback *right)
{
    return left->motion_model_available == right->motion_model_available &&
        strcmp(left->motion_model, right->motion_model) == 0 &&
        left->g53_geometry_available == right->g53_geometry_available &&
        left->g53_geometry_stale == right->g53_geometry_stale &&
        left->g53_geometry_epoch == right->g53_geometry_epoch &&
        memcmp(left->g53_centers, right->g53_centers, sizeof(left->g53_centers)) == 0;
}

static int v5_main_page_native_wcs_equal(
    const V5NativeReadback *left,
    const V5NativeReadback *right)
{
    return left->wcs_actual_available == right->wcs_actual_available &&
        left->wcs_index == right->wcs_index &&
        left->wcs_offset_available == right->wcs_offset_available &&
        left->wcs_table_available == right->wcs_table_available &&
        left->wcs_offsets_epoch == right->wcs_offsets_epoch &&
        memcmp(left->wcs_offsets, right->wcs_offsets, sizeof(left->wcs_offsets)) == 0;
}

static int v5_main_page_native_program_equal(
    const V5NativeReadback *left,
    const V5NativeReadback *right)
{
    return left->interpreter_state_available == right->interpreter_state_available &&
        left->interpreter_paused == right->interpreter_paused &&
        left->interpreter_idle_available == right->interpreter_idle_available &&
        left->interpreter_idle == right->interpreter_idle &&
        left->current_line_available == right->current_line_available &&
        left->current_line == right->current_line &&
        left->motion_line_available == right->motion_line_available &&
        left->motion_line == right->motion_line &&
        left->mdi_run_available == right->mdi_run_available &&
        left->mdi_run_active == right->mdi_run_active &&
        left->mdi_run_line == right->mdi_run_line &&
        strcmp(left->mdi_run_command, right->mdi_run_command) == 0;
}

static int v5_main_page_native_safety_equal(
    const V5NativeReadback *left,
    const V5NativeReadback *right)
{
    return left->homed_available == right->homed_available &&
        left->all_homed == right->all_homed &&
        left->safety_estop_available == right->safety_estop_available &&
        left->safety_estop_active == right->safety_estop_active &&
        left->machine_enable_available == right->machine_enable_available &&
        left->machine_enabled == right->machine_enabled;
}

static int v5_main_page_native_modal_equal(
    const V5NativeReadback *left,
    const V5NativeReadback *right)
{
    return left->rtcp_actual_available == right->rtcp_actual_available &&
        left->rtcp_enabled == right->rtcp_enabled &&
        left->modal_actual_available == right->modal_actual_available &&
        strcmp(left->modal_text, right->modal_text) == 0 &&
        left->tool_actual_available == right->tool_actual_available &&
        left->tool_number == right->tool_number &&
        left->tool_length_available == right->tool_length_available &&
        left->tool_length_mm == right->tool_length_mm;
}

unsigned int v5_main_page_native_readback_change_flags(
    const V5NativeReadback *before,
    const V5NativeReadback *after)
{
    unsigned int flags = 0U;
    if (!before || !after) {
        return V5_MAIN_PAGE_NATIVE_READBACK_ALL;
    }
    if (!v5_main_page_native_model_equal(before, after)) {
        flags |= V5_MAIN_PAGE_NATIVE_READBACK_MODEL;
    }
    if (!v5_main_page_native_wcs_equal(before, after)) {
        flags |= V5_MAIN_PAGE_NATIVE_READBACK_WCS;
    }
    if (!v5_main_page_native_program_equal(before, after)) {
        flags |= V5_MAIN_PAGE_NATIVE_READBACK_PROGRAM;
    }
    if (!v5_main_page_native_safety_equal(before, after)) {
        flags |= V5_MAIN_PAGE_NATIVE_READBACK_SAFETY;
    }
    if (!v5_main_page_native_modal_equal(before, after)) {
        flags |= V5_MAIN_PAGE_NATIVE_READBACK_MODAL;
    }
    if (strcmp(before->unavailable_reason, after->unavailable_reason) != 0) {
        flags = V5_MAIN_PAGE_NATIVE_READBACK_ALL;
    }
    return flags;
}

void v5_main_page_set_native_readback_flags(
    V5MainPage *page,
    const V5NativeReadback *readback,
    unsigned int change_flags)
{
    if (!page) {
        return;
    }
    if (readback) {
        page->native_readback = *readback;
    } else {
        v5_native_readback_set_unavailable(&page->native_readback, "native_readback_unavailable");
    }
    if (!page->root || lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if ((change_flags & V5_MAIN_PAGE_NATIVE_READBACK_MODEL) != 0U) {
        v5_main_page_internal_update_coordinate_target_axes(page);
        v5_main_page_internal_update_coordinate_selection_style(page);
    }
    if ((change_flags & V5_MAIN_PAGE_NATIVE_READBACK_SAFETY) != 0U) {
        v5_main_page_internal_update_estop_button_text(page);
    }
    if ((change_flags & (V5_MAIN_PAGE_NATIVE_READBACK_MODEL |
                         V5_MAIN_PAGE_NATIVE_READBACK_WCS |
                         V5_MAIN_PAGE_NATIVE_READBACK_SAFETY |
                         V5_MAIN_PAGE_NATIVE_READBACK_MODAL)) != 0U) {
        v5_main_page_internal_update_main_page_state_button_visuals(page);
    }
    if ((change_flags & V5_MAIN_PAGE_NATIVE_READBACK_WCS) != 0U) {
        v5_main_page_internal_update_main_page_wcs_header(page);
    }
    if ((change_flags & V5_MAIN_PAGE_NATIVE_READBACK_MODAL) != 0U) {
        v5_main_page_internal_update_main_page_modal_label(page);
    }
    if ((change_flags & (V5_MAIN_PAGE_NATIVE_READBACK_MODEL |
                         V5_MAIN_PAGE_NATIVE_READBACK_WCS |
                         V5_MAIN_PAGE_NATIVE_READBACK_MODAL)) != 0U) {
        v5_main_page_internal_update_toolpath_status_text(page);
    }
    if ((change_flags & V5_MAIN_PAGE_NATIVE_READBACK_PROGRAM) != 0U) {
        v5_main_page_refresh_program_status(page);
    }
    if (page->last_status_valid &&
        (change_flags & (V5_MAIN_PAGE_NATIVE_READBACK_MODEL |
                         V5_MAIN_PAGE_NATIVE_READBACK_WCS |
                         V5_MAIN_PAGE_NATIVE_READBACK_MODAL)) != 0U) {
        v5_main_page_internal_update_toolpath_state_lines(page, &page->last_status);
    }
}

void v5_main_page_set_native_readback(V5MainPage *page, const V5NativeReadback *readback)
{
    v5_main_page_set_native_readback_flags(
        page,
        readback,
        V5_MAIN_PAGE_NATIVE_READBACK_ALL);
}

void v5_main_page_store_native_readback_during_modal(
    V5MainPage *page,
    const V5NativeReadback *readback)
{
    if (!page || !readback) {
        return;
    }
    page->native_readback = *readback;
    v5_main_page_internal_update_estop_button_text(page);
}

void v5_main_page_set_navigation_callback(V5MainPage *page, V5UiNavigationCallback cb, void *user_data)
{
    if (!page) {
        return;
    }
    page->navigation_cb = cb;
    page->navigation_user_data = user_data;
}

void v5_main_page_set_native_readback_refresh_callback(
    V5MainPage *page,
    V5MainPageNativeReadbackRefreshCallback cb,
    void *user_data)
{
    if (!page) {
        return;
    }
    page->native_readback_refresh_cb = cb;
    page->native_readback_refresh_user_data = user_data;
}

int v5_main_page_set_mdi_text(V5MainPage *page, const char *line)
{
    if (!page || !page->program_controller) {
        return 0;
    }
    if (!v5_program_runtime_set_mdi_line(&page->program_controller->runtime, line)) {
        return 0;
    }
    page->program_preview_scroll_start_line = 1U;
    v5_main_page_internal_clear_program_preview_highlight(page);
    page->program_preview_started_loaded_epoch = 0U;
    page->program_preview_touch_active = 0;
    page->program_preview_touch_accum_y = 0;
    page->program_preview_dragged = 0;
    v5_main_page_internal_mark_toolpath_static_dirty(page);
    v5_main_page_refresh_program_status(page);
    return 1;
}

int v5_main_page_open_program(V5MainPage *page, const char *path, V5ProgramOpenResult *open_report)
{
    V5ProgramOpenResult local_open;
    V5ProgramOpenResult *out = open_report ? open_report : &local_open;

    if (!page || !page->program_controller) {
        return 0;
    }
    if (!v5_command_program_open(page->program_controller, path, out)) {
        return 0;
    }
    page->last_program_open = *out;
    page->program_preview_scroll_start_line = 1U;
    v5_main_page_internal_clear_program_preview_highlight(page);
    page->program_preview_started_loaded_epoch = 0U;
    page->program_preview_touch_active = 0;
    page->program_preview_touch_accum_y = 0;
    page->program_preview_dragged = 0;
    v5_main_page_internal_mark_toolpath_static_dirty(page);
    v5_main_page_refresh_program_status(page);
    return 1;
}
