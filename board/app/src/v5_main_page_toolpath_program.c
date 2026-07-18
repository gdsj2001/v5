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
    int was_drawn;
    if (!page || !page->trajectory_line) {
        return;
    }
    was_drawn =
        page->trajectory_point_count > 0U &&
        !lv_obj_has_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN);
    if (was_drawn) {
        lv_obj_add_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN);
    }
    page->toolpath_program_wcs_valid = 0;
    page->toolpath_program_wcs_index = -1;
    page->toolpath_program_wcs_epoch = 0U;
    memset(page->toolpath_program_wcs_offset, 0, sizeof(page->toolpath_program_wcs_offset));
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_model_scene_valid = 0;
    memset(
        &page->toolpath_program_model_scene,
        0,
        sizeof(page->toolpath_program_model_scene));
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    memset(page->trajectory_break_before, 0, sizeof(page->trajectory_break_before));
    page->toolpath_program_visible = 0;
    page->trajectory_point_count = 0U;
    if (was_drawn) {
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
    page->toolpath_program_model_scene_valid = 0;
    memset(
        &page->toolpath_program_model_scene,
        0,
        sizeof(page->toolpath_program_model_scene));
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
    page->toolpath_view_generation += 1U;
    if (page->toolpath_view_generation == 0U) {
        page->toolpath_view_generation = 1U;
    }
}

