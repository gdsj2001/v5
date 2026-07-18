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

#define V5_TOOLPATH_DIRTY_RECT_BUDGET 12U
#define V5_TOOLPATH_DIRTY_RESERVED_SLOTS 4U

static uint64_t toolpath_dirty_pixels(const lv_area_t *area)
{
    if (!area || area->x1 > area->x2 || area->y1 > area->y2) {
        return 0U;
    }
    return (uint64_t)((int32_t)area->x2 - (int32_t)area->x1 + 1) *
           (uint64_t)((int32_t)area->y2 - (int32_t)area->y1 + 1);
}

static void coalesce_toolpath_display_invalidations(lv_disp_t *display)
{
    const lv_area_t scope = {V5_TOOLPATH_X, V5_TOOLPATH_Y,
        V5_TOOLPATH_X + V5_TOOLPATH_W - 1, V5_TOOLPATH_Y + V5_TOOLPATH_H - 1};
    lv_area_t outside[LV_INV_BUF_SIZE];
    lv_area_t toolpath[LV_INV_BUF_SIZE];
    uint16_t outside_count = 0U;
    uint16_t toolpath_count = 0U;
    uint16_t toolpath_budget;
    uint16_t read_index;
    uint16_t write_index = 0U;

    if (!display || display->rendering_in_progress ||
        display->inv_p == 0U || display->inv_p > LV_INV_BUF_SIZE) {
        return;
    }
    for (read_index = 0U; read_index < display->inv_p; ++read_index) {
        if (display->inv_area_joined[read_index] != 0U) return;
        if (display->inv_areas[read_index].x1 >= scope.x1 &&
            display->inv_areas[read_index].x2 <= scope.x2 &&
            display->inv_areas[read_index].y1 >= scope.y1 &&
            display->inv_areas[read_index].y2 <= scope.y2) {
            toolpath[toolpath_count++] = display->inv_areas[read_index];
        } else {
            outside[outside_count++] = display->inv_areas[read_index];
        }
    }
    if (toolpath_count == 0U) return;

    /* Preserve genuinely separate moving primitives.  Only when the bounded
     * queue is exceeded do we choose the pair with the smallest additional
     * pixel cost (overlapping/contained areas naturally win that choice). */
    toolpath_budget = V5_TOOLPATH_DIRTY_RECT_BUDGET;
    read_index = (uint16_t)(LV_INV_BUF_SIZE - outside_count);
    if (read_index > V5_TOOLPATH_DIRTY_RESERVED_SLOTS)
        read_index = (uint16_t)(read_index - V5_TOOLPATH_DIRTY_RESERVED_SLOTS);
    if (toolpath_budget > read_index) toolpath_budget = read_index;
    if (toolpath_budget == 0U) toolpath_budget = 1U;
    while (toolpath_count > toolpath_budget) {
        uint16_t first;
        uint16_t second;
        uint16_t best_first = 0U;
        uint16_t best_second = 1U;
        int64_t best_extra = 0;
        int found = 0;
        lv_area_t best_joined = toolpath[0];
        for (first = 0U; first + 1U < toolpath_count; ++first) {
            for (second = first + 1U; second < toolpath_count; ++second) {
                lv_area_t joined = toolpath[first];
                int64_t extra;
                if (toolpath[second].x1 < joined.x1) joined.x1 = toolpath[second].x1;
                if (toolpath[second].y1 < joined.y1) joined.y1 = toolpath[second].y1;
                if (toolpath[second].x2 > joined.x2) joined.x2 = toolpath[second].x2;
                if (toolpath[second].y2 > joined.y2) joined.y2 = toolpath[second].y2;
                extra = (int64_t)toolpath_dirty_pixels(&joined) -
                    (int64_t)toolpath_dirty_pixels(&toolpath[first]) -
                    (int64_t)toolpath_dirty_pixels(&toolpath[second]);
                if (!found || extra < best_extra) {
                    found = 1;
                    best_extra = extra;
                    best_first = first;
                    best_second = second;
                    best_joined = joined;
                }
            }
        }
        toolpath[best_first] = best_joined;
        memmove(&toolpath[best_second], &toolpath[best_second + 1U],
            ((size_t)toolpath_count - best_second - 1U) * sizeof(toolpath[0]));
        --toolpath_count;
    }

    for (read_index = 0U; read_index < outside_count; ++read_index) {
        display->inv_areas[write_index++] = outside[read_index];
    }
    for (read_index = 0U; read_index < toolpath_count; ++read_index) {
        display->inv_areas[write_index++] = toolpath[read_index];
    }
    if (write_index < LV_INV_BUF_SIZE) {
        memset(&display->inv_areas[write_index], 0,
            (LV_INV_BUF_SIZE - write_index) * sizeof(display->inv_areas[0]));
    }
    memset(display->inv_area_joined, 0, sizeof(display->inv_area_joined));
    display->inv_p = write_index;
}

