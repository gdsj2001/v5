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
    for (segment = 0U; segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS; ++segment) {
        v5_main_page_internal_hide_toolpath_line(page->toolpath_line_segments[segment]);
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

static void set_toolpath_program_line(
    V5MainPage *page,
    const V5ToolpathScreenPoint *screen_points,
    unsigned int point_count)
{
    unsigned int i;
    unsigned int segment = 0U;
    unsigned int start = 0U;
    if (!page || !page->trajectory_line || !screen_points || point_count == 0U) {
        v5_main_page_internal_hide_toolpath_program_line(page);
        return;
    }
    page->trajectory_point_count = point_count;
    if (page->trajectory_point_count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        page->trajectory_point_count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < page->trajectory_point_count; ++i) {
        page->trajectory_points[i].x = v5_main_page_internal_clamp_coord(screen_points[i].x, -32760, 32760);
        page->trajectory_points[i].y = v5_main_page_internal_clamp_coord(screen_points[i].y, -32760, 32760);
    }
    while (start < page->trajectory_point_count && segment < V5_MAIN_PAGE_TOOLPATH_DRAW_SEGMENTS) {
        unsigned int local_count = 0U;
        unsigned int limit = V5_MAIN_PAGE_TOOLPATH_SEGMENT_POINT_COUNT;
        while (start + local_count < page->trajectory_point_count && local_count < limit &&
               (local_count == 0U || !page->toolpath_program_break_before[start + local_count])) {
            page->toolpath_segment_points[segment][local_count] = page->trajectory_points[start + local_count];
            ++local_count;
        }
        if (local_count >= 2U) {
            lv_line_set_points(
                page->toolpath_line_segments[segment],
                page->toolpath_segment_points[segment],
                (uint16_t)local_count);
            lv_obj_clear_flag(page->toolpath_line_segments[segment], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(page->toolpath_line_segments[segment]);
        } else {
            v5_main_page_internal_hide_toolpath_line(page->toolpath_line_segments[segment]);
        }
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
        v5_main_page_internal_hide_toolpath_line(page->toolpath_line_segments[segment]);
    }
    if (page->toolpath_holder_line) {
        lv_obj_move_foreground(page->toolpath_holder_line);
    }
    page->toolpath_program_visible = 1;
    page->toolpath_line_rewrite_count += 1U;
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
