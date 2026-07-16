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

int v5_main_page_internal_main_page_apply_program_preview_wcs_offset(
    const V5MainPage *page,
    const V5ProgramRuntime *runtime,
    V5StatusPoint *points,
    unsigned int count,
    int *wcs_index_out,
    double wcs_offset_out[3])
{
    double offset[3];
    int resolved_wcs_index = -1;
    unsigned int i;

    if (wcs_index_out) {
        *wcs_index_out = -1;
    }
    if (wcs_offset_out) {
        memset(wcs_offset_out, 0, sizeof(double) * 3U);
    }
    if (!page || !runtime || !points || count == 0U ||
        !v5_native_readback_wcs_table_known(&page->native_readback)) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        int point_wcs_index = -1;
        if (!v5_program_runtime_preview_wcs_index(runtime, i, &point_wcs_index) ||
            point_wcs_index < 0 || point_wcs_index >= (int)V5_NATIVE_READBACK_WCS_COUNT) {
            return 0;
        }
        if (resolved_wcs_index == -1) {
            resolved_wcs_index = point_wcs_index;
        } else if (resolved_wcs_index != point_wcs_index) {
            resolved_wcs_index = -2;
        }
        offset[0] = page->native_readback.wcs_offsets[point_wcs_index][0];
        offset[1] = page->native_readback.wcs_offsets[point_wcs_index][1];
        offset[2] = page->native_readback.wcs_offsets[point_wcs_index][2];
        if (!isfinite(offset[0]) || !isfinite(offset[1]) || !isfinite(offset[2])) {
            return 0;
        }
        points[i].axis[0] += offset[0];
        points[i].axis[1] += offset[1];
        points[i].axis[2] += offset[2];
    }
    if (wcs_index_out) {
        *wcs_index_out = resolved_wcs_index >= 0 ? resolved_wcs_index : -1;
    }
    if (wcs_offset_out && resolved_wcs_index >= 0) {
        offset[0] = page->native_readback.wcs_offsets[resolved_wcs_index][0];
        offset[1] = page->native_readback.wcs_offsets[resolved_wcs_index][1];
        offset[2] = page->native_readback.wcs_offsets[resolved_wcs_index][2];
        wcs_offset_out[0] = offset[0];
        wcs_offset_out[1] = offset[1];
        wcs_offset_out[2] = offset[2];
    }
    return 1;
}

void v5_main_page_internal_hide_toolpath_program_line(V5MainPage *page)
{
    unsigned int segment;
    if (!page || !page->trajectory_line) {
        return;
    }
    page->toolpath_program_wcs_valid = 0;
    page->toolpath_program_wcs_index = -1;
    page->toolpath_program_wcs_epoch = 0U;
    memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_ac_valid = 0;
    page->toolpath_program_model_kind = 0;
    page->toolpath_program_ac_a_deg = 0.0;
    page->toolpath_program_ac_c_deg = 0.0;
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    for (segment = 0U; segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++segment) {
        (void)v5_main_page_internal_update_toolpath_program_segment(page, segment, 0, 0U);
    }
    if (page->toolpath_program_visible) {
        page->toolpath_program_visible = 0;
        page->trajectory_point_count = 0U;
        page->toolpath_line_rewrite_count += 1U;
    }
}

void v5_main_page_internal_mark_toolpath_static_dirty(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->toolpath_program_generation = 0U;
    page->toolpath_program_view_generation = 0U;
    page->toolpath_program_wcs_valid = 0;
    page->toolpath_program_wcs_index = -1;
    page->toolpath_program_wcs_epoch = 0U;
    memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
    page->toolpath_program_visible = 0;
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_ac_valid = 0;
    page->toolpath_program_model_kind = 0;
    page->toolpath_program_ac_a_deg = 0.0;
    page->toolpath_program_ac_c_deg = 0.0;
    v5_toolpath_display_fit_init(&page->toolpath_fit);
}

void v5_main_page_internal_reset_toolpath_view_rotation(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->toolpath_gesture_active_count = 0;
    page->toolpath_gesture_last_distance = 0.0;
    page->toolpath_gesture_last_angle_deg = 0.0;
    page->toolpath_manual_rotate_deg = 0.0;
}

