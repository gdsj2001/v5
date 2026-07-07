#ifndef V5_TOOLPATH_DISPLAY_H
#define V5_TOOLPATH_DISPLAY_H

#include "v5_ui_status_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5ToolpathDisplayPlane {
    V5_TOOLPATH_DISPLAY_XY = 0,
    V5_TOOLPATH_DISPLAY_XZ = 1,
    V5_TOOLPATH_DISPLAY_YZ = 2,
    V5_TOOLPATH_DISPLAY_3D = 3
} V5ToolpathDisplayPlane;

typedef struct V5ToolpathScreenPoint {
    double x;
    double y;
} V5ToolpathScreenPoint;

typedef struct V5ToolpathDisplayBounds {
    int valid;
    double min_u;
    double max_u;
    double min_v;
    double max_v;
} V5ToolpathDisplayBounds;

typedef struct V5ToolpathDisplayFit {
    int valid;
    V5ToolpathDisplayPlane plane;
    V5ToolpathDisplayBounds bounds;
    unsigned int generation;
} V5ToolpathDisplayFit;

typedef struct V5ToolpathDisplaySnapshot {
    int trajectory_valid;
    int mcs_valid;
    int cmd_valid;
    unsigned int point_count;
    V5ToolpathScreenPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    V5ToolpathScreenPoint mcs_point;
    V5ToolpathScreenPoint cmd_point;
} V5ToolpathDisplaySnapshot;

void v5_toolpath_display_fit_init(V5ToolpathDisplayFit *fit);
int v5_toolpath_display_fit_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayFit *fit);
int v5_toolpath_display_fit_from_points(
    const V5StatusPoint *points,
    unsigned int count,
    V5ToolpathDisplayPlane plane,
    V5ToolpathDisplayFit *fit);
unsigned int v5_toolpath_display_project_points_with_fit(
    const V5StatusPoint *points,
    unsigned int count,
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathScreenPoint *screen_points,
    unsigned int screen_capacity);
void v5_toolpath_display_from_status_with_fit(
    const V5UiStatusView *status,
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display);
void v5_toolpath_display_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display);
int v5_toolpath_display_project_world_point(
    const double axis[V5_STATUS_AXIS_COUNT],
    const V5ToolpathDisplayFit *fit,
    double width,
    double height,
    V5ToolpathScreenPoint *point);

#ifdef __cplusplus
}
#endif

#endif
