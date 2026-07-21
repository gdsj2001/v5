#include "v5_lvgl_headless.h"
#include "v5_main_page.h"
#include "v5_toolpath_viewport.h"

#include <stdio.h>
#include <string.h>

static void prepare_status(
    V5UiStatusView *status,
    V5StatusDisplayScene *scene,
    uint64_t scene_generation)
{
    v5_ui_status_view_init(status);
    memset(scene, 0, sizeof(*scene));
    status->valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_DISPLAY_SCENE;
    status->status_epoch = 100ULL;
    status->position_writer_identity = 1U;
    status->source_acquired_mono_ns = 90ULL;
    status->source_generation = 7ULL;
    status->scene_generation = scene_generation;
    status->display_scene = scene;
    scene->flags = V5_STATUS_SCENE_FLAG_VALID | V5_STATUS_SCENE_FLAG_PROGRAM |
        V5_STATUS_SCENE_FLAG_MODEL | V5_STATUS_SCENE_FLAG_WCS |
        V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
        V5_STATUS_SCENE_FLAG_DIRTY_MASK;
    scene->native_generation = 7ULL;
    scene->view_generation = 1ULL;
    scene->fit_generation = 1ULL;
    scene->plane = V5_STATUS_SCENE_PLANE_3D;
    scene->build_count = scene_generation;
    scene->project_count = scene_generation;
    scene->primary_axis = 'A';
    scene->child_axis = 'C';
    scene->point_count = 3U;
    scene->points[0].x = 20.0f; scene->points[0].y = 20.0f;
    scene->points[1].x = 100.0f; scene->points[1].y = 60.0f;
    scene->points[2].x = 180.0f; scene->points[2].y = 120.0f;
    scene->segment_count = 2U;
    scene->segments[0].role = V5_STATUS_SCENE_SEGMENT_MCS_AXIS;
    scene->segments[0].index = 0U;
    scene->segments[0].start.x = 40.0f; scene->segments[0].start.y = 200.0f;
    scene->segments[0].end.x = 80.0f; scene->segments[0].end.y = 200.0f;
    scene->segments[1].role = V5_STATUS_SCENE_SEGMENT_MODEL_AXIS;
    scene->segments[1].index = 0U;
    scene->segments[1].start.x = 120.0f; scene->segments[1].start.y = 160.0f;
    scene->segments[1].end.x = 160.0f; scene->segments[1].end.y = 160.0f;
    scene->marker_count = 2U;
    scene->markers[0].role = V5_STATUS_SCENE_MARKER_MCS_ORIGIN;
    scene->markers[0].point.x = 40.0f; scene->markers[0].point.y = 200.0f;
    scene->markers[1].role = V5_STATUS_SCENE_MARKER_CMD_TIP;
    scene->markers[1].point.x = 150.0f; scene->markers[1].point.y = 80.0f;
    scene->dirty_x1 = 140;
    scene->dirty_y1 = 70;
    scene->dirty_x2 = 160;
    scene->dirty_y2 = 90;
}