static uint64_t invalidate_toolpath_program_segment_bounds(
    lv_obj_t *line,
    const lv_point_t *points,
    unsigned int point_count)
{
    lv_area_t coords;
    lv_area_t dirty;
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int32_t padding;
    int32_t dirty_x1;
    int32_t dirty_y1;
    int32_t dirty_x2;
    int32_t dirty_y2;
    unsigned int i;

    if (!line || !points || point_count == 0U) {
        return 0U;
    }
    min_x = max_x = points[0].x;
    min_y = max_y = points[0].y;
    for (i = 1U; i < point_count; ++i) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    lv_obj_get_coords(line, &coords);
    padding = ((int32_t)lv_obj_get_style_line_width(line, 0) + 1) / 2 + 2;
    dirty_x1 = (int32_t)coords.x1 + min_x - padding;
    dirty_y1 = (int32_t)coords.y1 + min_y - padding;
    dirty_x2 = (int32_t)coords.x1 + max_x + padding;
    dirty_y2 = (int32_t)coords.y1 + max_y + padding;
    if (dirty_x1 < coords.x1) dirty_x1 = coords.x1;
    if (dirty_y1 < coords.y1) dirty_y1 = coords.y1;
    if (dirty_x2 > coords.x2) dirty_x2 = coords.x2;
    if (dirty_y2 > coords.y2) dirty_y2 = coords.y2;
    if (dirty_x1 > dirty_x2 || dirty_y1 > dirty_y2) {
        return 0U;
    }
    dirty.x1 = (lv_coord_t)dirty_x1;
    dirty.y1 = (lv_coord_t)dirty_y1;
    dirty.x2 = (lv_coord_t)dirty_x2;
    dirty.y2 = (lv_coord_t)dirty_y2;
    lv_obj_invalidate_area(line, &dirty);
    return (uint64_t)((int32_t)dirty.x2 - (int32_t)dirty.x1 + 1) *
           (uint64_t)((int32_t)dirty.y2 - (int32_t)dirty.y1 + 1);
}

static uint64_t toolpath_program_line_object_pixels(lv_obj_t *line)
{
    lv_area_t coords;
    int32_t width;
    int32_t height;
    if (!line) {
        return 0U;
    }
    lv_obj_get_coords(line, &coords);
    width = (int32_t)coords.x2 - (int32_t)coords.x1 + 1;
    height = (int32_t)coords.y2 - (int32_t)coords.y1 + 1;
    if (width <= 0 || height <= 0) {
        return 0U;
    }
    return (uint64_t)width * (uint64_t)height;
}

static void record_toolpath_program_dirty(V5MainPage *page, uint64_t pixels)
{
    if (!page || pixels == 0U) {
        return;
    }
    page->toolpath_line_last_dirty_rect_count += 1U;
    page->toolpath_line_last_dirty_pixels += pixels;
    if (pixels > page->toolpath_line_last_dirty_max_pixels) {
        page->toolpath_line_last_dirty_max_pixels = pixels;
    }
}