void v5_main_page_internal_coalesce_toolpath_invalidations(V5MainPage *page)
{
    if (!page || !page->toolpath_clip_layer) return;
    coalesce_toolpath_display_invalidations(lv_obj_get_disp(page->toolpath_clip_layer));
}

void v5_main_page_internal_clear_obj_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}



lv_obj_t *v5_main_page_internal_make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

lv_obj_t *v5_main_page_internal_make_toolpath_v3_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t br, uint8_t bg, uint8_t bb)
{
    lv_obj_t *dot = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_bg_color(dot, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, v5_main_page_internal_rgb(br, bg, bb), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

lv_obj_t *v5_main_page_internal_make_toolpath_v3_center_dot(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *dot = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(dot);
    lv_obj_set_size(dot, 2, 2);
    lv_obj_set_style_radius(dot, 1, 0);
    lv_obj_set_style_bg_color(dot, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return dot;
}

void v5_main_page_internal_set_toolpath_v3_dot_center(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 3;
    if (!dot) {
        return;
    }
    coalesce_toolpath_display_invalidations(lv_obj_get_disp(dot));
    if (!valid || !point) {
        v5_main_page_internal_add_hidden_flag_if_visible(dot);
        return;
    }
    x = V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(point->x, half, V5_TOOLPATH_W - half);
    y = V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(point->y, half, V5_TOOLPATH_H - half);
    v5_main_page_internal_set_obj_pos_if_changed(dot, x - half, y - half);
    v5_main_page_internal_clear_hidden_flag_if_hidden(dot);
}


void v5_main_page_internal_set_toolpath_v3_center_dot(lv_obj_t *dot, const V5ToolpathScreenPoint *point, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 1;
    if (!dot) {
        return;
    }
    coalesce_toolpath_display_invalidations(lv_obj_get_disp(dot));
    if (!valid || !point) {
        v5_main_page_internal_add_hidden_flag_if_visible(dot);
        return;
    }
    x = V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(point->x, half, V5_TOOLPATH_W - half);
    y = V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(point->y, half, V5_TOOLPATH_H - half);
    v5_main_page_internal_set_obj_pos_if_changed(dot, x - half, y - half);
    v5_main_page_internal_clear_hidden_flag_if_hidden(dot);
}

static const char *toolpath_wcs_name_for_index(unsigned int index)
{
    static const char *const names[V5_MAIN_PAGE_TOOLPATH_WCS_COUNT] = {
        "G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};
    return index < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT ? names[index] : "";
}

lv_obj_t *v5_main_page_internal_make_toolpath_v3_line(lv_obj_t *parent, uint8_t r, uint8_t g, uint8_t b, uint8_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_size(line, V5_TOOLPATH_W, V5_TOOLPATH_H);
    lv_obj_set_style_line_color(line, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

void v5_main_page_internal_hide_toolpath_line(lv_obj_t *line)
{
    if (line) {
        coalesce_toolpath_display_invalidations(lv_obj_get_disp(line));
    }
    if (line && !lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        lv_line_set_points(line, 0, 0);
    }
}

static lv_point_t toolpath_local_point(const V5ToolpathScreenPoint *point)
{
    lv_point_t out;
    out.x = v5_main_page_internal_clamp_coord(point ? point->x : 0.0, 0, V5_TOOLPATH_W);
    out.y = v5_main_page_internal_clamp_coord(point ? point->y : 0.0, 0, V5_TOOLPATH_H);
    return out;
}

static void invalidate_toolpath_line_points(lv_obj_t *line, const lv_point_t *points, unsigned int point_count)
{
    lv_area_t coords;
    lv_area_t dirty;
    lv_coord_t padding;
    lv_coord_t min_x;
    lv_coord_t max_x;
    lv_coord_t min_y;
    lv_coord_t max_y;
    unsigned int i;
    if (!line || !points || point_count == 0U) {
        return;
    }
    lv_obj_get_coords(line, &coords);
    padding = (lv_obj_get_style_line_width(line, 0) + 1) / 2 + 2;
    min_x = max_x = points[0].x;
    min_y = max_y = points[0].y;
    for (i = 1U; i < point_count; ++i) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    dirty.x1 = coords.x1 + min_x - padding;
    dirty.y1 = coords.y1 + min_y - padding;
    dirty.x2 = coords.x1 + max_x + padding;
    dirty.y2 = coords.y1 + max_y + padding;
    lv_obj_invalidate_area(line, &dirty);
}

int v5_main_page_internal_clip_toolpath_segment(V5ToolpathScreenPoint *start, V5ToolpathScreenPoint *end);

void v5_main_page_internal_set_toolpath_axis_line(lv_obj_t *line, lv_point_t points[2], const V5ToolpathScreenPoint *start, const V5ToolpathScreenPoint *end, int valid)
{
    V5ToolpathScreenPoint clipped_start;
    V5ToolpathScreenPoint clipped_end;
    if (!line || !points) {
        return;
    }
    coalesce_toolpath_display_invalidations(lv_obj_get_disp(line));
    if (!valid || !start || !end) {
        v5_main_page_internal_hide_toolpath_line(line);
        return;
    }
    clipped_start = *start;
    clipped_end = *end;
    if (!v5_main_page_internal_clip_toolpath_segment(&clipped_start, &clipped_end)) {
        v5_main_page_internal_hide_toolpath_line(line);
        return;
    }
    {
        lv_point_t next[2];
        next[0] = toolpath_local_point(&clipped_start);
        next[1] = toolpath_local_point(&clipped_end);
        if (lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN)) {
            points[0] = next[0];
            points[1] = next[1];
            lv_line_set_points(line, points, 2);
        } else if (!v5_main_page_internal_points_equal(points, next, 2U)) {
            invalidate_toolpath_line_points(line, points, 2U);
            points[0] = next[0];
            points[1] = next[1];
            invalidate_toolpath_line_points(line, points, 2U);
        }
    }
    v5_main_page_internal_clear_hidden_flag_if_hidden(line);
}

int v5_main_page_internal_main_page_tool_length_mm(const V5MainPage *page, double *out)
{
    if (!page || !out || !v5_native_readback_tool_length_known(&page->native_readback) ||
        !isfinite(page->native_readback.tool_length_mm)) {
        return 0;
    }
    *out = page->native_readback.tool_length_mm;
    return 1;
}

static int project_dynamic_scene_point(
    const V5MainPage *page,
    const V5ToolpathViewTransform *view_transform,
    const double world[V5_STATUS_AXIS_COUNT],
    V5ToolpathScreenPoint *point)
{
    if (!page || !view_transform || !world || !point ||
        !page->toolpath_fit.valid ||
        !v5_toolpath_display_project_world_point(
            world,
            &page->toolpath_fit,
            (double)V5_TOOLPATH_W,
            (double)V5_TOOLPATH_H,
            point)) {
        return 0;
    }
    *point = v5_main_page_internal_apply_toolpath_view_transform_prepared(
        *point, view_transform);
    return 1;
}

void v5_main_page_internal_build_dynamic_scene(
    V5MainPage *page,
    const V5UiStatusView *status,
    V5MainPageDynamicScene *scene)
{
    V5ToolpathViewTransform view_transform;
    unsigned int generation;
    double world[V5_STATUS_AXIS_COUNT];
    double tool_len = 0.0;

    if (!page || !scene) {
        return;
    }
    generation = page->toolpath_dynamic_scene.generation + 1U;
    if (generation == 0U) {
        generation = 1U;
    }
    memset(scene, 0, sizeof(*scene));
    scene->generation = generation;
    page->toolpath_dynamic_scene_build_count += 1U;
    if (!status) {
        return;
    }
    v5_main_page_internal_prepare_toolpath_view_transform(
        page, &view_transform);
    if ((status->valid_mask & V5_STATUS_VALID_MCS) != 0U) {
        scene->mcs_valid =
            project_dynamic_scene_point(
                page,
                &view_transform,
                status->mcs,
                &scene->mcs_point);
    }
    if ((status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U) {
        memcpy(world, status->cmd_mcs, sizeof(world));
        if (v5_main_page_internal_main_page_tool_length_mm(page, &tool_len)) {
            world[2] -= tool_len;
        }
        scene->cmd_tip_valid =
            project_dynamic_scene_point(
                page,
                &view_transform,
                world,
                &scene->cmd_tip_point);
    }
    if (!scene->mcs_valid ||
        !v5_main_page_internal_main_page_tool_length_mm(page, &tool_len) ||
        fabs(tool_len) <= 1.0e-9) {
        return;
    }
    memcpy(world, status->mcs, sizeof(world));
    world[2] -= tool_len;
    scene->holder_end_valid =
        project_dynamic_scene_point(
            page,
            &view_transform,
            world,
            &scene->holder_end_point);
}

void v5_main_page_internal_apply_dynamic_scene(
    V5MainPage *page,
    const V5MainPageDynamicScene *scene)
{
    if (!page || !scene) {
        return;
    }
    v5_main_page_internal_set_toolpath_v3_dot_center(
        page->toolpath_microkernel_marker_dot,
        &scene->cmd_tip_point,
        scene->cmd_tip_valid);
    v5_main_page_internal_set_toolpath_v3_dot_center(
        page->toolpath_holder_marker_line,
        &scene->mcs_point,
        scene->mcs_valid);
    if (scene->mcs_valid && scene->holder_end_valid) {
        v5_main_page_internal_set_toolpath_axis_line(
            page->toolpath_holder_line,
            page->toolpath_holder_points,
            &scene->mcs_point,
            &scene->holder_end_point,
            1);
    } else {
        v5_main_page_internal_hide_toolpath_line(
            page->toolpath_holder_line);
    }
    if (!scene->mcs_valid) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
    }
}

void v5_main_page_internal_set_toolpath_origin_cross(lv_obj_t *line, lv_point_t points[5], const V5ToolpathScreenPoint *origin, int valid)
{
    lv_coord_t x;
    lv_coord_t y;
    const lv_coord_t half = 3;
    if (!line || !points) {
        return;
    }
    coalesce_toolpath_display_invalidations(lv_obj_get_disp(line));
    if (!valid || !origin) {
        v5_main_page_internal_hide_toolpath_line(line);
        return;
    }
    x = v5_main_page_internal_clamp_coord(origin->x, half, V5_TOOLPATH_W - half);
    y = v5_main_page_internal_clamp_coord(origin->y, half, V5_TOOLPATH_H - half);
    {
        lv_point_t next[5];
        next[0].x = x;
        next[0].y = y - half;
        next[1].x = x + half;
        next[1].y = y;
        next[2].x = x;
        next[2].y = y + half;
        next[3].x = x - half;
        next[3].y = y;
        next[4].x = x;
        next[4].y = y - half;
        if (lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN)) {
            memcpy(points, next, sizeof(next));
            lv_line_set_points(line, points, 5);
        } else if (!v5_main_page_internal_points_equal(points, next, 5U)) {
            invalidate_toolpath_line_points(line, points, 5U);
            memcpy(points, next, sizeof(next));
            invalidate_toolpath_line_points(line, points, 5U);
        }
    }
    v5_main_page_internal_clear_hidden_flag_if_hidden(line);
}

void v5_main_page_internal_hide_toolpath_model_geometry(V5MainPage *page)
{
    if (!page) {
        return;
    }
    v5_main_page_internal_hide_toolpath_line(page->toolpath_model_primary_axis_line);
    v5_main_page_internal_hide_toolpath_line(page->toolpath_model_child_axis_line);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_model_primary_center_line);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_model_child_center_line);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_model_primary_label);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_model_child_label);
}


