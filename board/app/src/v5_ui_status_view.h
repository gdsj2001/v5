#ifndef V5_UI_STATUS_VIEW_H
#define V5_UI_STATUS_VIEW_H

#include "v5_status_shm.h"

#include <stdint.h>

typedef struct V5UiStatusView {
    uint32_t valid_mask;
    uint32_t frame_flags;
    uint64_t status_epoch;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    V5StatusPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    uint32_t trajectory_count;
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
} V5UiStatusView;

void v5_ui_status_view_init(V5UiStatusView *view);
int v5_ui_status_view_from_frame(V5UiStatusView *view, const V5StatusShmFrame *frame);
void v5_ui_status_view_clear_dynamic(V5UiStatusView *view);
int v5_ui_status_view_has_dynamic(const V5UiStatusView *view);

#endif
