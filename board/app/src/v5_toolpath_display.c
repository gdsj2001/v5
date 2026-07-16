#include "v5_toolpath_display.h"

#include <math.h>
#include <string.h>

#define V5_TOOLPATH_3D_RIGHT_X 0.7071067811865476
#define V5_TOOLPATH_3D_RIGHT_Y 0.7071067811865476
#define V5_TOOLPATH_3D_RIGHT_Z 0.0
#define V5_TOOLPATH_3D_UP_X (-0.4082482904638631)
#define V5_TOOLPATH_3D_UP_Y 0.4082482904638631
#define V5_TOOLPATH_3D_UP_Z 0.8164965809277261

static int axis_values_finite(const double axis[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    if (!axis) {
        return 0;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(axis[i])) {
            return 0;
        }
    }
    return 1;
}

static void plane_values(const double axis[V5_STATUS_AXIS_COUNT], V5ToolpathDisplayPlane plane, double *u, double *v)
{
    if (plane == V5_TOOLPATH_DISPLAY_XZ) {
        *u = axis[0];
        *v = axis[2];
    } else if (plane == V5_TOOLPATH_DISPLAY_YZ) {
        *u = axis[1];
        *v = axis[2];
    } else if (plane == V5_TOOLPATH_DISPLAY_3D) {
        *u =
            (axis[0] * V5_TOOLPATH_3D_RIGHT_X) +
            (axis[1] * V5_TOOLPATH_3D_RIGHT_Y) +
            (axis[2] * V5_TOOLPATH_3D_RIGHT_Z);
        *v =
            (axis[0] * V5_TOOLPATH_3D_UP_X) +
            (axis[1] * V5_TOOLPATH_3D_UP_Y) +
            (axis[2] * V5_TOOLPATH_3D_UP_Z);
    } else {
        *u = axis[0];
        *v = axis[1];
    }
}

static void expand_bounds(double u, double v, V5ToolpathDisplayBounds *bounds)
{
    if (!bounds) {
        return;
    }
    if (!bounds->valid) {
        bounds->min_u = bounds->max_u = u;
        bounds->min_v = bounds->max_v = v;
        bounds->valid = 1;
        return;
    }
    if (u < bounds->min_u) {
        bounds->min_u = u;
    }
    if (u > bounds->max_u) {
        bounds->max_u = u;
    }
    if (v < bounds->min_v) {
        bounds->min_v = v;
    }
    if (v > bounds->max_v) {
        bounds->max_v = v;
    }
}

static void expand_axis_bounds(
    const double axis[V5_STATUS_AXIS_COUNT],
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayBounds *bounds)
{
    double u;
    double v;
    if (!axis_values_finite(axis)) {
        return;
    }
    plane_values(axis, plane, &u, &v);
    expand_bounds(u, v, bounds);
}

static V5ToolpathScreenPoint project(double u, double v, V5ToolpathDisplayBounds bounds, double width, double height)
{
    V5ToolpathScreenPoint point;
    double span_u = bounds.max_u - bounds.min_u;
    double span_v = bounds.max_v - bounds.min_v;
    if (span_u < 0.001 && span_u > -0.001) {
        span_u = 1.0;
        bounds.min_u -= 0.5;
    }
    if (span_v < 0.001 && span_v > -0.001) {
        span_v = 1.0;
        bounds.min_v -= 0.5;
    }
    {
        double pad_x = width * 0.10;
        double pad_y = height * 0.10;
        double draw_w = width - (pad_x * 2.0);
        double draw_h = height - (pad_y * 2.0);
        double scale;
        double content_w;
        double content_h;
        double origin_x;
        double origin_y;
        if (draw_w < 1.0) {
            draw_w = width;
            pad_x = 0.0;
        }
        if (draw_h < 1.0) {
            draw_h = height;
            pad_y = 0.0;
        }
        scale = draw_w / span_u;
        if ((draw_h / span_v) < scale) {
            scale = draw_h / span_v;
        }
        content_w = span_u * scale;
        content_h = span_v * scale;
        origin_x = pad_x + ((draw_w - content_w) * 0.5);
        origin_y = pad_y + ((draw_h - content_h) * 0.5);
        point.x = origin_x + ((u - bounds.min_u) * scale);
        point.y = origin_y + content_h - ((v - bounds.min_v) * scale);
    }
    return point;
}

void v5_toolpath_display_fit_init(V5ToolpathDisplayFit *fit)
{
    if (!fit) {
        return;
    }
    memset(fit, 0, sizeof(*fit));
}

static void expand_point_array_bounds(
    const V5StatusPoint *points,
    unsigned int count,
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayBounds *bounds)
{
    unsigned int i;
    if (!points || !bounds) {
        return;
    }
    for (i = 0U; i < count; ++i) {
        expand_axis_bounds(points[i].axis, plane, bounds);
    }
}

int v5_toolpath_display_fit_from_points(
    const V5StatusPoint *points,
    unsigned int count,
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayFit *fit)
{
    V5ToolpathDisplayBounds bounds;

    if (!fit) {
        return 0;
    }
    memset(&bounds, 0, sizeof(bounds));
    if (!points || count == 0U) {
        v5_toolpath_display_fit_init(fit);
        return 0;
    }
    expand_point_array_bounds(points, count, plane, &bounds);
    if (!bounds.valid) {
        v5_toolpath_display_fit_init(fit);
        return 0;
    }
    fit->valid = 1;
    fit->plane = plane;
    fit->bounds = bounds;
    fit->generation += 1U;
    if (fit->generation == 0U) {
        fit->generation = 1U;
    }
    return 1;
}

