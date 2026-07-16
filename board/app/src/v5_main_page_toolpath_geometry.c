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

static int set_toolpath_active_model_geometry(
    V5MainPage *page,
    const V5MainPageModelGeometry *geometry)
{
    V5ToolpathScreenPoint center[2];
    V5ToolpathScreenPoint axis_start[2];
    V5ToolpathScreenPoint axis_end[2];
    double start_world[2][V5_STATUS_AXIS_COUNT];
    double end_world[2][V5_STATUS_AXIS_COUNT];
    const double *centers[2];
    const double *directions[2];
    char primary_label[2];
    char child_label[2];
    unsigned int axis_i;
    unsigned int component_i;

    if (!page || !geometry) {
        v5_main_page_internal_hide_toolpath_model_geometry(page);
        return 0;
    }
    centers[0] = geometry->primary_center;
    centers[1] = geometry->child_center;
    directions[0] = geometry->primary_direction;
    directions[1] = geometry->child_direction;
    for (axis_i = 0U; axis_i < 2U; ++axis_i) {
        memset(start_world[axis_i], 0, sizeof(start_world[axis_i]));
        memset(end_world[axis_i], 0, sizeof(end_world[axis_i]));
        for (component_i = 0U; component_i < 3U; ++component_i) {
            start_world[axis_i][component_i] =
                centers[axis_i][component_i] - (directions[axis_i][component_i] * 40.0);
            end_world[axis_i][component_i] =
                centers[axis_i][component_i] + (directions[axis_i][component_i] * 40.0);
        }
        if (!v5_main_page_internal_main_page_project_world_point_transformed(
                page,
                centers[axis_i],
                &center[axis_i]) ||
            !v5_main_page_internal_main_page_project_world_point_transformed(
                page,
                start_world[axis_i],
                &axis_start[axis_i]) ||
            !v5_main_page_internal_main_page_project_world_point_transformed(
                page,
                end_world[axis_i],
                &axis_end[axis_i])) {
            v5_main_page_internal_hide_toolpath_model_geometry(page);
            return 0;
        }
    }

    v5_main_page_internal_set_toolpath_axis_line(
        page->toolpath_model_primary_axis_line,
        page->toolpath_model_axis_points[0],
        &axis_start[0],
        &axis_end[0],
        v5_main_page_internal_clip_toolpath_segment(&axis_start[0], &axis_end[0]));
    v5_main_page_internal_set_toolpath_axis_line(
        page->toolpath_model_child_axis_line,
        page->toolpath_model_axis_points[1],
        &axis_start[1],
        &axis_end[1],
        v5_main_page_internal_clip_toolpath_segment(&axis_start[1], &axis_end[1]));
    v5_main_page_internal_set_toolpath_v3_center_dot(
        page->toolpath_model_primary_center_line,
        &center[0],
        1);
    v5_main_page_internal_set_toolpath_v3_center_dot(
        page->toolpath_model_child_center_line,
        &center[1],
        1);
    primary_label[0] = geometry->primary_axis;
    primary_label[1] = '\0';
    child_label[0] = geometry->child_axis;
    child_label[1] = '\0';
    if (page->toolpath_model_primary_label) {
        v5_main_page_internal_set_label_text_if_changed(
            page->toolpath_model_primary_label,
            primary_label);
        v5_main_page_internal_set_obj_pos_if_changed(
            page->toolpath_model_primary_label,
            V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(
                axis_end[0].x + 4.0,
                0,
                V5_TOOLPATH_W - 24),
            V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(
                axis_end[0].y - 12.0,
                0,
                V5_TOOLPATH_H - 22));
        v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_model_primary_label);
    }
    if (page->toolpath_model_child_label) {
        v5_main_page_internal_set_label_text_if_changed(
            page->toolpath_model_child_label,
            child_label);
        v5_main_page_internal_set_obj_pos_if_changed(
            page->toolpath_model_child_label,
            V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(
                axis_end[1].x + 8.0,
                0,
                V5_TOOLPATH_W - 20),
            V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(
                axis_end[1].y - 10.0,
                0,
                V5_TOOLPATH_H - 22));
        v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_model_child_label);
    }
    return 1;
}

static void hide_current_wcs_geometry(V5MainPage *page)
{
    unsigned int i;

    if (!page) {
        return;
    }
    v5_main_page_internal_hide_toolpath_line(page->toolpath_wcs_origin_line);
    for (i = 0U; i < 3U; ++i) {
        v5_main_page_internal_hide_toolpath_line(page->toolpath_wcs_axis_lines[i]);
    }
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_wcs_label);
}

void v5_main_page_internal_hide_toolpath_unproven_geometry(V5MainPage *page)
{
    unsigned int i;

    if (!page) {
        return;
    }
    v5_main_page_internal_hide_toolpath_line(page->toolpath_wcs_origin_line);
    v5_main_page_internal_hide_toolpath_line(page->toolpath_mcs_origin_line);
    for (i = 0U; i < 3U; ++i) {
        v5_main_page_internal_hide_toolpath_line(page->toolpath_wcs_axis_lines[i]);
        v5_main_page_internal_hide_toolpath_line(page->toolpath_mcs_axis_lines[i]);
    }
    v5_main_page_internal_hide_toolpath_program_wcs_objects(page);
    v5_main_page_internal_hide_toolpath_model_geometry(page);
    v5_main_page_internal_hide_toolpath_line(page->toolpath_holder_line);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_microkernel_marker_dot);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_holder_marker_line);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_mcs_label);
    v5_main_page_internal_add_hidden_flag_if_visible(page->toolpath_wcs_label);
}

