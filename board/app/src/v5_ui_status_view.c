#include "v5_ui_status_view.h"

#include <math.h>
#include <string.h>

#define V5_CPU_USAGE_MAX_AGE_NS 5000000000ULL

static int finite_axis(const double axis[V5_STATUS_AXIS_COUNT])
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

static int finite_value(double value)
{
    return isfinite(value);
}

double v5_ui_status_view_rotary_phase_deg(double value)
{
    double phase;
    if (!isfinite(value)) {
        return value;
    }
    phase = fmod(value, 360.0);
    if (phase < 0.0) {
        phase += 360.0;
    }
    if (phase >= 360.0 || phase == 0.0) {
        return 0.0;
    }
    return phase;
}

static void normalize_rotary_display_slots(double axis[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    for (i = 3U; i < V5_STATUS_AXIS_COUNT; ++i) {
        axis[i] = v5_ui_status_view_rotary_phase_deg(axis[i]);
    }
}

void v5_ui_status_view_init(V5UiStatusView *view)
{
    if (!view) {
        return;
    }

    memset(view, 0, sizeof(*view));
}

void v5_ui_status_view_clear_dynamic(V5UiStatusView *view)
{
    if (!view) {
        return;
    }
    view->valid_mask &= ~(V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS | V5_STATUS_VALID_TRAJECTORY);
    memset(view->raw_mcs, 0, sizeof(view->raw_mcs));
    memset(view->raw_cmd_mcs, 0, sizeof(view->raw_cmd_mcs));
    memset(view->mcs, 0, sizeof(view->mcs));
    memset(view->cmd_mcs, 0, sizeof(view->cmd_mcs));
    memset(view->trajectory, 0, sizeof(view->trajectory));
    view->trajectory_count = 0U;
}

int v5_ui_status_view_has_dynamic(const V5UiStatusView *view)
{
    return view && (view->valid_mask & (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS | V5_STATUS_VALID_TRAJECTORY)) != 0U;
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
    if ((frame->typed_valid_mask & V5_STATUS_VALID_MCS) && finite_axis(frame->mcs)) {
        memcpy(view->raw_mcs, frame->mcs, sizeof(view->raw_mcs));
        memcpy(view->mcs, frame->mcs, sizeof(view->mcs));
        normalize_rotary_display_slots(view->mcs);
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_MCS;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_CMD_MCS) && finite_axis(frame->cmd_mcs)) {
        memcpy(view->raw_cmd_mcs, frame->cmd_mcs, sizeof(view->raw_cmd_mcs));
        memcpy(view->cmd_mcs, frame->cmd_mcs, sizeof(view->cmd_mcs));
        normalize_rotary_display_slots(view->cmd_mcs);
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_CMD_MCS;
    }
    if (frame->typed_valid_mask & V5_STATUS_VALID_TRAJECTORY) {
        uint32_t count = frame->trajectory_count;
        uint32_t i;
        if (count > V5_STATUS_TRAJECTORY_POINT_COUNT) {
            count = V5_STATUS_TRAJECTORY_POINT_COUNT;
        }
        for (i = 0U; i < count; ++i) {
            if (!finite_axis(frame->trajectory[i].axis)) {
                break;
            }
            view->trajectory[i] = frame->trajectory[i];
        }
        view->trajectory_count = i;
        if (view->trajectory_count == 0U) {
            view->valid_mask &= ~V5_STATUS_VALID_TRAJECTORY;
        }
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) && finite_value(frame->spindle_speed_rpm)) {
        view->spindle_speed_rpm = frame->spindle_speed_rpm;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_SPINDLE_SPEED;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_LINEAR_VELOCITY) && finite_value(frame->linear_velocity_mm_per_min)) {
        view->linear_velocity_mm_per_min = frame->linear_velocity_mm_per_min;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_LINEAR_VELOCITY;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_FEED_OVERRIDE) && finite_value(frame->feedrate_override)) {
        view->feedrate_override = frame->feedrate_override;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_FEED_OVERRIDE;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) && finite_value(frame->spindle_override)) {
        view->spindle_override = frame->spindle_override;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_SPINDLE_OVERRIDE;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_CPU_USAGE) &&
        finite_value(frame->cpu0_percent) && frame->cpu0_percent >= 0.0 && frame->cpu0_percent <= 100.0 &&
        finite_value(frame->cpu1_percent) && frame->cpu1_percent >= 0.0 && frame->cpu1_percent <= 100.0 &&
        frame->cpu_sample_generation != 0ULL && frame->cpu_sample_monotonic_ns != 0ULL &&
        frame->status_epoch >= frame->cpu_sample_monotonic_ns &&
        frame->status_epoch - frame->cpu_sample_monotonic_ns <= V5_CPU_USAGE_MAX_AGE_NS) {
        view->cpu0_percent = frame->cpu0_percent;
        view->cpu1_percent = frame->cpu1_percent;
        view->cpu_sample_generation = frame->cpu_sample_generation;
        view->cpu_sample_monotonic_ns = frame->cpu_sample_monotonic_ns;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_CPU_USAGE;
    }

    return 1;
}
