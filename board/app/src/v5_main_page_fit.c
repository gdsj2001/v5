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
    const V5MainPageModelScene *wcs_follow_scene = 0;
    V5MainPageModelGeometry geometry;
    int wcs_follow_model;
    int rtcp_requires_model_scene;
    unsigned int i;
    unsigned int axis_i;
    unsigned int component_i;

    if (!page || !fit || !fit->valid) {
        return;
    }
    rtcp_requires_model_scene =
        v5_native_readback_rtcp_known(&page->native_readback) &&
        page->native_readback.rtcp_enabled;
    if (v5_native_readback_wcs_offset_known(&page->native_readback) &&
        !(rtcp_requires_model_scene && !page->toolpath_model_scene_fresh)) {
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
        wcs_follow_model =
            v5_main_page_internal_main_page_rtcp_wcs_follow_model_scene_available(
                page,
                &wcs_follow_scene);
        if (wcs_follow_model) {
            v5_main_page_model_scene_transform_world_point(wcs_follow_scene, wcs_origin);
        }
        v5_main_page_internal_main_page_fit_expand_world_point(fit, wcs_origin);
        for (i = 0U; i < 3U; ++i) {
            if (wcs_follow_model) {
                v5_main_page_model_scene_transform_world_point(
                    wcs_follow_scene,
                    wcs_axis[i]);
            }
            v5_main_page_internal_main_page_fit_expand_world_point(fit, wcs_axis[i]);
        }
    }
    if (page->toolpath_model_scene_valid &&
        page->toolpath_model_scene_fresh &&
        v5_main_page_model_scene_build_geometry(
            &page->toolpath_model_scene,
            &geometry)) {
        const double *centers[2] = {
            geometry.primary_center,
            geometry.child_center,
        };
        const double *directions[2] = {
            geometry.primary_direction,
            geometry.child_direction,
        };
        for (axis_i = 0U; axis_i < 2U; ++axis_i) {
            memset(point, 0, sizeof(point));
            for (component_i = 0U; component_i < 3U; ++component_i) {
                point[component_i] =
                    centers[axis_i][component_i] -
                    (directions[axis_i][component_i] * 40.0);
            }
            v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
            for (component_i = 0U; component_i < 3U; ++component_i) {
                point[component_i] =
                    centers[axis_i][component_i] +
                    (directions[axis_i][component_i] * 40.0);
            }
            v5_main_page_internal_main_page_fit_expand_world_point(fit, point);
        }
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