void v5_main_page_internal_update_toolpath_state_lines(V5MainPage *page, const V5UiStatusView *status)
{
    V5ToolpathScreenPoint origin_point;
    V5ToolpathScreenPoint axis_point[3];
    V5ToolpathScreenPoint wcs_origin_point;
    V5ToolpathScreenPoint wcs_axis_point[3];
    double origin[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double axis_world[3][V5_STATUS_AXIS_COUNT] = {
        {40.0, 0.0, 0.0, 0.0, 0.0},
        {0.0, 40.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 40.0, 0.0, 0.0},
    };
    double wcs_origin[V5_STATUS_AXIS_COUNT];
    double wcs_axis[3][V5_STATUS_AXIS_COUNT];
    const V5MainPageModelScene *wcs_follow_scene = 0;
    V5MainPageModelGeometry model_geometry;
    int mcs_valid;
    int wcs_valid;
    int wcs_follow_active_model;
    int rtcp_requires_model_scene;
    unsigned int i;

    if (!page) {
        return;
    }
    mcs_valid = status && (status->valid_mask & V5_STATUS_VALID_MCS) != 0u && page->toolpath_fit.valid;
    if (!mcs_valid ||
        !v5_toolpath_display_project_world_point(origin, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &origin_point)) {
        v5_main_page_internal_hide_toolpath_unproven_geometry(page);
        return;
    } else {
        origin_point = v5_main_page_internal_apply_toolpath_view_transform(page, origin_point);
        v5_main_page_internal_set_toolpath_origin_cross(page->toolpath_mcs_origin_line, page->toolpath_mcs_origin_points, &origin_point, 1);
        for (i = 0U; i < 3U; ++i) {
            int ok = v5_toolpath_display_project_world_point(axis_world[i], &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &axis_point[i]);
            if (ok) {
                axis_point[i] = v5_main_page_internal_apply_toolpath_view_transform(page, axis_point[i]);
            }
            v5_main_page_internal_set_toolpath_axis_line(page->toolpath_mcs_axis_lines[i], page->toolpath_mcs_axis_points[i], &origin_point, &axis_point[i], ok);
        }
        if (page->toolpath_model_scene_valid &&
            page->toolpath_model_scene_fresh &&
            v5_main_page_model_scene_build_geometry(
                &page->toolpath_model_scene,
                &model_geometry)) {
            set_toolpath_active_model_geometry(page, &model_geometry);
        } else if (!page->toolpath_model_scene_valid) {
            v5_main_page_internal_hide_toolpath_model_geometry(page);
        }
    }

    v5_main_page_internal_hide_toolpath_program_wcs_objects(page);
    rtcp_requires_model_scene =
        v5_native_readback_rtcp_known(&page->native_readback) &&
        page->native_readback.rtcp_enabled;
    if (rtcp_requires_model_scene && !page->toolpath_model_scene_fresh) {
        if (!page->toolpath_model_scene_valid) {
            hide_current_wcs_geometry(page);
        }
        return;
    }
    wcs_valid = mcs_valid && v5_native_readback_wcs_offset_known(&page->native_readback);
    if (!wcs_valid) {
        if (page->toolpath_wcs_label) {
            v5_main_page_internal_set_label_text_if_changed(page->toolpath_wcs_label, "WCS");
            v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
        }
    } else {
        const double *active_offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
        if (!active_offsets) {
            return;
        }
        memset(wcs_origin, 0, sizeof(wcs_origin));
        for (i = 0U; i < V5_STATUS_AXIS_COUNT && i < V5_NATIVE_READBACK_WCS_OFFSET_COUNT; ++i) {
            wcs_origin[i] = active_offsets[i];
        }
        for (i = 0U; i < 3U; ++i) {
            memcpy(wcs_axis[i], wcs_origin, sizeof(wcs_axis[i]));
            wcs_axis[i][i] += 40.0;
        }
        wcs_follow_active_model = v5_main_page_internal_main_page_rtcp_wcs_follow_model_scene_available(
            page,
            &wcs_follow_scene);
        if (wcs_follow_active_model) {
            v5_main_page_model_scene_transform_world_point(wcs_follow_scene, wcs_origin);
            for (i = 0U; i < 3U; ++i) {
                v5_main_page_model_scene_transform_world_point(wcs_follow_scene, wcs_axis[i]);
            }
        }
        if (v5_toolpath_display_project_world_point(wcs_origin, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &wcs_origin_point)) {
            wcs_origin_point = v5_main_page_internal_apply_toolpath_view_transform(page, wcs_origin_point);
            v5_main_page_internal_set_toolpath_origin_cross(page->toolpath_wcs_origin_line, page->toolpath_wcs_origin_points, &wcs_origin_point, 1);
            if (page->toolpath_wcs_label) {
                v5_main_page_internal_set_obj_pos_if_changed(
                    page->toolpath_wcs_label,
                    V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(wcs_origin_point.x + 5.0, 0, V5_TOOLPATH_W - 36),
                    V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(wcs_origin_point.y - 14.0, 0, V5_TOOLPATH_H - 18));
                v5_main_page_internal_set_label_text_if_changed(page->toolpath_wcs_label, v5_main_page_internal_main_page_wcs_code(&page->native_readback));
                v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_wcs_label);
            }
            for (i = 0U; i < 3U; ++i) {
                int ok = v5_toolpath_display_project_world_point(wcs_axis[i], &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, &wcs_axis_point[i]);
                if (ok) {
                    wcs_axis_point[i] = v5_main_page_internal_apply_toolpath_view_transform(page, wcs_axis_point[i]);
                }
                v5_main_page_internal_set_toolpath_axis_line(page->toolpath_wcs_axis_lines[i], page->toolpath_wcs_axis_points[i], &wcs_origin_point, &wcs_axis_point[i], ok);
            }
        }
    }
}
