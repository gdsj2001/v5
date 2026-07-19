#include "v5_main_page.h"
#include "v5_main_page_internal.h"

#include <string.h>

#define V5_TOOLPATH_DIRTY_RECT_BUDGET 12U
#define V5_TOOLPATH_DIRTY_RESERVED_SLOTS 4U

static uint64_t toolpath_dirty_pixels(const lv_area_t *area)
{
    if (!area || area->x1 > area->x2 || area->y1 > area->y2) return 0U;
    return (uint64_t)((int32_t)area->x2 - (int32_t)area->x1 + 1) *
        (uint64_t)((int32_t)area->y2 - (int32_t)area->y1 + 1);
}

static void coalesce_toolpath_display_invalidations(lv_disp_t *display)
{
    const V5ToolpathViewport *viewport = v5_toolpath_viewport();
    const lv_area_t scope = {
        viewport->x, viewport->y,
        viewport->x + viewport->width - 1,
        viewport->y + viewport->height - 1};
    lv_area_t outside[LV_INV_BUF_SIZE];
    lv_area_t toolpath[LV_INV_BUF_SIZE];
    uint16_t outside_count = 0U;
    uint16_t toolpath_count = 0U;
    uint16_t toolpath_budget;
    uint16_t read_index;
    uint16_t write_index = 0U;
    if (!display || display->rendering_in_progress ||
        display->inv_p == 0U || display->inv_p > LV_INV_BUF_SIZE) return;
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
    toolpath_budget = V5_TOOLPATH_DIRTY_RECT_BUDGET;
    read_index = (uint16_t)(LV_INV_BUF_SIZE - outside_count);
    if (read_index > V5_TOOLPATH_DIRTY_RESERVED_SLOTS) {
        read_index = (uint16_t)(
            read_index - V5_TOOLPATH_DIRTY_RESERVED_SLOTS);
    }
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
                if (toolpath[second].x1 < joined.x1) {
                    joined.x1 = toolpath[second].x1;
                }
                if (toolpath[second].y1 < joined.y1) {
                    joined.y1 = toolpath[second].y1;
                }
                if (toolpath[second].x2 > joined.x2) {
                    joined.x2 = toolpath[second].x2;
                }
                if (toolpath[second].y2 > joined.y2) {
                    joined.y2 = toolpath[second].y2;
                }
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
        memmove(
            &toolpath[best_second],
            &toolpath[best_second + 1U],
            ((size_t)toolpath_count - best_second - 1U) *
                sizeof(toolpath[0]));
        --toolpath_count;
    }
    for (read_index = 0U; read_index < outside_count; ++read_index) {
        display->inv_areas[write_index++] = outside[read_index];
    }
    for (read_index = 0U; read_index < toolpath_count; ++read_index) {
        display->inv_areas[write_index++] = toolpath[read_index];
    }
    if (write_index < LV_INV_BUF_SIZE) {
        memset(
            &display->inv_areas[write_index], 0,
            (LV_INV_BUF_SIZE - write_index) *
                sizeof(display->inv_areas[0]));
    }
    memset(display->inv_area_joined, 0, sizeof(display->inv_area_joined));
    display->inv_p = write_index;
}

void v5_main_page_internal_coalesce_toolpath_invalidations(V5MainPage *page)
{
    if (!page || !page->toolpath_clip_layer) return;
    coalesce_toolpath_display_invalidations(
        lv_obj_get_disp(page->toolpath_clip_layer));
}

void v5_main_page_internal_clear_obj_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *v5_main_page_internal_make_panel(
    lv_obj_t *parent,
    int x,
    int y,
    int w,
    int h,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(
        panel, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}