int v5_toolpath_display_fit_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayFit *fit)
{
    V5ToolpathDisplayBounds bounds;

    if (!fit) {
        return 0;
    }
    memset(&bounds, 0, sizeof(bounds));
    if (!status) {
        v5_toolpath_display_fit_init(fit);
        return 0;
    }

    if (status->valid_mask & V5_STATUS_VALID_TRAJECTORY) {
        unsigned int count = status->trajectory_count;
        if (count > V5_STATUS_TRAJECTORY_POINT_COUNT) {
            count = V5_STATUS_TRAJECTORY_POINT_COUNT;
        }
        expand_point_array_bounds(status->trajectory, count, plane, &bounds);
    }
    if (status->valid_mask & V5_STATUS_VALID_MCS) {
        double origin[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
        double x_axis[V5_STATUS_AXIS_COUNT] = {40.0, 0.0, 0.0, 0.0, 0.0};
        double y_axis[V5_STATUS_AXIS_COUNT] = {0.0, 40.0, 0.0, 0.0, 0.0};
        double z_axis[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 40.0, 0.0, 0.0};
        expand_axis_bounds(origin, plane, &bounds);
        expand_axis_bounds(x_axis, plane, &bounds);
        expand_axis_bounds(y_axis, plane, &bounds);
        expand_axis_bounds(z_axis, plane, &bounds);
        expand_axis_bounds(status->mcs, plane, &bounds);
    }
    if (status->valid_mask & V5_STATUS_VALID_CMD_MCS) {
        expand_axis_bounds(status->cmd_mcs, plane, &bounds);
    }

    if (!bounds.valid) {
        v5_toolpath_display_fit_init(fit);
        return 0;
    }
    fit->valid = 1;
    fit->plane = plane;
    fit->bounds = bounds;
    fit->generation += 1U;
    if (fit->generation == 0U) {
        fit->generation = 1U;
    }
    return 1;
}

void v5_toolpath_display_from_status_with_fit(
    const V5UiStatusView *status,
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display)
{
    unsigned int i;
    double u;
    double v;

    if (!display) {
        return;
    }
    memset(display, 0, sizeof(*display));
    if (!status || !fit || !fit->valid || !fit->bounds.valid || width <= 0.0 || height <= 0.0) {
        return;
    }

    display->trajectory_valid = (status->valid_mask & V5_STATUS_VALID_TRAJECTORY) != 0u;
    display->mcs_valid = ((status->valid_mask & V5_STATUS_VALID_MCS) != 0u) && axis_values_finite(status->mcs);
    display->cmd_valid = ((status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0u) && axis_values_finite(status->cmd_mcs);

    if (display->trajectory_valid) {
        display->point_count = status->trajectory_count;
        if (display->point_count > V5_STATUS_TRAJECTORY_POINT_COUNT) {
            display->point_count = V5_STATUS_TRAJECTORY_POINT_COUNT;
        }
        for (i = 0U; i < display->point_count; ++i) {
            if (!axis_values_finite(status->trajectory[i].axis)) {
                display->point_count = i;
                break;
            }
            plane_values(status->trajectory[i].axis, fit->plane, &u, &v);
            display->trajectory[i] = project(u, v, fit->bounds, width, height);
        }
        display->trajectory_valid = display->point_count > 0U;
    }
    if (display->mcs_valid) {
        plane_values(status->mcs, fit->plane, &u, &v);
        display->mcs_point = project(u, v, fit->bounds, width, height);
    }
    if (display->cmd_valid) {
        plane_values(status->cmd_mcs, fit->plane, &u, &v);
        display->cmd_point = project(u, v, fit->bounds, width, height);
    }
}

unsigned int v5_toolpath_display_project_points_with_fit(
    const V5StatusPoint *points,
    unsigned int count,
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathScreenPoint *screen_points,
    unsigned int screen_capacity)
{
    unsigned int i;
    unsigned int out_count = 0U;
    double u;
    double v;

    if (!points || !screen_points || !fit || !fit->valid || !fit->bounds.valid || width <= 0.0 || height <= 0.0) {
        return 0U;
    }
    for (i = 0U; i < count && out_count < screen_capacity; ++i) {
        if (!axis_values_finite(points[i].axis)) {
            break;
        }
        plane_values(points[i].axis, fit->plane, &u, &v);
        screen_points[out_count++] = project(u, v, fit->bounds, width, height);
    }
    return out_count;
}

int v5_toolpath_display_project_world_point(
    const double axis[V5_STATUS_AXIS_COUNT],
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathScreenPoint *point)
{
    double u;
    double v;
    if (!point || !axis_values_finite(axis) || !fit || !fit->valid || !fit->bounds.valid || width <= 0.0 || height <= 0.0) {
        return 0;
    }
    plane_values(axis, fit->plane, &u, &v);
    *point = project(u, v, fit->bounds, width, height);
    return 1;
}

void v5_toolpath_display_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display)
{
    V5ToolpathDisplayFit fit;
    v5_toolpath_display_fit_init(&fit);
    if (v5_toolpath_display_fit_from_status(status, plane, &fit)) {
        v5_toolpath_display_from_status_with_fit(status, &fit, width, height, display);
        return;
    }
    if (display) {
        memset(display, 0, sizeof(*display));
    }
}
