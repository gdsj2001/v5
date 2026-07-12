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

static void set_toolpath_active_model_geometry_from_basis(
    V5MainPage *page,
    const V5MotionModelDescriptor *model,
    const V5ToolpathScreenPoint *origin,
    const V5ToolpathScreenPoint *x_axis,
    const V5ToolpathScreenPoint *y_axis,
    const V5ToolpathScreenPoint *z_axis,
    const V5ToolpathScreenPoint *first_center_override,
    const V5ToolpathScreenPoint *second_center_override,
    double first_deg,
    int first_valid)
{
    V5ToolpathScreenPoint ac_center[2];
    V5ToolpathScreenPoint ac_axis_start[2];
    V5ToolpathScreenPoint ac_axis_end[2];
    double angle = 0.0;
    double x_dx;
    double x_dy;
    double y_dx;
    double y_dy;
    double z_dx;
    double z_dy;
    double c_dx;
    double c_dy;
    char first_label[2] = {model ? model->first_rotary_axis : '-', '\0'};

    if (!page || !origin || !x_axis || !y_axis || !z_axis ||
        !first_center_override || !second_center_override || !model) {
        v5_main_page_internal_hide_toolpath_ac_geometry(page);
        return;
    }

    if (first_valid && isfinite(first_deg)) {
        angle = first_deg * M_PI / 180.0;
    }
    x_dx = x_axis->x - origin->x;
    x_dy = x_axis->y - origin->y;
    y_dx = y_axis->x - origin->x;
    y_dy = y_axis->y - origin->y;
    z_dx = z_axis->x - origin->x;
    z_dy = z_axis->y - origin->y;
    if (model->first_world_axis_component == 1U) {
        c_dx = (sin(angle) * x_dx) + (cos(angle) * z_dx);
        c_dy = (sin(angle) * x_dy) + (cos(angle) * z_dy);
    } else {
        c_dx = (-sin(angle) * y_dx) + (cos(angle) * z_dx);
        c_dy = (-sin(angle) * y_dy) + (cos(angle) * z_dy);
    }

    ac_center[0] = *first_center_override;
    if (model->first_world_axis_component == 1U) {
        ac_axis_start[0] = v5_main_page_internal_toolpath_scaffold_point(ac_center[0].x - y_dx, ac_center[0].y - y_dy);
        ac_axis_end[0] = v5_main_page_internal_toolpath_scaffold_point(ac_center[0].x + y_dx, ac_center[0].y + y_dy);
    } else {
        ac_axis_start[0] = v5_main_page_internal_toolpath_scaffold_point(ac_center[0].x - x_dx, ac_center[0].y - x_dy);
        ac_axis_end[0] = v5_main_page_internal_toolpath_scaffold_point(ac_center[0].x + x_dx, ac_center[0].y + x_dy);
    }
    ac_center[1] = *second_center_override;
    ac_axis_start[1] = v5_main_page_internal_toolpath_scaffold_point(ac_center[1].x - c_dx, ac_center[1].y - c_dy);
    ac_axis_end[1] = v5_main_page_internal_toolpath_scaffold_point(ac_center[1].x + c_dx, ac_center[1].y + c_dy);

    v5_main_page_internal_set_toolpath_axis_line(
        page->toolpath_a_axis_line,
        page->toolpath_ac_axis_points[0],
        &ac_axis_start[0],
        &ac_axis_end[0],
        v5_main_page_internal_clip_toolpath_segment(&ac_axis_start[0], &ac_axis_end[0]));
    v5_main_page_internal_set_toolpath_axis_line(
        page->toolpath_c_axis_line,
        page->toolpath_ac_axis_points[1],
        &ac_axis_start[1],
        &ac_axis_end[1],
        v5_main_page_internal_clip_toolpath_segment(&ac_axis_start[1], &ac_axis_end[1]));
    v5_main_page_internal_set_toolpath_v3_center_dot(page->toolpath_a_center_line, &ac_center[0], 1);
    v5_main_page_internal_set_toolpath_v3_center_dot(page->toolpath_c_center_line, &ac_center[1], 1);
    if (page->toolpath_a_label) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_a_label, first_label);
        v5_main_page_internal_set_obj_pos_if_changed(page->toolpath_a_label, V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(ac_axis_end[0].x + 4.0, 0, V5_TOOLPATH_W - 24), V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(ac_axis_end[0].y - 12.0, 0, V5_TOOLPATH_H - 22));
        v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_a_label);
    }
    if (page->toolpath_c_label) {
        v5_main_page_internal_set_label_text_if_changed(page->toolpath_c_label, "C");
        v5_main_page_internal_set_obj_pos_if_changed(page->toolpath_c_label, V5_TOOLPATH_X + v5_main_page_internal_clamp_coord(ac_axis_end[1].x + 8.0, 0, V5_TOOLPATH_W - 20), V5_TOOLPATH_Y + v5_main_page_internal_clamp_coord(ac_axis_end[1].y - 10.0, 0, V5_TOOLPATH_H - 22));
        v5_main_page_internal_clear_hidden_flag_if_hidden(page->toolpath_c_label);
    }
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
    v5_main_page_internal_hide_toolpath_ac_geometry(page);
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
    const V5MotionModelDescriptor *active_model;
    const V5MotionModelDescriptor *wcs_follow_model = 0;
    double wcs_follow_first_center[V5_STATUS_AXIS_COUNT];
    double wcs_follow_second_center[V5_STATUS_AXIS_COUNT];
    double wcs_follow_first_deg = 0.0;
    double wcs_follow_second_deg = 0.0;
    int mcs_valid;
    int wcs_valid;
    int wcs_follow_active_model;
    int axis_ok[3] = {0, 0, 0};
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
                axis_ok[i] = 1;
            }
            v5_main_page_internal_set_toolpath_axis_line(page->toolpath_mcs_axis_lines[i], page->toolpath_mcs_axis_points[i], &origin_point, &axis_point[i], ok);
        }
        if (axis_ok[0] && axis_ok[1] && axis_ok[2]) {
            V5ToolpathScreenPoint first_center_point;
            V5ToolpathScreenPoint second_center_point;
            double first_center_world[V5_STATUS_AXIS_COUNT];
            double second_center_world[V5_STATUS_AXIS_COUNT];
            int active_geometry_valid;
            active_model = v5_main_page_internal_main_page_active_motion_model(page);
            active_geometry_valid = active_model &&
                v5_main_page_internal_main_page_g53_active_center_world(page, 0U, first_center_world) &&
                v5_main_page_internal_main_page_g53_active_center_world(page, 1U, second_center_world);
            if (active_geometry_valid) {
                v5_main_page_internal_main_page_rotate_about_active_model_first_axis(
                    active_model,
                    second_center_world,
                    first_center_world,
                    status->mcs[3]);
            }
            if (active_geometry_valid &&
                v5_main_page_internal_main_page_project_world_point_transformed(page, first_center_world, &first_center_point) &&
                v5_main_page_internal_main_page_project_world_point_transformed(page, second_center_world, &second_center_point)) {
                set_toolpath_active_model_geometry_from_basis(
                    page,
                    active_model,
                    &origin_point,
                    &axis_point[0],
                    &axis_point[1],
                    &axis_point[2],
                    &first_center_point,
                    &second_center_point,
                    status->mcs[3],
                    1);
            } else {
                v5_main_page_internal_hide_toolpath_ac_geometry(page);
            }
        } else {
            v5_main_page_internal_hide_toolpath_ac_geometry(page);
        }
    }

    v5_main_page_internal_hide_toolpath_program_wcs_objects(page);
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
        wcs_follow_active_model = v5_main_page_internal_main_page_rtcp_wcs_follow_active_model_available(
            page,
            status,
            &wcs_follow_model,
            &wcs_follow_first_deg,
            &wcs_follow_second_deg,
            wcs_follow_first_center,
            wcs_follow_second_center);
        if (wcs_follow_active_model) {
            v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
                wcs_follow_model,
                wcs_origin,
                wcs_follow_first_center,
                wcs_follow_second_center,
                wcs_follow_first_deg,
                wcs_follow_second_deg);
            for (i = 0U; i < 3U; ++i) {
                v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
                    wcs_follow_model,
                    wcs_axis[i],
                    wcs_follow_first_center,
                    wcs_follow_second_center,
                    wcs_follow_first_deg,
                    wcs_follow_second_deg);
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
