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

static void main_page_expand_static_geometry_fit(
    V5MainPage *page,
    const V5UiStatusView *status,
    V5ToolpathDisplayFit *fit)
{
    double point[V5_STATUS_AXIS_COUNT];
    double wcs_origin[V5_STATUS_AXIS_COUNT];
    double wcs_axis[3][V5_STATUS_AXIS_COUNT];
    const V5MotionModelDescriptor *model;
    double first_center[V5_STATUS_AXIS_COUNT];
    double second_center[V5_STATUS_AXIS_COUNT];
    double first_deg = 0.0;
    double second_deg = 0.0;
    double pose_first_deg;
    double angle;
    double c_vec_x;
    double c_vec_y;
    double c_vec_z;
    int first_center_valid;
    int wcs_follow_model;
    unsigned int i;
    if (!page || !fit || !fit->valid) {
        return;
    }
    if (v5_native_readback_wcs_offset_known(&page->native_readback)) {
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
        wcs_follow_model = v5_main_page_internal_main_page_rtcp_wcs_follow_active_model_available(
            page, status, &model, &first_deg, &second_deg, first_center, second_center);
        if (wcs_follow_model) {
            v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
                model, wcs_origin, first_center, second_center, first_deg, second_deg);
        }
        v5_main_page_internal_main_page_fit_expand_world_point(fit, wcs_origin);
        for (i = 0U; i < 3U; ++i) {
            if (wcs_follow_model) {
                v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
                    model, wcs_axis[i], first_center, second_center, first_deg, second_deg);
            }
            v5_main_page_internal_main_page_fit_expand_world_point(fit, wcs_axis[i]);
        }
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    first_center_valid = v5_main_page_internal_main_page_g53_active_center_world(page, 0U, first_center);
    if (first_center_valid) {
        point[0] = first_center[0];
        point[1] = first_center[1];
        point[2] = first_center[2];
        point[3] = 0.0;
        point[4] = 0.0;
        point[model->first_world_axis_component] -= 40.0;
        v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
        point[0] = first_center[0];
        point[1] = first_center[1];
        point[2] = first_center[2];
        point[model->first_world_axis_component] += 40.0;
        v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
    }
    if (v5_main_page_internal_main_page_g53_active_center_world(page, 1U, second_center)) {
        pose_first_deg = (status && (status->valid_mask & V5_STATUS_VALID_MCS) && isfinite(status->mcs[3])) ? status->mcs[3] : 0.0;
        angle = pose_first_deg * M_PI / 180.0;
        if (first_center_valid) {
            v5_main_page_internal_main_page_rotate_about_active_model_first_axis(
                model,
                second_center,
                first_center,
                pose_first_deg);
        }
        c_vec_x = model->first_world_axis_component == 1U ? sin(angle) * 40.0 : 0.0;
        c_vec_y = model->first_world_axis_component == 0U ? -sin(angle) * 40.0 : 0.0;
        c_vec_z = cos(angle) * 40.0;
        point[0] = second_center[0] - c_vec_x; point[1] = second_center[1] - c_vec_y; point[2] = second_center[2] - c_vec_z; point[3] = 0.0; point[4] = 0.0;
        v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
        point[0] = second_center[0] + c_vec_x;
        point[1] = second_center[1] + c_vec_y;
        point[2] = second_center[2] + c_vec_z;
        v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
    }
}

