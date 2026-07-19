#include "v5_main_page.h"
#include "v5_main_page_internal.h"

#include <string.h>

static void invalidate_scene_dirty(
    V5MainPage *page,
    const V5StatusDisplayScene *scene)
{
    lv_area_t coords;
    lv_area_t dirty;
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
    uint64_t pixels;
    if (!page || !page->trajectory_line || !scene ||
        scene->dirty_x2 < scene->dirty_x1 ||
        scene->dirty_y2 < scene->dirty_y1) return;
    lv_obj_get_coords(page->trajectory_line, &coords);
    x1 = (int32_t)coords.x1 + scene->dirty_x1 - 7;
    y1 = (int32_t)coords.y1 + scene->dirty_y1 - 7;
    x2 = (int32_t)coords.x1 + scene->dirty_x2 + 7;
    y2 = (int32_t)coords.y1 + scene->dirty_y2 + 7;
    if (x1 < coords.x1) x1 = coords.x1;
    if (y1 < coords.y1) y1 = coords.y1;
    if (x2 > coords.x2) x2 = coords.x2;
    if (y2 > coords.y2) y2 = coords.y2;
    if (x1 > x2 || y1 > y2) return;
    dirty.x1 = (lv_coord_t)x1;
    dirty.y1 = (lv_coord_t)y1;
    dirty.x2 = (lv_coord_t)x2;
    dirty.y2 = (lv_coord_t)y2;
    lv_obj_invalidate_area(page->trajectory_line, &dirty);
    pixels = (uint64_t)(x2 - x1 + 1) * (uint64_t)(y2 - y1 + 1);
    page->toolpath_line_last_dirty_rect_count += 1U;
    page->toolpath_line_last_dirty_pixels += pixels;
    if (pixels > page->toolpath_line_last_dirty_max_pixels) {
        page->toolpath_line_last_dirty_max_pixels = pixels;
    }
}

static void hide_scene_labels(V5MainPage *page)
{
    if (!page) return;
    v5_main_page_internal_add_hidden_flag_if_visible(
        page->toolpath_model_primary_label);
    v5_main_page_internal_add_hidden_flag_if_visible(
        page->toolpath_model_child_label);
    v5_main_page_internal_add_hidden_flag_if_visible(
        page->toolpath_wcs_label);
}

void v5_main_page_internal_hide_toolpath_unproven_geometry(V5MainPage *page)
{
    if (!page) return;
    if (page->toolpath_display_scene_valid && page->toolpath_display_scene) {
        invalidate_scene_dirty(page, page->toolpath_display_scene);
    }
    page->toolpath_display_scene_valid = 0;
    page->toolpath_display_scene = NULL;
    v5_main_page_internal_hide_toolpath_program_line(page);
    hide_scene_labels(page);
}

static void apply_segment_labels(
    V5MainPage *page,
    const V5StatusDisplayScene *scene,
    unsigned char model_seen[2])
{
    unsigned int i;
    for (i = 0U; i < scene->segment_count; ++i) {
        const V5StatusSceneSegment *segment = &scene->segments[i];
        if (segment->role == V5_STATUS_SCENE_SEGMENT_MODEL_AXIS &&
            segment->index < 2U) {
            lv_obj_t *label = segment->index ?
                page->toolpath_model_child_label :
                page->toolpath_model_primary_label;
            char text[2] = {
                (char)(segment->index ?
                    scene->child_axis : scene->primary_axis), '\0'};
            model_seen[segment->index] = 1U;
            v5_main_page_internal_set_label_text_if_changed(label, text);
            v5_main_page_internal_set_obj_pos_if_changed(
                label,
                v5_toolpath_viewport()->x + v5_main_page_internal_clamp_coord(
                    segment->end.x + 4.0, 0, v5_toolpath_viewport()->width - 24),
                v5_toolpath_viewport()->y + v5_main_page_internal_clamp_coord(
                    segment->end.y - 12.0, 0, v5_toolpath_viewport()->height - 22));
            v5_main_page_internal_clear_hidden_flag_if_hidden(label);
        }
    }
}

static void apply_marker_labels(
    V5MainPage *page,
    const V5StatusDisplayScene *scene,
    int *wcs_seen)
{
    unsigned int i;
    for (i = 0U; i < scene->marker_count; ++i) {
        const V5StatusSceneMarker *marker = &scene->markers[i];
        if (marker->role == V5_STATUS_SCENE_MARKER_WCS_ORIGIN) {
            *wcs_seen = 1;
            v5_main_page_internal_set_label_text_if_changed(
                page->toolpath_wcs_label,
                v5_main_page_internal_main_page_wcs_code(
                    &page->native_readback));
            v5_main_page_internal_set_obj_pos_if_changed(
                page->toolpath_wcs_label,
                v5_toolpath_viewport()->x + v5_main_page_internal_clamp_coord(
                    marker->point.x + 5.0, 0, v5_toolpath_viewport()->width - 36),
                v5_toolpath_viewport()->y + v5_main_page_internal_clamp_coord(
                    marker->point.y - 14.0, 0, v5_toolpath_viewport()->height - 18));
            v5_main_page_internal_clear_hidden_flag_if_hidden(
                page->toolpath_wcs_label);
        }
    }
}

void v5_main_page_internal_apply_display_scene(
    V5MainPage *page,
    const V5StatusDisplayScene *scene)
{
    unsigned char model_seen[2] = {0};
    int wcs_seen = 0;
    unsigned int i;
    if (!page || !scene ||
        (scene->flags & V5_STATUS_SCENE_FLAG_VALID) == 0U) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
        return;
    }
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    if (page->toolpath_display_scene_valid && page->toolpath_display_scene) {
        invalidate_scene_dirty(page, page->toolpath_display_scene);
    }
    page->toolpath_display_scene = scene;
    page->toolpath_display_scene_valid = 1;
    (void)v5_main_page_internal_apply_program_display_scene(page, scene);
    invalidate_scene_dirty(page, scene);
    apply_segment_labels(page, scene, model_seen);
    apply_marker_labels(page, scene, &wcs_seen);
    for (i = 0U; i < 2U; ++i) {
        if (!model_seen[i]) {
            v5_main_page_internal_add_hidden_flag_if_visible(
                i ? page->toolpath_model_child_label :
                    page->toolpath_model_primary_label);
        }
    }
    if (!wcs_seen) {
        v5_main_page_internal_add_hidden_flag_if_visible(
            page->toolpath_wcs_label);
    }
    v5_main_page_internal_coalesce_toolpath_invalidations(page);
}

void v5_main_page_internal_update_toolpath_state_lines(
    V5MainPage *page,
    const V5UiStatusView *status)
{
    if (!status ||
        (status->valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) == 0U) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
        return;
    }
    v5_main_page_internal_apply_display_scene(
        page, status->display_scene);
}
