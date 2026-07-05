#include "v5_ui_status_view.h"

#include <string.h>

void v5_ui_status_view_init(V5UiStatusView *view)
{
    if (!view) {
        return;
    }

    memset(view, 0, sizeof(*view));
}

int v5_ui_status_view_from_frame(V5UiStatusView *view, const V5StatusShmFrame *frame)
{
    if (!view || !frame) {
        return 0;
    }
    if (frame->magic != V5_STATUS_SHM_MAGIC || frame->version != V5_STATUS_SHM_VERSION) {
        return 0;
    }

    v5_ui_status_view_init(view);
    view->valid_mask = frame->typed_valid_mask;
    view->frame_flags = frame->flags;
    view->status_epoch = frame->status_epoch;
    if (frame->typed_valid_mask & V5_STATUS_VALID_MCS) {
        memcpy(view->mcs, frame->mcs, sizeof(view->mcs));
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_CMD_MCS) {
        memcpy(view->cmd_mcs, frame->cmd_mcs, sizeof(view->cmd_mcs));
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_TRAJECTORY) {
        uint32_t count = frame->trajectory_count;
        if (count > V5_STATUS_TRAJECTORY_POINT_COUNT) {
            count = V5_STATUS_TRAJECTORY_POINT_COUNT;
        }
        memcpy(view->trajectory, frame->trajectory, sizeof(view->trajectory[0]) * count);
        view->trajectory_count = count;
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_MODAL) {
        memcpy(view->runtime_modal_text, frame->runtime_modal_text, sizeof(view->runtime_modal_text));
        view->runtime_modal_text[V5_STATUS_MODAL_TEXT_CAP - 1U] = '\0';
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) {
        view->spindle_speed_rpm = frame->spindle_speed_rpm;
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_LINEAR_VELOCITY) {
        view->linear_velocity_mm_per_min = frame->linear_velocity_mm_per_min;
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_FEED_OVERRIDE) {
        view->feedrate_override = frame->feedrate_override;
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) {
        view->spindle_override = frame->spindle_override;
    }

    return 1;
}
