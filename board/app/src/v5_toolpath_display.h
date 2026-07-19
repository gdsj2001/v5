#ifndef V5_TOOLPATH_DISPLAY_H
#define V5_TOOLPATH_DISPLAY_H

#include "v5_ui_status_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5ToolpathDisplayPlane {
    V5_TOOLPATH_DISPLAY_XY = V5_STATUS_SCENE_PLANE_XY,
    V5_TOOLPATH_DISPLAY_XZ = V5_STATUS_SCENE_PLANE_XZ,
    V5_TOOLPATH_DISPLAY_YZ = V5_STATUS_SCENE_PLANE_YZ,
    V5_TOOLPATH_DISPLAY_3D = V5_STATUS_SCENE_PLANE_3D
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

#ifdef __cplusplus
}
#endif

#endif
