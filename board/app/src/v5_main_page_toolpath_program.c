#include "v5_main_page.h"

#include "v5_main_page_internal.h"

#include <math.h>
#include <string.h>

void v5_main_page_internal_hide_toolpath_program_line(V5MainPage *page)
{
    int was_drawn;
    if (!page || !page->trajectory_line) return;
    was_drawn = page->trajectory_point_count > 0U;
    v5_main_page_internal_clear_program_raster(page);
    page->toolpath_program_point_count = 0U;
    page->toolpath_program_visible = 0;
    page->trajectory_point_count = 0U;
    if (was_drawn) {
        page->toolpath_line_rewrite_count += 1U;
        lv_obj_invalidate(page->trajectory_line);
    }
    if (!page->toolpath_display_scene_valid) {
        lv_obj_add_flag(page->trajectory_line, LV_OBJ_FLAG_HIDDEN);
        if (page->toolpath_dynamic_layer) {
            lv_obj_add_flag(page->toolpath_dynamic_layer, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void v5_main_page_internal_mark_toolpath_static_dirty(V5MainPage *page)
{
    if (!page) return;
    v5_main_page_internal_clear_program_raster(page);
    page->toolpath_program_generation = 0U;
    page->toolpath_program_view_generation = 0U;
    page->toolpath_program_visible = 0;
    page->toolpath_program_point_count = 0U;
    page->toolpath_display_scene_valid = 0;
    page->toolpath_display_scene = NULL;
    page->toolpath_previous_scene_valid = 0;
    memset(&page->toolpath_fit, 0, sizeof(page->toolpath_fit));
}

int v5_main_page_internal_publish_program_scene_request(V5MainPage *page)
{
    V5ProgramSceneRequest request;
    V5ToolpathViewTransform transform;
    const V5ProgramRuntime *runtime;
    unsigned int i;
    if (!page) return 0;
    runtime = page->program_controller ?
        v5_program_controller_runtime(page->program_controller) : 0;
    v5_program_scene_request_init(&request);
    request.view_generation = page->toolpath_view_generation ?
        page->toolpath_view_generation : 1U;
    request.fit_generation = page->toolpath_fit_generation ?
        page->toolpath_fit_generation : 1U;
    request.plane = (uint32_t)page->view_plane;
    request.page_visible = page->page_visible ? 1U : 0U;
    v5_main_page_internal_prepare_toolpath_view_transform(page, &transform);
    request.scale = (float)transform.scale;
    request.sine = (float)transform.sine;
    request.cosine = (float)transform.cosine;
    request.pan_x = (float)transform.pan_x;
    request.pan_y = (float)transform.pan_y;
    if (runtime && v5_program_runtime_has_open_program(runtime)) {
        request.program_generation = v5_program_runtime_loaded_epoch(runtime);
        request.program_source_identity = v5_program_scene_source_identity(
            v5_program_runtime_source_sha256(runtime));
        request.program_wcs_mask =
            v5_program_runtime_preview_wcs_mask(runtime);
    }
    v5_program_scene_request_prepare_transport(
        &request,
        page->toolpath_display_scene_valid ?
            page->toolpath_display_scene : 0);
    page->toolpath_last_request_program_source_identity =
        request.program_source_identity;
    page->toolpath_last_request_program_generation =
        request.program_generation;
    page->toolpath_last_request_view_generation = request.view_generation;
    page->toolpath_last_request_fit_generation = request.fit_generation;
    page->toolpath_last_request_page_visible = request.page_visible;
    if (request.reserved == V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL) {
        if (runtime && v5_program_runtime_has_open_program(runtime)) {
            request.point_count = v5_program_runtime_preview_trajectory(
                runtime, request.points, V5_STATUS_SCENE_POINT_COUNT);
            for (i = 0U; i < request.point_count; ++i) {
                int wcs_index = -1;
                if (!v5_program_runtime_preview_wcs_index(
                        runtime, i, &wcs_index) ||
                    wcs_index < 0 ||
                    wcs_index >= (int)V5_NATIVE_READBACK_WCS_COUNT) return 0;
                request.wcs_index[i] = (int8_t)wcs_index;
                request.break_before[i] =
                    v5_program_runtime_preview_break_before(
                        runtime, i) ? 1U : 0U;
            }
        }
    }
    if (!v5_program_scene_request_publish(0, &request)) return 0;
    return 1;
}

void v5_main_page_internal_reset_toolpath_view(V5MainPage *page)
{
    if (!page) return;
    page->toolpath_gesture_active_count = 0;
    page->toolpath_gesture_last_distance = 0.0;
    page->toolpath_gesture_last_angle_deg = 0.0;
    page->toolpath_manual_scale = 1.0;
    page->toolpath_manual_rotate_deg = 0.0;
    page->toolpath_manual_pan_x = 0.0;
    page->toolpath_manual_pan_y = 0.0;
    if (++page->toolpath_view_generation == 0U) {
        page->toolpath_view_generation = 1U;
    }
    if (++page->toolpath_fit_generation == 0U) {
        page->toolpath_fit_generation = 1U;
    }
}

static lv_color_t axis_color(int wcs, unsigned int axis)
{
    static const uint8_t mcs[3][3] = {
        {255, 150, 156}, {120, 255, 190}, {180, 226, 255}};
    static const uint8_t work[3][3] = {
        {255, 100, 106}, {0, 232, 150}, {86, 204, 252}};
    const uint8_t (*colors)[3] = wcs ? work : mcs;
    axis = axis < 3U ? axis : 0U;
    return lv_color_make(
        colors[axis][0], colors[axis][1], colors[axis][2]);
}

static lv_color_t segment_color(const V5StatusSceneSegment *segment)
{
    if (segment->role == V5_STATUS_SCENE_SEGMENT_MCS_AXIS) {
        return axis_color(0, segment->index);
    }
    if (segment->role == V5_STATUS_SCENE_SEGMENT_WCS_AXIS) {
        return axis_color(1, segment->index);
    }
    if (segment->role == V5_STATUS_SCENE_SEGMENT_MODEL_AXIS) {
        return segment->index ?
            lv_color_make(120, 240, 255) :
            lv_color_make(255, 113, 118);
    }
    return lv_color_make(96, 176, 255);
}

static lv_color_t marker_color(const V5StatusSceneMarker *marker)
{
    if (marker->role == V5_STATUS_SCENE_MARKER_CMD_TIP) {
        return lv_color_make(255, 64, 64);
    }
    if (marker->role == V5_STATUS_SCENE_MARKER_MCS_ACTUAL ||
        marker->role == V5_STATUS_SCENE_MARKER_WCS_ORIGIN) {
        return lv_color_make(68, 221, 144);
    }
    if (marker->role == V5_STATUS_SCENE_MARKER_MODEL_CENTER) {
        return marker->index ?
            lv_color_make(120, 240, 255) :
            lv_color_make(255, 113, 118);
    }
    return lv_color_make(210, 235, 255);
}

static lv_point_t local_point(
    V5StatusScreenPoint point,
    lv_coord_t x_offset,
    lv_coord_t y_offset)
{
    lv_point_t out;
    out.x = x_offset + (lv_coord_t)lroundf(point.x);
    out.y = y_offset + (lv_coord_t)lroundf(point.y);
    return out;
}

static void draw_line(
    lv_draw_ctx_t *draw_ctx,
    lv_draw_line_dsc_t *line,
    lv_coord_t x_offset,
    lv_coord_t y_offset,
    V5StatusScreenPoint start_point,
    V5StatusScreenPoint end_point,
    lv_color_t color,
    lv_coord_t width)
{
    lv_point_t start = local_point(start_point, x_offset, y_offset);
    lv_point_t end = local_point(end_point, x_offset, y_offset);
    line->color = color;
    line->width = width;
    line->opa = LV_OPA_COVER;
    lv_draw_line(draw_ctx, line, &start, &end);
}

static void draw_marker(
    lv_draw_ctx_t *draw_ctx,
    lv_draw_line_dsc_t *line,
    lv_coord_t x_offset,
    lv_coord_t y_offset,
    const V5StatusSceneMarker *marker)
{
    const float x = marker->point.x;
    const float y = marker->point.y;
    const float half =
        marker->role == V5_STATUS_SCENE_MARKER_MCS_ACTUAL ||
        marker->role == V5_STATUS_SCENE_MARKER_CMD_TIP ? 4.0f : 3.0f;
    const lv_color_t color = marker_color(marker);
    const V5StatusScreenPoint horizontal_start = {x - half, y};
    const V5StatusScreenPoint horizontal_end = {x + half, y};
    const V5StatusScreenPoint vertical_start = {x, y - half};
    const V5StatusScreenPoint vertical_end = {x, y + half};
    draw_line(
        draw_ctx, line, x_offset, y_offset,
        horizontal_start, horizontal_end, color, 2);
    draw_line(
        draw_ctx, line, x_offset, y_offset,
        vertical_start, vertical_end, color, 2);
}

static void toolpath_scene_draw_event_cb(lv_event_t *event)
{
    V5MainPage *page;
    lv_obj_t *object;
    lv_draw_ctx_t *draw_ctx;
    lv_area_t coords;
    lv_coord_t x_offset;
    lv_coord_t y_offset;
    const V5StatusDisplayScene *scene;
    lv_draw_line_dsc_t line;
    int dynamic_layer;
    unsigned int i;
    if (!event || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    page = (V5MainPage *)lv_event_get_user_data(event);
    object = lv_event_get_target(event);
    draw_ctx = lv_event_get_draw_ctx(event);
    if (!page || !object || !draw_ctx ||
        !page->toolpath_display_scene_valid ||
        !page->toolpath_display_scene) return;
    scene = page->toolpath_display_scene;
    dynamic_layer = object == page->toolpath_dynamic_layer;
    lv_obj_get_coords(object, &coords);
    x_offset = coords.x1 - lv_obj_get_scroll_x(object);
    y_offset = coords.y1 - lv_obj_get_scroll_y(object);
    lv_draw_line_dsc_init(&line);
    lv_obj_init_draw_line_dsc(object, LV_PART_MAIN, &line);
    if (!dynamic_layer) {
        v5_main_page_internal_draw_program_raster(
            page, draw_ctx, x_offset, y_offset);
    }
    for (i = 0U; i < scene->segment_count; ++i) {
        const V5StatusSceneSegment *segment = &scene->segments[i];
        const int is_dynamic =
            segment->role == V5_STATUS_SCENE_SEGMENT_MODEL_AXIS ||
            segment->role == V5_STATUS_SCENE_SEGMENT_HOLDER;
        if (dynamic_layer != is_dynamic) continue;
        draw_line(
            draw_ctx, &line, x_offset, y_offset,
            segment->start, segment->end, segment_color(segment),
            segment->role == V5_STATUS_SCENE_SEGMENT_HOLDER ? 5 : 1);
    }
    for (i = 0U; i < scene->marker_count; ++i) {
        const int is_dynamic =
            scene->markers[i].role == V5_STATUS_SCENE_MARKER_MODEL_CENTER ||
            scene->markers[i].role == V5_STATUS_SCENE_MARKER_MCS_ACTUAL ||
            scene->markers[i].role == V5_STATUS_SCENE_MARKER_CMD_TIP;
        if (dynamic_layer != is_dynamic) continue;
        draw_marker(
            draw_ctx, &line, x_offset, y_offset, &scene->markers[i]);
    }
}

lv_obj_t *v5_main_page_internal_create_toolpath_scene_layer(
    V5MainPage *page,
    lv_obj_t *parent)
{
    const V5ToolpathViewport *viewport = v5_toolpath_viewport();
    lv_obj_t *scene;
    if (!page || !parent) return 0;
    scene = lv_obj_create(parent);
    v5_main_page_internal_clear_obj_style(scene);
    lv_obj_set_pos(scene, 0, 0);
    lv_obj_set_size(scene, viewport->width, viewport->height);
    lv_obj_set_style_bg_opa(scene, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(scene, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scene, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scene, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        scene, toolpath_scene_draw_event_cb, LV_EVENT_DRAW_MAIN, page);
    return scene;
}

int v5_main_page_internal_apply_program_display_scene(
    V5MainPage *page,
    const V5StatusDisplayScene *scene)
{
    unsigned int count;
    if (!page || !scene ||
        (scene->flags & V5_STATUS_SCENE_FLAG_VALID) == 0U) return 0;
    count = scene->point_count;
    if (count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    page->trajectory_point_count = count;
    page->toolpath_program_point_count = count;
    page->toolpath_program_visible = count > 0U;
    if (page->trajectory_line) {
        v5_main_page_internal_clear_hidden_flag_if_hidden(
            page->trajectory_line);
    }
    page->toolpath_line_rewrite_count += 1U;
    return 1;
}
