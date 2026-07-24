#ifndef V5_COORDINATE_PANEL_H
#define V5_COORDINATE_PANEL_H

#include "v5_ui_status_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_COORDINATE_TEXT_CAP 24u
#define V5_COORDINATE_AXIS_COUNT V5_STATUS_AXIS_COUNT
#define V5_COORDINATE_MODAL_TEXT_CAP 128u

typedef struct V5CoordinatePanelLine {
    char axis;
    int mcs_valid;
    int cmd_valid;
    int following_error_valid;
    char mcs_text[V5_COORDINATE_TEXT_CAP];
    char cmd_text[V5_COORDINATE_TEXT_CAP];
    char following_error_text[V5_COORDINATE_TEXT_CAP];
} V5CoordinatePanelLine;

typedef struct V5CoordinatePanelSnapshot {
    V5CoordinatePanelLine lines[V5_COORDINATE_AXIS_COUNT];
    int tool_tip_contour_error_valid;
    char tool_tip_contour_error_text[V5_COORDINATE_TEXT_CAP];
    char modal_text[V5_COORDINATE_MODAL_TEXT_CAP];
    char spindle_speed_text[V5_COORDINATE_TEXT_CAP];
    char linear_velocity_text[V5_COORDINATE_TEXT_CAP];
    char feed_override_text[V5_COORDINATE_TEXT_CAP];
    char spindle_override_text[V5_COORDINATE_TEXT_CAP];
} V5CoordinatePanelSnapshot;

void v5_coordinate_panel_from_status(const V5UiStatusView *status, V5CoordinatePanelSnapshot *panel);

#ifdef __cplusplus
}
#endif

#endif
