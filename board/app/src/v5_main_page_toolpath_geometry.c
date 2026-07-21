#include "v5_main_page.h"
#include "v5_main_page_internal.h"

#include <math.h>
#include <string.h>

static int segment_in_layer(uint16_t role, uint32_t layer)
{
    if (layer == V5_STATUS_SCENE_FLAG_DIRTY_STATIC) {
        return role == V5_STATUS_SCENE_SEGMENT_MCS_AXIS ||
            role == V5_STATUS_SCENE_SEGMENT_WCS_AXIS;
    }
    if (layer == V5_STATUS_SCENE_FLAG_DIRTY_MODEL) {
        return role == V5_STATUS_SCENE_SEGMENT_MODEL_AXIS;
    }
    return layer == V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC &&
        role == V5_STATUS_SCENE_SEGMENT_HOLDER;
}

static int marker_in_layer(uint16_t role, uint32_t layer)
{
    if (layer == V5_STATUS_SCENE_FLAG_DIRTY_STATIC) {
        return role == V5_STATUS_SCENE_MARKER_MCS_ORIGIN ||
            role == V5_STATUS_SCENE_MARKER_WCS_ORIGIN;
    }
    if (layer == V5_STATUS_SCENE_FLAG_DIRTY_MODEL) {
        return role == V5_STATUS_SCENE_MARKER_MODEL_CENTER;
    }
    return layer == V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC &&
        (role == V5_STATUS_SCENE_MARKER_MCS_ACTUAL ||
         role == V5_STATUS_SCENE_MARKER_CMD_TIP);
}

static void bounds_add_point(
    int *valid,
    int32_t *x1,
    int32_t *y1,
    int32_t *x2,
    int32_t *y2,
    V5StatusScreenPoint point)
{
    const int32_t x = (int32_t)lroundf(point.x);
    const int32_t y = (int32_t)lroundf(point.y);
    if (!*valid) {
        *valid = 1;
        *x1 = *x2 = x;
        *y1 = *y2 = y;
        return;
    }
    if (x < *x1) *x1 = x;
    if (x > *x2) *x2 = x;
    if (y < *y1) *y1 = y;
    if (y > *y2) *y2 = y;
}

