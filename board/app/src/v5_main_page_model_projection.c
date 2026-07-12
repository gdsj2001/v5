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
    if (!page->selection.all_axes && old_fourth && page->selection.axis == old_fourth &&
        old_fourth != new_fourth && new_fourth != '-') {
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

int v5_main_page_internal_main_page_g53_active_center_world(
    const V5MainPage *page,
    unsigned int index,
    double center[V5_STATUS_AXIS_COUNT])
{
    const V5MotionModelDescriptor *model;
    const double *source;
    const double *active_offsets;
    unsigned int native_center;
    unsigned int i;
    if (!page || !center) {
        return 0;
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    if (!model) {
        return 0;
    }
    if (index > 1U) {
        return 0;
    }
    active_offsets = v5_native_readback_active_wcs_offsets(&page->native_readback);
    if (!active_offsets ||
        !isfinite(active_offsets[0]) ||
        !isfinite(active_offsets[1]) ||
        !isfinite(active_offsets[2])) {
        return 0;
    }
    native_center = index == 0U ? model->first_g53_center : model->second_g53_center;
    source = v5_native_readback_g53_center(&page->native_readback, native_center);
    if (!source) {
        return 0;
    }
    memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
    for (i = 0U; i < V5_NATIVE_READBACK_G53_AXIS_COUNT && i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(source[i])) {
            memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
            return 0;
        }
        center[i] = source[i];
    }
    if (index == 0U) {
        center[model->first_center_wcs_component] = active_offsets[model->first_center_wcs_component];
    } else {
        center[model->second_center_wcs_component] = active_offsets[model->second_center_wcs_component];
    }
    return 1;
}

int v5_main_page_internal_main_page_project_world_point_transformed(
    const V5MainPage *page,
    const double world[V5_STATUS_AXIS_COUNT],
    V5ToolpathScreenPoint *point)
{
    if (!page || !world || !point || !page->toolpath_fit.valid ||
        !v5_toolpath_display_project_world_point(world, &page->toolpath_fit, (double)V5_TOOLPATH_W, (double)V5_TOOLPATH_H, point)) {
        return 0;
    }
    *point = v5_main_page_internal_apply_toolpath_view_transform(page, *point);
    return 1;
}

static int main_page_status_active_model_display_values(
    const V5UiStatusView *status,
    double *first_deg,
    double *second_deg)
{
    if (first_deg) {
        *first_deg = 0.0;
    }
    if (second_deg) {
        *second_deg = 0.0;
    }
    if (!status || (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[3]) || !isfinite(status->mcs[4])) {
        return 0;
    }
    if (first_deg) {
        *first_deg = status->mcs[3];
    }
    if (second_deg) {
        *second_deg = status->mcs[4];
    }
    return 1;
}

static int main_page_program_active_model_projection_available(
    const V5MainPage *page,
    const V5UiStatusView *status,
    double *first_deg,
    double *second_deg,
    double first_center[V5_STATUS_AXIS_COUNT],
    double second_center[V5_STATUS_AXIS_COUNT])
{
    if (!page ||
        !v5_main_page_internal_main_page_active_motion_model(page) ||
        !main_page_status_active_model_display_values(status, first_deg, second_deg) ||
        !v5_main_page_internal_main_page_g53_active_center_world(page, 0U, first_center) ||
        !v5_main_page_internal_main_page_g53_active_center_world(page, 1U, second_center)) {
        return 0;
    }
    return 1;
}

int v5_main_page_internal_main_page_program_ac_projection_changed(const V5MainPage *page, const V5UiStatusView *status)
{
    double first_deg = 0.0;
    double second_deg = 0.0;
    double first_center[V5_STATUS_AXIS_COUNT];
    double second_center[V5_STATUS_AXIS_COUNT];
    const V5MotionModelDescriptor *model;
    int valid;

    if (!page || !page->toolpath_program_visible || page->toolpath_program_point_count == 0U) {
        return 0;
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    valid = main_page_program_active_model_projection_available(
        page, status, &first_deg, &second_deg, first_center, second_center);
    if (valid != page->toolpath_program_ac_valid) {
        return 1;
    }
    if (!valid) {
        return 0;
    }
    if (page->toolpath_program_model_kind != (int)model->registry_id) {
        return 1;
    }
    return fabs(first_deg - page->toolpath_program_ac_a_deg) > 0.0005 ||
           fabs(second_deg - page->toolpath_program_ac_c_deg) > 0.0005;
}

int v5_main_page_internal_main_page_static_pose_changed(const V5MainPage *page, const V5UiStatusView *status)
{
    double first_deg = 0.0;
    double second_deg = 0.0;
    const V5MotionModelDescriptor *model = page ? v5_main_page_internal_main_page_active_motion_model(page) : 0;
    int valid = model != 0 &&
        main_page_status_active_model_display_values(status, &first_deg, &second_deg);
    if (!page) {
        return 0;
    }
    if (valid != page->toolpath_static_pose_valid) {
        return 1;
    }
    if (!valid) {
        return 0;
    }
    if (page->toolpath_static_model_kind != (int)model->registry_id) {
        return 1;
    }
    return fabs(first_deg - page->toolpath_static_pose_a_deg) > 0.0005 ||
           fabs(second_deg - page->toolpath_static_pose_c_deg) > 0.0005;
}

void v5_main_page_internal_main_page_store_static_pose(V5MainPage *page, const V5UiStatusView *status)
{
    double first_deg = 0.0;
    double second_deg = 0.0;
    const V5MotionModelDescriptor *model;
    int valid;
    if (!page) {
        return;
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    valid = model != 0 &&
        main_page_status_active_model_display_values(status, &first_deg, &second_deg);
    page->toolpath_static_pose_valid = valid;
    page->toolpath_static_model_kind = valid ? (int)model->registry_id : 0;
    page->toolpath_static_pose_a_deg = valid ? first_deg : 0.0;
    page->toolpath_static_pose_c_deg = valid ? second_deg : 0.0;
}

static void main_page_rotate_xyz_about_axis(
    double point[V5_STATUS_AXIS_COUNT],
    const double center[V5_STATUS_AXIS_COUNT],
    const double axis[3],
    double angle_rad)
{
    double vx;
    double vy;
    double vz;
    double kx;
    double ky;
    double kz;
    double norm;
    double c;
    double s;
    double dot;
    double cross_x;
    double cross_y;
    double cross_z;

    if (!point || !center || !axis) {
        return;
    }
    norm = sqrt((axis[0] * axis[0]) + (axis[1] * axis[1]) + (axis[2] * axis[2]));
    if (norm <= 1.0e-12 || !isfinite(norm) || !isfinite(angle_rad)) {
        return;
    }
    kx = axis[0] / norm;
    ky = axis[1] / norm;
    kz = axis[2] / norm;
    vx = point[0] - center[0];
    vy = point[1] - center[1];
    vz = point[2] - center[2];
    c = cos(angle_rad);
    s = sin(angle_rad);
    dot = (kx * vx) + (ky * vy) + (kz * vz);
    cross_x = (ky * vz) - (kz * vy);
    cross_y = (kz * vx) - (kx * vz);
    cross_z = (kx * vy) - (ky * vx);
    point[0] = center[0] + (vx * c) + (cross_x * s) + (kx * dot * (1.0 - c));
    point[1] = center[1] + (vy * c) + (cross_y * s) + (ky * dot * (1.0 - c));
    point[2] = center[2] + (vz * c) + (cross_z * s) + (kz * dot * (1.0 - c));
}

void v5_main_page_internal_main_page_rotate_about_active_model_first_axis(
    const V5MotionModelDescriptor *model,
    double point[V5_STATUS_AXIS_COUNT],
    const double center[V5_STATUS_AXIS_COUNT],
    double first_deg)
{
    double first_axis[3] = {0.0, 0.0, 0.0};

    if (!model || !point || !center || !isfinite(first_deg) ||
        model->first_world_axis_component >= 3U) {
        return;
    }
    first_axis[model->first_world_axis_component] = 1.0;
    main_page_rotate_xyz_about_axis(point, center, first_axis, first_deg * M_PI / 180.0);
}

void v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
    const V5MotionModelDescriptor *model,
    double point[V5_STATUS_AXIS_COUNT],
    const double first_center[V5_STATUS_AXIS_COUNT],
    const double second_center[V5_STATUS_AXIS_COUNT],
    double first_deg,
    double second_deg)
{
    double base_z[3] = {0.0, 0.0, 1.0};

    if (!point || !first_center || !second_center ||
        !isfinite(first_deg) || !isfinite(second_deg) ||
        !model || model->first_world_axis_component >= 3U) {
        return;
    }
    main_page_rotate_xyz_about_axis(point, second_center, base_z, second_deg * M_PI / 180.0);
    v5_main_page_internal_main_page_rotate_about_active_model_first_axis(model, point, first_center, first_deg);
}

int v5_main_page_internal_main_page_rtcp_wcs_follow_active_model_available(
    const V5MainPage *page,
    const V5UiStatusView *status,
    const V5MotionModelDescriptor **model,
    double *first_deg,
    double *second_deg,
    double first_center[V5_STATUS_AXIS_COUNT],
    double second_center[V5_STATUS_AXIS_COUNT])
{
    const V5MotionModelDescriptor *active_model;
    if (!page ||
        !v5_native_readback_rtcp_known(&page->native_readback) ||
        !page->native_readback.rtcp_enabled) {
        return 0;
    }
    active_model = v5_main_page_internal_main_page_active_motion_model(page);
    if (!active_model ||
        !main_page_program_active_model_projection_available(
            page, status, first_deg, second_deg, first_center, second_center)) {
        return 0;
    }
    if (model) {
        *model = active_model;
    }
    return 1;
}

int v5_main_page_internal_main_page_update_program_project_points(
    V5MainPage *page,
    const V5UiStatusView *status,
    unsigned int count)
{
    const V5MotionModelDescriptor *model;
    double first_deg = 0.0;
    double second_deg = 0.0;
    int model_valid;
    double first_center[V5_STATUS_AXIS_COUNT];
    double second_center[V5_STATUS_AXIS_COUNT];
    unsigned int i;

    if (!page) {
        return 0;
    }
    if (count > V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT) {
        count = V5_MAIN_PAGE_PROGRAM_TRAJECTORY_POINT_COUNT;
    }
    for (i = 0U; i < count; ++i) {
        page->toolpath_program_project_points[i] = page->toolpath_program_points[i];
    }
    model = v5_main_page_internal_main_page_active_motion_model(page);
    model_valid = main_page_program_active_model_projection_available(
        page, status, &first_deg, &second_deg, first_center, second_center);
    page->toolpath_program_ac_valid = model_valid;
    page->toolpath_program_model_kind = model_valid ? (int)model->registry_id : 0;
    page->toolpath_program_ac_a_deg = model_valid ? first_deg : 0.0;
    page->toolpath_program_ac_c_deg = model_valid ? second_deg : 0.0;
    if (!model_valid) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        v5_main_page_internal_main_page_apply_active_model_pose_to_world_point(
            model,
            page->toolpath_program_project_points[i].axis,
            first_center,
            second_center,
            first_deg,
            second_deg);
    }
    return 1;
}
