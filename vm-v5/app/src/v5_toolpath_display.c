#include "v5_toolpath_display.h"

#include <string.h>

static void plane_values(const double axis[V5_STATUS_AXIS_COUNT], V5ToolpathDisplayPlane plane, double *u, double *v)
{
    if (plane == V5_TOOLPATH_DISPLAY_XZ) {
        *u = axis[0];
        *v = axis[2];
    } else if (plane == V5_TOOLPATH_DISPLAY_YZ) {
        *u = axis[1];
        *v = axis[2];
    } else {
        *u = axis[0];
        *v = axis[1];
    }
}

static void expand_bounds(double u, double v, double *min_u, double *max_u, double *min_v, double *max_v)
{
    if (u < *min_u) {
        *min_u = u;
    }
    if (u > *max_u) {
        *max_u = u;
    }
    if (v < *min_v) {
        *min_v = v;
    }
    if (v > *max_v) {
        *max_v = v;
    }
}

static V5ToolpathScreenPoint project(double u, double v, double min_u, double max_u, double min_v, double max_v, double width, double height)
{
    V5ToolpathScreenPoint point;
    double span_u = max_u - min_u;
    double span_v = max_v - min_v;
    if (span_u < 0.001 && span_u > -0.001) {
        span_u = 1.0;
        min_u -= 0.5;
    }
    if (span_v < 0.001 && span_v > -0.001) {
        span_v = 1.0;
        min_v -= 0.5;
    }
    point.x = ((u - min_u) / span_u) * width;
    point.y = height - (((v - min_v) / span_v) * height);
    return point;
}

void v5_toolpath_display_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display)
{
    unsigned int i;
    double u;
    double v;
    double min_u = 0.0;
    double max_u = 0.0;
    double min_v = 0.0;
    double max_v = 0.0;
    int bounds_set = 0;

    if (!display) {
        return;
    }
    memset(display, 0, sizeof(*display));
    if (!status || width <= 0.0 || height <= 0.0) {
        return;
    }

    display->trajectory_valid = (status->valid_mask & V5_STATUS_VALID_TRAJECTORY) != 0u;
    display->mcs_valid = (status->valid_mask & V5_STATUS_VALID_MCS) != 0u;
    display->cmd_valid = (status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0u;

    if (display->trajectory_valid) {
        display->point_count = status->trajectory_count;
        if (display->point_count > V5_STATUS_TRAJECTORY_POINT_COUNT) {
            display->point_count = V5_STATUS_TRAJECTORY_POINT_COUNT;
        }
        for (i = 0; i < display->point_count; ++i) {
            plane_values(status->trajectory[i].axis, plane, &u, &v);
            if (!bounds_set) {
                min_u = max_u = u;
                min_v = max_v = v;
                bounds_set = 1;
            } else {
                expand_bounds(u, v, &min_u, &max_u, &min_v, &max_v);
            }
        }
    }
    if (display->mcs_valid) {
        plane_values(status->mcs, plane, &u, &v);
        if (!bounds_set) {
            min_u = max_u = u;
            min_v = max_v = v;
            bounds_set = 1;
        } else {
            expand_bounds(u, v, &min_u, &max_u, &min_v, &max_v);
        }
    }
    if (display->cmd_valid) {
        plane_values(status->cmd_mcs, plane, &u, &v);
        if (!bounds_set) {
            min_u = max_u = u;
            min_v = max_v = v;
            bounds_set = 1;
        } else {
            expand_bounds(u, v, &min_u, &max_u, &min_v, &max_v);
        }
    }
    if (!bounds_set) {
        return;
    }

    for (i = 0; i < display->point_count; ++i) {
        plane_values(status->trajectory[i].axis, plane, &u, &v);
        display->trajectory[i] = project(u, v, min_u, max_u, min_v, max_v, width, height);
    }
    if (display->mcs_valid) {
        plane_values(status->mcs, plane, &u, &v);
        display->mcs_point = project(u, v, min_u, max_u, min_v, max_v, width, height);
    }
    if (display->cmd_valid) {
        plane_values(status->cmd_mcs, plane, &u, &v);
        display->cmd_point = project(u, v, min_u, max_u, min_v, max_v, width, height);
    }
}