static void toolpath_program_scene_draw_event_cb(lv_event_t *event)
{
    V5MainPage *page;
    lv_obj_t *scene;
    lv_draw_ctx_t *draw_ctx;
    lv_draw_line_dsc_t line_dsc;
    lv_area_t coords;
    const lv_area_t *clip;
    lv_coord_t x_offset;
    lv_coord_t y_offset;
    lv_coord_t padding;
    unsigned int i;

    if (!event || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    page = (V5MainPage *)lv_event_get_user_data(event);
    scene = lv_event_get_target(event);
    draw_ctx = lv_event_get_draw_ctx(event);
    if (!page || !scene || !draw_ctx || !page->toolpath_program_visible ||
        page->trajectory_point_count < 2U) {
        return;
    }
    lv_obj_get_coords(scene, &coords);
    x_offset = coords.x1 - lv_obj_get_scroll_x(scene);
    y_offset = coords.y1 - lv_obj_get_scroll_y(scene);
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(scene, LV_PART_MAIN, &line_dsc);
    clip = draw_ctx->clip_area;
    padding = (line_dsc.width + 1) / 2 + 1;
    for (i = 1U; i < page->trajectory_point_count; ++i) {
        lv_point_t start;
        lv_point_t end;
        if (page->trajectory_break_before[i]) {
            continue;
        }
        start.x = page->trajectory_points[i - 1U].x + x_offset;
        start.y = page->trajectory_points[i - 1U].y + y_offset;
        end.x = page->trajectory_points[i].x + x_offset;
        end.y = page->trajectory_points[i].y + y_offset;
        if (clip &&
            ((start.x < end.x ? end.x : start.x) < clip->x1 - padding ||
             (start.x < end.x ? start.x : end.x) > clip->x2 + padding ||
             (start.y < end.y ? end.y : start.y) < clip->y1 - padding ||
             (start.y < end.y ? start.y : end.y) > clip->y2 + padding)) {
            continue;
        }
        lv_draw_line(draw_ctx, &line_dsc, &start, &end);
    }
}

lv_obj_t *v5_main_page_internal_create_toolpath_program_scene(
    V5MainPage *page,
    lv_obj_t *parent)
{
    lv_obj_t *scene;
    if (!page || !parent) {
        return 0;
    }
    scene = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(scene);
    lv_obj_set_pos(scene, 0, 0);
    lv_obj_set_size(scene, V5_TOOLPATH_W, V5_TOOLPATH_H);
    lv_obj_set_style_bg_opa(scene, LV_OPA_TRANSP, 0);
    lv_obj_set_style_line_color(scene, v5_main_page_internal_rgb(255, 214, 64), 0);
    lv_obj_set_style_line_width(scene, V5_TOOLPATH_PROGRAM_LINE_WIDTH, 0);
    lv_obj_set_style_line_opa(scene, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scene, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scene, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        scene,
        toolpath_program_scene_draw_event_cb,
        LV_EVENT_DRAW_MAIN,
        page);
    return scene;
}

static int toolpath_program_segment_bounds(
    lv_obj_t *scene,
    const lv_point_t *points,
    unsigned int point_count,
    lv_area_t *dirty)
{
    lv_area_t coords;
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

    if (!scene || !points || point_count == 0U || !dirty) {
        return 0;
    }
    min_x = max_x = points[0].x;
    min_y = max_y = points[0].y;
    for (i = 1U; i < point_count; ++i) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    lv_obj_get_coords(scene, &coords);
    padding = ((int32_t)lv_obj_get_style_line_width(scene, 0) + 1) / 2 + 2;
    dirty_x1 = (int32_t)coords.x1 + min_x - padding;
    dirty_y1 = (int32_t)coords.y1 + min_y - padding;
    dirty_x2 = (int32_t)coords.x1 + max_x + padding;
    dirty_y2 = (int32_t)coords.y1 + max_y + padding;
    if (dirty_x1 < coords.x1) dirty_x1 = coords.x1;
    if (dirty_y1 < coords.y1) dirty_y1 = coords.y1;
    if (dirty_x2 > coords.x2) dirty_x2 = coords.x2;
    if (dirty_y2 > coords.y2) dirty_y2 = coords.y2;
    if (dirty_x1 > dirty_x2 || dirty_y1 > dirty_y2) {
        return 0;
    }
    dirty->x1 = (lv_coord_t)dirty_x1;
    dirty->y1 = (lv_coord_t)dirty_y1;
    dirty->x2 = (lv_coord_t)dirty_x2;
    dirty->y2 = (lv_coord_t)dirty_y2;
    return 1;
}

static uint64_t toolpath_program_area_pixels(const lv_area_t *area)
{
    if (!area || area->x1 > area->x2 || area->y1 > area->y2) {
        return 0U;
    }
    return (uint64_t)((int32_t)area->x2 - (int32_t)area->x1 + 1) *
           (uint64_t)((int32_t)area->y2 - (int32_t)area->y1 + 1);
}

static lv_area_t toolpath_program_area_union(const lv_area_t *first, const lv_area_t *second)
{
    lv_area_t joined = *first;
    if (second->x1 < joined.x1) joined.x1 = second->x1;
    if (second->y1 < joined.y1) joined.y1 = second->y1;
    if (second->x2 > joined.x2) joined.x2 = second->x2;
    if (second->y2 > joined.y2) joined.y2 = second->y2;
    return joined;
}

#define V5_TOOLPATH_PROGRAM_DIRTY_RECT_BUDGET 2U

typedef struct {
    lv_area_t areas[V5_TOOLPATH_PROGRAM_DIRTY_RECT_BUDGET + 1U];
    unsigned int count;
} V5ToolpathProgramDirtySet;

static void toolpath_program_dirty_add(
    V5ToolpathProgramDirtySet *dirty,
    const lv_area_t *area)
{
    unsigned int first;
    unsigned int second;
    unsigned int best_first = 0U;
    unsigned int best_second = 1U;
    int64_t best_extra = 0;
    int found = 0;
    lv_area_t best_joined;

    if (!dirty || !area || toolpath_program_area_pixels(area) == 0U) {
        return;
    }
    dirty->areas[dirty->count++] = *area;
    if (dirty->count <= V5_TOOLPATH_PROGRAM_DIRTY_RECT_BUDGET) {
        return;
    }
    best_joined = toolpath_program_area_union(&dirty->areas[0], &dirty->areas[1]);
    for (first = 0U; first + 1U < dirty->count; ++first) {
        for (second = first + 1U; second < dirty->count; ++second) {
            const lv_area_t joined =
                toolpath_program_area_union(&dirty->areas[first], &dirty->areas[second]);
            const int64_t extra =
                (int64_t)toolpath_program_area_pixels(&joined) -
                (int64_t)toolpath_program_area_pixels(&dirty->areas[first]) -
                (int64_t)toolpath_program_area_pixels(&dirty->areas[second]);
            if (!found || extra < best_extra) {
                found = 1;
                best_extra = extra;
                best_first = first;
                best_second = second;
                best_joined = joined;
            }
        }
    }
    dirty->areas[best_first] = best_joined;
    memmove(
        &dirty->areas[best_second],
        &dirty->areas[best_second + 1U],
        (dirty->count - best_second - 1U) * sizeof(dirty->areas[0]));
    --dirty->count;
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

static int toolpath_program_snapshot_equal(
    const V5MainPage *page,
    const lv_point_t *points,
    unsigned int point_count)
{
    unsigned int i;
    if (!page || !points || page->trajectory_point_count != point_count) {
        return 0;
    }
    for (i = 0U; i < point_count; ++i) {
        if (page->trajectory_points[i].x != points[i].x ||
            page->trajectory_points[i].y != points[i].y ||
            page->trajectory_break_before[i] != page->toolpath_program_break_before[i]) {
            return 0;
        }
    }
    return 1;
}

static void set_toolpath_program_scene(
    V5MainPage *page,
    const V5ToolpathScreenPoint *screen_points,
    unsigned int point_count)
{
    V5ToolpathProgramDirtySet dirty = {0};
    lv_point_t next_points[V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT];
    unsigned int old_count;
    unsigned int dirty_index;
    unsigned int i;
    if (!page || !page->trajectory_line || !screen_points || point_count == 0U) {
        v5_main_page_internal_hide_toolpath_program_line(page);
        return;
    }
    if (point_count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        point_count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < point_count; ++i) {
        next_points[i].x = v5_main_page_internal_clamp_coord(screen_points[i].x, -32760, 32760);
        next_points[i].y = v5_main_page_internal_clamp_coord(screen_points[i].y, -32760, 32760);
    }
    if (toolpath_program_snapshot_equal(page, next_points, point_count)) {
        page->toolpath_program_visible = 1;
        if (lv_obj_has_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    old_count = page->trajectory_point_count;
    for (i = 1U; i < (old_count > point_count ? old_count : point_count); ++i) {
        const int old_drawn =
            i < old_count && !page->trajectory_break_before[i];
        const int new_drawn =
            i < point_count && !page->toolpath_program_break_before[i];
        int changed = old_drawn != new_drawn;
        lv_area_t area;
        if (old_drawn && new_drawn &&
            (page->trajectory_points[i - 1U].x != next_points[i - 1U].x ||
             page->trajectory_points[i - 1U].y != next_points[i - 1U].y ||
             page->trajectory_points[i].x != next_points[i].x ||
             page->trajectory_points[i].y != next_points[i].y)) {
            changed = 1;
        }
        if (!changed) {
            continue;
        }
        if (old_drawn && toolpath_program_segment_bounds(
                page->trajectory_line,
                &page->trajectory_points[i - 1U],
                2U,
                &area)) {
            toolpath_program_dirty_add(&dirty, &area);
        }
        if (new_drawn && toolpath_program_segment_bounds(
                page->trajectory_line,
                &next_points[i - 1U],
                2U,
                &area)) {
            toolpath_program_dirty_add(&dirty, &area);
        }
    }
    memcpy(page->trajectory_points, next_points, point_count * sizeof(next_points[0]));
    memcpy(
        page->trajectory_break_before,
        page->toolpath_program_break_before,
        point_count * sizeof(page->trajectory_break_before[0]));
    page->trajectory_point_count = point_count;
    page->toolpath_program_visible = 1;
    if (lv_obj_has_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN);
    }
    for (dirty_index = 0U; dirty_index < dirty.count; ++dirty_index) {
        lv_obj_invalidate_area(page->trajectory_line, &dirty.areas[dirty_index]);
        record_toolpath_program_dirty(
            page,
            toolpath_program_area_pixels(&dirty.areas[dirty_index]));
    }
    v5_main_page_internal_coalesce_toolpath_invalidations(page);
    page->toolpath_line_rewrite_count += 1U;
}

int v5_main_page_internal_main_page_project_program_with_current_fit(V5MainPage *page)
{
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
    v5_main_page_internal_apply_toolpath_view_transform_points(
        page, page->toolpath_program_screen_points, projected_count);
    set_toolpath_program_scene(page, page->toolpath_program_screen_points, projected_count);
    return 1;
}

int v5_main_page_internal_main_page_project_program_fused(V5MainPage *page)
{
    V5ToolpathViewTransform view_transform;
    unsigned int count;
    unsigned int i;

    if (!page || !page->toolpath_fit.valid ||
        page->toolpath_program_point_count == 0U ||
        !v5_main_page_internal_main_page_capture_program_model_scene(page)) {
        return 0;
    }
    count = page->toolpath_program_point_count;
    if (count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    v5_main_page_internal_prepare_toolpath_view_transform(
        page, &view_transform);
    for (i = 0U; i < count; ++i) {
        page->toolpath_program_project_points[i] =
            page->toolpath_program_points[i];
        if (page->native_readback.rtcp_enabled &&
            !v5_main_page_model_scene_transform_world_point(
                &page->toolpath_program_model_scene,
                page->toolpath_program_project_points[i].axis)) {
            page->toolpath_program_model_scene_valid = 0;
            memset(
                &page->toolpath_program_model_scene,
                0,
                sizeof(page->toolpath_program_model_scene));
            return 0;
        }
        if (!v5_toolpath_display_project_world_point(
                page->toolpath_program_project_points[i].axis,
                &page->toolpath_fit,
                (double)V5_TOOLPATH_W,
                (double)V5_TOOLPATH_H,
                &page->toolpath_program_screen_points[i])) {
            return 0;
        }
        page->toolpath_program_screen_points[i] =
            v5_main_page_internal_apply_toolpath_view_transform_prepared(
                page->toolpath_program_screen_points[i],
                &view_transform);
    }
    if (page->native_readback.rtcp_enabled) {
        page->toolpath_program_rtcp_transform_count += 1U;
    }
    page->toolpath_program_fused_frame_count += 1U;
    set_toolpath_program_scene(
        page, page->toolpath_program_screen_points, count);
    return 1;
}