static void invalidate_scene_layer(
    V5MainPage *page,
    lv_obj_t *object,
    const V5StatusDisplayScene *scene,
    uint32_t layer)
{
    lv_area_t coords;
    lv_area_t dirty;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
    int valid = 0;
    uint64_t pixels;
    unsigned int i;
    if (!page || !object || !scene) return;
    for (i = 0U; i < scene->segment_count; ++i) {
        if (!segment_in_layer(scene->segments[i].role, layer)) continue;
        bounds_add_point(
            &valid, &x1, &y1, &x2, &y2, scene->segments[i].start);
        bounds_add_point(
            &valid, &x1, &y1, &x2, &y2, scene->segments[i].end);
    }
    for (i = 0U; i < scene->marker_count; ++i) {
        if (!marker_in_layer(scene->markers[i].role, layer)) continue;
        bounds_add_point(
            &valid, &x1, &y1, &x2, &y2, scene->markers[i].point);
    }
    if (!valid) return;
    lv_obj_get_coords(object, &coords);
    x1 += (int32_t)coords.x1 - 7;
    y1 += (int32_t)coords.y1 - 7;
    x2 += (int32_t)coords.x1 + 7;
    y2 += (int32_t)coords.y1 + 7;
    if (x1 < coords.x1) x1 = coords.x1;
    if (y1 < coords.y1) y1 = coords.y1;
    if (x2 > coords.x2) x2 = coords.x2;
    if (y2 > coords.y2) y2 = coords.y2;
    if (x1 > x2 || y1 > y2) return;
    dirty.x1 = (lv_coord_t)x1;
    dirty.y1 = (lv_coord_t)y1;
    dirty.x2 = (lv_coord_t)x2;
    dirty.y2 = (lv_coord_t)y2;
    lv_obj_invalidate_area(object, &dirty);
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
    const V5StatusDisplayScene *previous;
    if (!page) return;
    previous = page->toolpath_previous_scene_valid ?
        &page->toolpath_previous_scene : NULL;
    v5_main_page_internal_clear_program_raster(page);
    if (previous) {
        invalidate_scene_layer(
            page, page->trajectory_line, previous,
            V5_STATUS_SCENE_FLAG_DIRTY_STATIC);
        invalidate_scene_layer(
            page, page->toolpath_dynamic_layer, previous,
            V5_STATUS_SCENE_FLAG_DIRTY_MODEL);
        invalidate_scene_layer(
            page, page->toolpath_dynamic_layer, previous,
            V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC);
    }
    page->toolpath_display_scene_valid = 0;
    page->toolpath_display_scene = NULL;
    page->toolpath_previous_scene_valid = 0;
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
    const V5StatusDisplayScene *scene,
    uint64_t scene_generation)
{
    unsigned char model_seen[2] = {0};
    int wcs_seen = 0;
    uint32_t dirty_layers;
    const V5StatusDisplayScene *previous_scene;
    int previous_valid;
    unsigned int i;
    if (!page || !scene ||
        (scene->flags & V5_STATUS_SCENE_FLAG_VALID) == 0U) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
        return;
    }
    page->toolpath_line_last_dirty_rect_count = 0U;
    page->toolpath_line_last_dirty_pixels = 0U;
    page->toolpath_line_last_dirty_max_pixels = 0U;
    previous_scene = page->toolpath_previous_scene_valid ?
        &page->toolpath_previous_scene : NULL;
    previous_valid = previous_scene != NULL;
    dirty_layers = scene->flags & V5_STATUS_SCENE_FLAG_DIRTY_MASK;
    if (!previous_valid ||
        (scene->flags & V5_STATUS_SCENE_FLAG_DIRTY_KNOWN) == 0U) {
        dirty_layers = V5_STATUS_SCENE_FLAG_DIRTY_MASK;
    }
    if (previous_valid) {
        if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_STATIC) {
            invalidate_scene_layer(
                page, page->trajectory_line, previous_scene,
                V5_STATUS_SCENE_FLAG_DIRTY_STATIC);
        }
        if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_MODEL) {
            invalidate_scene_layer(
                page, page->toolpath_dynamic_layer, previous_scene,
                V5_STATUS_SCENE_FLAG_DIRTY_MODEL);
        }
        if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC) {
            invalidate_scene_layer(
                page, page->toolpath_dynamic_layer, previous_scene,
                V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC);
        }
    }
    page->toolpath_display_scene = scene;
    page->toolpath_display_scene_valid = 1;
    if (!previous_valid ||
        (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_PROGRAM) != 0U) {
        (void)v5_main_page_internal_apply_program_display_scene(page, scene);
    }
    v5_main_page_internal_clear_hidden_flag_if_hidden(
        page->trajectory_line);
    if (page->toolpath_dynamic_layer) {
        v5_main_page_internal_clear_hidden_flag_if_hidden(
            page->toolpath_dynamic_layer);
    }
    if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_PROGRAM) {
        v5_main_page_internal_update_program_raster(
            page, scene, scene_generation);
    }
    if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_STATIC) {
        invalidate_scene_layer(
            page, page->trajectory_line, scene,
            V5_STATUS_SCENE_FLAG_DIRTY_STATIC);
    }
    if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_MODEL) {
        invalidate_scene_layer(
            page, page->toolpath_dynamic_layer, scene,
            V5_STATUS_SCENE_FLAG_DIRTY_MODEL);
    }
    if (dirty_layers & V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC) {
        invalidate_scene_layer(
            page, page->toolpath_dynamic_layer, scene,
            V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC);
    }
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
    page->toolpath_previous_scene = *scene;
    page->toolpath_previous_scene_valid = 1;
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
        page, status->display_scene, status->scene_generation);
}
