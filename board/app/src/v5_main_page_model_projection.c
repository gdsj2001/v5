#include "v5_main_page.h"

#include "v5_main_page_internal.h"
#include "v5_motion_model_registry.h"

#include <string.h>

static void v5_main_page_clear_active_model_scene(V5MainPage *page)
{
    if (!page) {
        return;
    }
    page->toolpath_model_scene_valid = 0;
    page->toolpath_model_scene_fresh = 0;
    memset(&page->toolpath_model_scene, 0, sizeof(page->toolpath_model_scene));
}

void v5_main_page_internal_update_coordinate_target_axes(V5MainPage *page)
{
    unsigned int i;
    char old_fourth;
    char new_fourth;
    int selection_still_active = 0;

    if (!page) {
        return;
    }
    old_fourth = page->mcs_targets[3].axis;
    new_fourth = v5_main_page_internal_main_page_axis_display_char(page, 3U);
    for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        char axis = v5_main_page_internal_main_page_axis_display_char(page, i);
        page->mcs_targets[i].axis = axis;
        page->wcs_targets[i].axis = axis;
    }
    if (!page->selection.all_axes && old_fourth &&
        page->selection.axis == old_fourth &&
        old_fourth != new_fourth &&
        new_fourth != '-') {
        page->selection.axis = new_fourth;
    }
    if (!page->selection.all_axes) {
        for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
            if (page->mcs_targets[i].axis == page->selection.axis) {
                selection_still_active = 1;
                break;
            }
        }
        if (!selection_still_active) {
            page->selection.space = V5_MAIN_PAGE_SELECT_MCS;
            page->selection.axis = '*';
            page->selection.all_axes = 1;
            if (page->selection_idle_timer) {
                lv_timer_pause(page->selection_idle_timer);
            }
        }
    }
}

int v5_main_page_internal_main_page_project_world_point_transformed(
    const V5MainPage *page,
    const double world[V5_STATUS_AXIS_COUNT],
    V5ToolpathScreenPoint *point)
{
    if (!page || !world || !point || !page->toolpath_fit.valid ||
        !v5_toolpath_display_project_world_point(
            world,
            &page->toolpath_fit,
            (double)V5_TOOLPATH_W,
            (double)V5_TOOLPATH_H,
            point)) {
        return 0;
    }
    *point = v5_main_page_internal_apply_toolpath_view_transform(page, *point);
    return 1;
}

int v5_main_page_internal_main_page_resolve_active_model_scene(
    V5MainPage *page,
    const V5UiStatusView *status)
{
    const V5MotionModelDescriptor *model;
    V5MainPageModelScene scene;

    if (!page) {
        return 0;
    }
    page->toolpath_model_scene_fresh = 0;
    if (page->native_readback.g53_geometry_stale) {
        return 0;
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    if (!model) {
        v5_main_page_clear_active_model_scene(page);
        return 0;
    }
    if (!v5_main_page_model_projector_descriptor_supported(model)) {
        v5_main_page_clear_active_model_scene(page);
        return 0;
    }
    if (page->toolpath_model_scene_valid &&
        page->toolpath_model_scene.registry_id != model->registry_id) {
        v5_main_page_clear_active_model_scene(page);
    }
    if (!v5_main_page_model_scene_resolve(
            model,
            &page->native_readback,
            status,
            &scene)) {
        if (!page->toolpath_model_scene_valid) {
            v5_main_page_clear_active_model_scene(page);
        }
        return 0;
    }
    page->toolpath_model_scene = scene;
    page->toolpath_model_scene_valid = 1;
    page->toolpath_model_scene_fresh = 1;
    return 1;
}

int v5_main_page_internal_main_page_program_model_projection_changed(const V5MainPage *page)
{
    if (!page || !page->toolpath_program_visible ||
        page->toolpath_program_point_count == 0U) {
        return 0;
    }
    if (page->toolpath_model_scene_valid != page->toolpath_program_model_scene_valid) {
        return 1;
    }
    if (!page->toolpath_model_scene_valid) {
        return 0;
    }
    if (!page->toolpath_model_scene_fresh) {
        return 0;
    }
    return !v5_main_page_model_scene_pose_equal(
        &page->toolpath_model_scene,
        &page->toolpath_program_model_scene,
        0.0005);
}

int v5_main_page_internal_main_page_static_pose_changed(const V5MainPage *page)
{
    if (!page) {
        return 0;
    }
    if (page->toolpath_model_scene_valid != page->toolpath_static_model_scene_valid) {
        return 1;
    }
    if (!page->toolpath_model_scene_valid) {
        return 0;
    }
    if (!page->toolpath_model_scene_fresh) {
        return 0;
    }
    return !v5_main_page_model_scene_pose_equal(
        &page->toolpath_model_scene,
        &page->toolpath_static_model_scene,
        0.0005);
}

void v5_main_page_internal_main_page_store_static_pose(V5MainPage *page)
{
    if (!page) {
        return;
    }
    if (page->toolpath_model_scene_valid &&
        !page->toolpath_model_scene_fresh) {
        return;
    }
    page->toolpath_static_model_scene_valid = page->toolpath_model_scene_valid;
    if (page->toolpath_model_scene_valid) {
        page->toolpath_static_model_scene = page->toolpath_model_scene;
    } else {
        memset(
            &page->toolpath_static_model_scene,
            0,
            sizeof(page->toolpath_static_model_scene));
    }
}

int v5_main_page_internal_main_page_rtcp_wcs_follow_model_scene_available(
    const V5MainPage *page,
    const V5MainPageModelScene **scene)
{
    if (!page ||
        !v5_native_readback_rtcp_known(&page->native_readback) ||
        !page->native_readback.rtcp_enabled ||
        !page->toolpath_model_scene_valid ||
        !page->toolpath_model_scene_fresh) {
        return 0;
    }
    if (scene) {
        *scene = &page->toolpath_model_scene;
    }
    return 1;
}

int v5_main_page_internal_main_page_update_program_project_points(
    V5MainPage *page,
    unsigned int count)
{
    unsigned int i;

    if (!page) {
        return 0;
    }
    if (!page->toolpath_model_scene_valid ||
        !page->toolpath_model_scene_fresh) {
        return 0;
    }
    if (count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < count; ++i) {
        page->toolpath_program_project_points[i] = page->toolpath_program_points[i];
    }
    page->toolpath_program_model_scene_valid = 1;
    page->toolpath_program_model_scene = page->toolpath_model_scene;
    for (i = 0U; i < count; ++i) {
        if (!v5_main_page_model_scene_transform_world_point(
                &page->toolpath_model_scene,
                page->toolpath_program_project_points[i].axis)) {
            page->toolpath_program_model_scene_valid = 0;
            memset(
                &page->toolpath_program_model_scene,
                0,
                sizeof(page->toolpath_program_model_scene));
            return 0;
        }
    }
    return 1;
}