int main(void)
{
    V5MainPage page;
    V5UiStatusView status;
    V5StatusDisplayScene first_scene;
    V5StatusDisplayScene second_scene;
    V5StatusDisplayScene third_scene;
    lv_obj_t *screen;
    unsigned int rewrites;
    lv_init();
    if (!v5_lvgl_headless_display_setup()) return 1;
    screen = lv_scr_act();
    if (!screen || !v5_main_page_create(&page, screen)) return 2;
    lv_obj_update_layout(screen);
    {
        const V5ToolpathViewport *viewport = v5_toolpath_viewport();
        if (lv_obj_get_x(page.toolpath_clip_layer) != viewport->x ||
            lv_obj_get_y(page.toolpath_clip_layer) != viewport->y ||
            lv_obj_get_width(page.toolpath_clip_layer) != viewport->width ||
            lv_obj_get_height(page.toolpath_clip_layer) != viewport->height ||
            lv_obj_get_child_cnt(page.toolpath_clip_layer) != 2U ||
            lv_obj_get_x(page.trajectory_line) != 0 ||
            lv_obj_get_y(page.trajectory_line) != 0 ||
            lv_obj_get_width(page.trajectory_line) != viewport->width ||
            lv_obj_get_height(page.trajectory_line) != viewport->height ||
            lv_obj_get_x(page.toolpath_dynamic_layer) != 0 ||
            lv_obj_get_y(page.toolpath_dynamic_layer) != 0 ||
            lv_obj_get_width(page.toolpath_dynamic_layer) != viewport->width ||
            lv_obj_get_height(page.toolpath_dynamic_layer) != viewport->height) {
            fprintf(stderr,
                "viewport clip=%d,%d,%d,%d children=%u scene=%d,%d,%d,%d expected=%d,%d,%d,%d\n",
                (int)lv_obj_get_x(page.toolpath_clip_layer),
                (int)lv_obj_get_y(page.toolpath_clip_layer),
                (int)lv_obj_get_width(page.toolpath_clip_layer),
                (int)lv_obj_get_height(page.toolpath_clip_layer),
                (unsigned int)lv_obj_get_child_cnt(page.toolpath_clip_layer),
                (int)lv_obj_get_x(page.trajectory_line),
                (int)lv_obj_get_y(page.trajectory_line),
                (int)lv_obj_get_width(page.trajectory_line),
                (int)lv_obj_get_height(page.trajectory_line),
                viewport->x, viewport->y, viewport->width, viewport->height);
            return 30;
        }
    }
    if (page.view_plane != V5_TOOLPATH_DISPLAY_3D ||
        page.toolpath_view_generation != 1U ||
        page.toolpath_fit_generation != 1U) return 31;
    {
        unsigned int fit_generation = page.toolpath_fit_generation;
        v5_main_page_set_page_visible(&page, 1);
        v5_main_page_set_page_visible(&page, 0);
        if (page.toolpath_fit_generation != fit_generation) return 32;
    }
    {
        V5MainPageActionReport report;
        unsigned int view_generation = page.toolpath_view_generation;
        unsigned int fit_generation = page.toolpath_fit_generation;
        page.toolpath_manual_scale = 2.0;
        page.toolpath_manual_rotate_deg = 17.0;
        page.toolpath_manual_pan_x = 31.0;
        page.toolpath_manual_pan_y = -23.0;
        if (!v5_main_page_trigger_action(&page, V5_MAIN_PAGE_ACTION_VIEW_3D, &report) ||
            page.view_plane != V5_TOOLPATH_DISPLAY_3D ||
            page.toolpath_view_generation != view_generation + 1U ||
            page.toolpath_fit_generation != fit_generation + 1U ||
            page.toolpath_manual_scale != 1.0 ||
            page.toolpath_manual_rotate_deg != 0.0 ||
            page.toolpath_manual_pan_x != 0.0 ||
            page.toolpath_manual_pan_y != 0.0) return 33;
    }
    page.toolpath_last_request_view_generation = page.toolpath_view_generation;
    page.toolpath_last_request_fit_generation = page.toolpath_fit_generation;
    page.toolpath_last_request_page_visible = 0U;
    prepare_status(&status, &first_scene, 1ULL);
    first_scene.view_generation = page.toolpath_view_generation;
    first_scene.plane = V5_STATUS_SCENE_PLANE_XY;
    page.toolpath_request_retry_count = V5_PROGRAM_SCENE_REQUEST_RETRY_LIMIT;
    if (!v5_main_page_apply_status_flags(&page, &status,
            V5_MAIN_PAGE_REFRESH_DYNAMIC | V5_MAIN_PAGE_REFRESH_POSE |
            V5_MAIN_PAGE_REFRESH_STRUCTURE) ||
        page.toolpath_scene_generation != 0ULL ||
        page.toolpath_display_scene_valid) return 4;
    first_scene.plane = V5_STATUS_SCENE_PLANE_3D;
    if (!v5_main_page_apply_status_flags(&page, &status,
            V5_MAIN_PAGE_REFRESH_DYNAMIC | V5_MAIN_PAGE_REFRESH_POSE |
            V5_MAIN_PAGE_REFRESH_STRUCTURE)) return 4;
    if (page.toolpath_scene_generation != 1ULL || page.trajectory_point_count != 3U ||
        lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ||
        !page.toolpath_display_scene_valid ||
        page.toolpath_display_scene != &first_scene ||
        page.toolpath_display_scene->segment_count != 2U ||
        page.toolpath_display_scene->marker_count != 2U) return 5;
    rewrites = page.toolpath_line_rewrite_count;
    if (!v5_main_page_apply_status_flags(&page, &status, V5_MAIN_PAGE_REFRESH_DYNAMIC) ||
        page.toolpath_line_rewrite_count != rewrites || page.toolpath_static_cache_hits == 0U) return 6;
    prepare_status(&status, &second_scene, 2ULL);
    second_scene.view_generation = page.toolpath_view_generation;
    second_scene.points[1].x = 110.0f;
    second_scene.flags &= ~V5_STATUS_SCENE_FLAG_DIRTY_MASK;
    second_scene.flags |= V5_STATUS_SCENE_FLAG_DIRTY_STATIC;
    if (!v5_main_page_apply_status_flags(&page, &status, V5_MAIN_PAGE_REFRESH_DYNAMIC) ||
        page.toolpath_scene_generation != 2ULL ||
        page.toolpath_line_rewrite_count <= rewrites ||
        page.toolpath_line_last_dirty_rect_count != 2U ||
        page.toolpath_line_last_dirty_pixels >=
            (uint64_t)v5_toolpath_viewport()->width *
            (uint64_t)v5_toolpath_viewport()->height) return 7;
    rewrites = page.toolpath_line_rewrite_count;
    prepare_status(&status, &third_scene, 3ULL);
    third_scene.view_generation = page.toolpath_view_generation;
    third_scene.flags &= ~V5_STATUS_SCENE_FLAG_DIRTY_MASK;
    third_scene.flags |= V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC;
    third_scene.markers[1].point.x = 154.0f;
    if (!v5_main_page_apply_status_flags(&page, &status, V5_MAIN_PAGE_REFRESH_DYNAMIC) ||
        page.toolpath_scene_generation != 3ULL ||
        page.toolpath_line_rewrite_count != rewrites ||
        page.toolpath_line_last_dirty_rect_count != 2U ||
        page.toolpath_line_last_dirty_pixels >= 2048U) return 34;
    status.valid_mask &= ~V5_STATUS_VALID_DISPLAY_SCENE;
    if (!v5_main_page_apply_status_flags(&page, &status, V5_MAIN_PAGE_REFRESH_DYNAMIC) ||
        !lv_obj_has_flag(page.trajectory_line, LV_OBJ_FLAG_HIDDEN)) return 8;
    return 0;
}