void v5_main_page_internal_hide_toolpath_program_wcs_objects(V5MainPage *page)
{
    unsigned int wcs;
    unsigned int axis;
    if (!page) {
        return;
    }
    for (wcs = 0U; wcs < V5_MAIN_PAGE_TOOLPATH_WCS_COUNT; ++wcs) {
        v5_main_page_internal_hide_toolpath_line(page->toolpath_program_wcs_origin_lines[wcs]);
        for (axis = 0U; axis < 3U; ++axis) {
            v5_main_page_internal_hide_toolpath_line(page->toolpath_program_wcs_axis_lines[wcs][axis]);
        }
        if (page->toolpath_program_wcs_labels[wcs]) {
            v5_main_page_internal_set_label_text_if_changed(page->toolpath_program_wcs_labels[wcs], "");
            v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_program_wcs_labels[wcs]);
        }
    }
}


V5ToolpathScreenPoint v5_main_page_internal_toolpath_scaffold_point(double x, double y)
{
    V5ToolpathScreenPoint point;
    point.x = x;
    point.y = y;
    return point;
}

int v5_main_page_internal_clip_toolpath_segment(V5ToolpathScreenPoint *start, V5ToolpathScreenPoint *end)
{
    const double xmin = 0.0;
    const double ymin = 0.0;
    const double xmax = (double)V5_TOOLPATH_W;
    const double ymax = (double)V5_TOOLPATH_H;
    const double dx = end->x - start->x;
    const double dy = end->y - start->y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {start->x - xmin, xmax - start->x, start->y - ymin, ymax - start->y};
    double u1 = 0.0;
    double u2 = 1.0;
    int i;

    for (i = 0; i < 4; ++i) {
        if (fabs(p[i]) < 1.0e-9) {
            if (q[i] < 0.0) {
                return 0;
            }
        } else {
            const double r = q[i] / p[i];
            if (p[i] < 0.0) {
                if (r > u2) {
                    return 0;
                }
                if (r > u1) {
                    u1 = r;
                }
            } else {
                if (r < u1) {
                    return 0;
                }
                if (r < u2) {
                    u2 = r;
                }
            }
        }
    }

    if (u2 < 1.0) {
        end->x = start->x + (u2 * dx);
        end->y = start->y + (u2 * dy);
    }
    if (u1 > 0.0) {
        start->x = start->x + (u1 * dx);
        start->y = start->y + (u1 * dy);
    }
    return 1;
}