int v5_main_page_internal_update_toolpath_program_segment(
    V5MainPage *page,
    unsigned int segment,
    const lv_point_t *points,
    unsigned int point_count)
{
    lv_obj_t *line;
    unsigned int old_count;
    int hidden;

    if (!page || segment >= V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS ||
        point_count > V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT ||
        (point_count > 0U && !points)) {
        return 0;
    }
    line = page->toolpath_line_segments[segment];
    if (!line) {
        return 0;
    }
    hidden = lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN) ? 1 : 0;
    old_count = page->toolpath_segment_point_counts[segment];
    if (point_count < 2U) {
        page->toolpath_segment_point_counts[segment] = 0U;
        if (hidden) {
            return 0;
        }
        record_toolpath_program_dirty(page, toolpath_program_line_object_pixels(line));
        v5_main_page_internal_hide_toolpath_line(line);
        page->toolpath_line_set_points_count += 1U;
        return 1;
    }
    if (hidden || old_count != point_count) {
        memcpy(page->toolpath_segment_points[segment], points, point_count * sizeof(points[0]));
        page->toolpath_segment_point_counts[segment] = (uint16_t)point_count;
        record_toolpath_program_dirty(page, toolpath_program_line_object_pixels(line));
        lv_line_set_points(
            line,
            page->toolpath_segment_points[segment],
            (uint16_t)point_count);
        page->toolpath_line_set_points_count += 1U;
        if (hidden) {
            lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
        }
        return 1;
    }
    if (v5_main_page_internal_points_equal(
            page->toolpath_segment_points[segment], points, point_count)) {
        return 0;
    }
    record_toolpath_program_dirty(
        page,
        invalidate_toolpath_program_segment_bounds(
            line, page->toolpath_segment_points[segment], old_count));
    memcpy(page->toolpath_segment_points[segment], points, point_count * sizeof(points[0]));
    record_toolpath_program_dirty(
        page,
        invalidate_toolpath_program_segment_bounds(
            line, page->toolpath_segment_points[segment], point_count));
    return 1;
}

static void set_toolpath_program_line(
    V5MainPage *page,
    const V5ToolpathScreenPoint *screen_points,
    unsigned int point_count)
{
    unsigned int i;
    unsigned int segment = 0U;
    unsigned int start = 0U;
    int changed = 0;
    if (!page || !page->trajectory_line || !screen_points || point_count == 0U) {
        v5_main_page_internal_hide_toolpath_program_line(page);
        return;
    }
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    page->trajectory_point_count = point_count;
    if (page->trajectory_point_count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        page->trajectory_point_count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < page->trajectory_point_count; ++i) {
        page->trajectory_points[i].x = v5_main_page_internal_clamp_coord(screen_points[i].x, -32760, 32760);
        page->trajectory_points[i].y = v5_main_page_internal_clamp_coord(screen_points[i].y, -32760, 32760);
    }
    while (start < page->trajectory_point_count && segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS) {
        lv_point_t next_points[V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT];
        unsigned int local_count = 0U;
        unsigned int limit = V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT;
        while (start + local_count < page->trajectory_point_count && local_count < limit &&
               (local_count == 0U || !page->toolpath_program_break_before[start + local_count])) {
            next_points[local_count] = page->trajectory_points[start + local_count];
            ++local_count;
        }
        changed |= v5_main_page_internal_update_toolpath_program_segment(
            page, segment, next_points, local_count);
        if (start + local_count >= page->trajectory_point_count) {
            ++segment;
            break;
        }
        if (start + local_count < page->trajectory_point_count &&
            page->toolpath_program_break_before[start + local_count]) {
            start += local_count;
        } else {
            start += local_count > 1U ? local_count - 1U : local_count;
        }
        ++segment;
    }
    for (; segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++segment) {
        changed |= v5_main_page_internal_update_toolpath_program_segment(page, segment, 0, 0U);
    }
    page->toolpath_program_visible = 1;
    if (changed) {
        page->toolpath_line_rewrite_count += 1U;
    }
}

int v5_main_page_internal_main_page_project_program_with_current_fit(V5MainPage *page)
{
    unsigned int i;
    unsigned int projected_count;

    if (!page || !page->toolpath_fit.valid || page->toolpath_program_point_count == 0U) {
        return 0;
    }
    projected_count = v5_toolpath_display_project_points_with_fit(
        page->toolpath_program_project_points,
        page->toolpath_program_point_count,
        &page->toolpath_fit,
        (double)V5_TOOLPATH_W,
        (double)V5_TOOLPATH_H,
        page->toolpath_program_screen_points,
        V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT);
    if (projected_count == 0U) {
        return 0;
    }
    for (i = 0U; i < projected_count; ++i) {
        page->toolpath_program_screen_points[i] =
            v5_main_page_internal_apply_toolpath_view_transform(page, page->toolpath_program_screen_points[i]);
    }
    set_toolpath_program_line(page, page->toolpath_program_screen_points, projected_count);
    return 1;
}
