#ifndef V5_TOOLPATH_DISPLAY_H
#define V5_TOOLPATH_DISPLAY_H

#include "v5_ui_status_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5ToolpathDisplayPlane {
    V5_TOOLPATH_DISPLAY_XY = 0,
    V5_TOOLPATH_DISPLAY_XZ = 1,
    V5_TOOLPATH_DISPLAY_YZ = 2
} V5ToolpathDisplayPlane;

typedef struct V5ToolpathScreenPoint {
    double x;
    double y;
} V5ToolpathScreenPoint;

typedef struct V5ToolpathDisplaySnapshot {
    int trajectory_valid;
    int mcs_valid;
    int cmd_valid;
    unsigned int point_count;
    V5ToolpathScreenPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    V5ToolpathScreenPoint mcs_point;
    V5ToolpathScreenPoint cmd_point;
} V5ToolpathDisplaySnapshot;

void v5_toolpath_display_from_status(
    const V5UiStatusView *status,
    V5ToolpathDisplayPlane plane,
    double width,
    double height,
    V5ToolpathDisplaySnapshot *display);

#ifdef __cplusplus
}
#endif

#endif