void v5_main_page_internal_main_page_expand_visible_toolpath_fit(
    V5MainPage *page,
    const V5UiStatusView *status,
    V5ToolpathDisplayFit *fit)
{
    double origin[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double x_axis[V5_STATUS_AXIS_COUNT] = {40.0, 0.0, 0.0, 0.0, 0.0};
    double y_axis[V5_STATUS_AXIS_COUNT] = {0.0, 40.0, 0.0, 0.0, 0.0};
    double z_axis[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 40.0, 0.0, 0.0};
    double tool_len = 0.0;
    unsigned int i;

    if (!page || !fit || !fit->valid) {
        return;
    }

    v5_main_page_internal_main_page_fit_expand_world_point(fit, origin);
    v5_main_page_internal_main_page_fit_expand_world_point(fit, x_axis);
    v5_main_page_internal_main_page_fit_expand_world_point(fit, y_axis);
    v5_main_page_internal_main_page_fit_expand_world_point(fit, z_axis);

    if (page->toolpath_program_visible) {
        for (i = 0U; i < page->toolpath_program_point_count; ++i) {
            v5_main_page_internal_main_page_fit_expand_world_point(fit, page->toolpath_program_project_points[i].axis);
        }
    }

    if (status && (status->valid_mask & V5_STATUS_VALID_MCS) != 0U && v5_main_page_internal_main_page_axis_values_finite(status->mcs)) {
        double holder_end[V5_STATUS_AXIS_COUNT];
        v5_main_page_internal_main_page_fit_expand_world_point(fit, status->mcs);
        if (v5_main_page_internal_main_page_tool_length_mm(page, &tool_len) && fabs(tool_len) > 1.0e-9) {
            memcpy(holder_end, status->mcs, sizeof(holder_end));
            holder_end[2] -= tool_len;
            v5_main_page_internal_main_page_fit_expand_world_point(fit, holder_end);
        }
    }

    if (status && (status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U && v5_main_page_internal_main_page_axis_values_finite(status->cmd_mcs)) {
        double cmd_tip[V5_STATUS_AXIS_COUNT];
        memcpy(cmd_tip, status->cmd_mcs, sizeof(cmd_tip));
        if (v5_main_page_internal_main_page_tool_length_mm(page, &tool_len) && fabs(tool_len) > 1.0e-9) {
            cmd_tip[2] -= tool_len;
        }
        v5_main_page_internal_main_page_fit_expand_world_point(fit, cmd_tip);
    }

    main_page_expand_static_geometry_fit(page, status, fit);
}

static int main_page_fit_candidate_outside_window(
    const V5ToolpathDisplayFit *current,
    const V5ToolpathDisplayFit *candidate)
{
    double span_u;
    double span_v;
    double margin_u;
    double margin_v;

    if (!current || !candidate || !current->valid || !candidate->valid ||
        !current->bounds.valid || !candidate->bounds.valid || current->plane != candidate->plane) {
        return 0;
    }
    span_u = current->bounds.max_u - current->bounds.min_u;
    span_v = current->bounds.max_v - current->bounds.min_v;
    if (fabs(span_u) < 0.001) span_u = 1.0;
    if (fabs(span_v) < 0.001) span_v = 1.0;
    margin_u = fabs(span_u) * 0.125;
    margin_v = fabs(span_v) * 0.125;
    return candidate->bounds.min_u < current->bounds.min_u - margin_u ||
           candidate->bounds.max_u > current->bounds.max_u + margin_u ||
           candidate->bounds.min_v < current->bounds.min_v - margin_v ||
           candidate->bounds.max_v > current->bounds.max_v + margin_v;
}

static int main_page_world_point_outside_fit_window(
    const V5MainPage *page,
    const double world[V5_STATUS_AXIS_COUNT])
{
    V5ToolpathScreenPoint point;

    if (!page || !world || !page->toolpath_fit.valid ||
        !v5_toolpath_display_project_world_point(
            world,
            &page->toolpath_fit,
            (double)V5_TOOLPATH_W,
            (double)V5_TOOLPATH_H,
            &point)) {
        return 0;
    }
    return point.x < 0.0 || point.x > (double)V5_TOOLPATH_W ||
           point.y < 0.0 || point.y > (double)V5_TOOLPATH_H;
}

int v5_main_page_internal_main_page_dynamic_toolpath_outside_fit_window(
    const V5MainPage *page,
    const V5UiStatusView *status)
{
    double point[V5_STATUS_AXIS_COUNT];
    double tool_len = 0.0;
    int tool_len_valid;

    if (!page || !status || !page->toolpath_fit.valid) {
        return 0;
    }
    tool_len_valid = v5_main_page_internal_main_page_tool_length_mm(page, &tool_len) && fabs(tool_len) > 1.0e-9;
    if ((status->valid_mask & V5_STATUS_VALID_MCS) != 0U && v5_main_page_internal_main_page_axis_values_finite(status->mcs)) {
        if (main_page_world_point_outside_fit_window(page, status->mcs)) {
            return 1;
        }
        if (tool_len_valid) {
            memcpy(point, status->mcs, sizeof(point));
            point[2] -= tool_len;
            if (main_page_world_point_outside_fit_window(page, point)) {
                return 1;
            }
        }
    }
    if ((status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U && v5_main_page_internal_main_page_axis_values_finite(status->cmd_mcs)) {
        memcpy(point, status->cmd_mcs, sizeof(point));
        if (tool_len_valid) {
            point[2] -= tool_len;
        }
        if (main_page_world_point_outside_fit_window(page, point)) {
            return 1;
        }
    }
    return 0;
}

int v5_main_page_internal_main_page_program_outside_fit_window(const V5MainPage *page)
{
    unsigned int i;

    if (!page || !page->toolpath_fit.valid || !page->toolpath_program_visible) {
        return 0;
    }
    for (i = 0U; i < page->toolpath_program_point_count; ++i) {
        if (main_page_world_point_outside_fit_window(
                page,
                page->toolpath_program_project_points[i].axis)) {
            return 1;
        }
    }
    return 0;
}

int v5_main_page_internal_main_page_static_geometry_outside_fit_window(
    V5MainPage *page,
    const V5UiStatusView *status)
{
    V5ToolpathDisplayFit candidate;

    if (!page || !page->toolpath_fit.valid || !page->toolpath_fit.bounds.valid) {
        return 0;
    }
    candidate = page->toolpath_fit;
    main_page_expand_static_geometry_fit(page, status, &candidate);
    return main_page_fit_candidate_outside_window(&page->toolpath_fit, &candidate);
}

int v5_main_page_internal_main_page_expand_fit_on_overflow(V5MainPage *page, const V5UiStatusView *status)
{
    V5ToolpathDisplayFit candidate;

    if (!page || !page->toolpath_fit.valid || !page->toolpath_fit.bounds.valid) {
        return 0;
    }
    candidate = page->toolpath_fit;
    v5_main_page_internal_main_page_expand_visible_toolpath_fit(page, status, &candidate);
    if (!main_page_fit_candidate_outside_window(&page->toolpath_fit, &candidate)) {
        return 0;
    }
    candidate.generation = page->toolpath_fit.generation + 1U;
    if (candidate.generation == 0U) {
        candidate.generation = 1U;
    }
    page->toolpath_fit = candidate;
    return 1;
}
