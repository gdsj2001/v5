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
    memset(view->mcs, 0, sizeof(view->mcs));
    memset(view->cmd_mcs, 0, sizeof(view->cmd_mcs));
    memset(view->unit_per_count, 0, sizeof(view->unit_per_count));
    memset(view->following_error, 0, sizeof(view->following_error));
    memset(view->display_digits, 0, sizeof(view->display_digits));
    memset(view->trajectory, 0, sizeof(view->trajectory));
    view->trajectory_count = 0U;
    view->valid_mask &= ~V5_STATUS_VALID_DISPLAY_SCENE;
    view->display_scene = NULL;
}

int v5_ui_status_view_has_dynamic(const V5UiStatusView *view)
{
    return view && (view->valid_mask & (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_TRAJECTORY | V5_STATUS_VALID_DISPLAY_SCENE)) != 0U;
}

static int display_scene_valid(const V5StatusDisplayScene *scene)
{
    uint32_t i;
    if (!scene || (scene->flags & V5_STATUS_SCENE_FLAG_VALID) == 0U ||
        scene->native_generation == 0ULL || scene->view_generation == 0ULL ||
        scene->fit_generation == 0ULL || scene->build_count == 0ULL ||
        scene->project_count == 0ULL ||
        scene->point_count > V5_STATUS_SCENE_POINT_COUNT ||
        scene->segment_count > V5_STATUS_SCENE_SEGMENT_COUNT ||
        scene->marker_count > V5_STATUS_SCENE_MARKER_COUNT) return 0;
    for (i = 0U; i < scene->point_count; ++i) {
        if (!isfinite(scene->points[i].x) || !isfinite(scene->points[i].y)) return 0;
    }
    for (i = 0U; i < scene->segment_count; ++i) {
        if (!isfinite(scene->segments[i].start.x) || !isfinite(scene->segments[i].start.y) ||
            !isfinite(scene->segments[i].end.x) || !isfinite(scene->segments[i].end.y)) return 0;
    }
    for (i = 0U; i < scene->marker_count; ++i) {
        if (!isfinite(scene->markers[i].point.x) || !isfinite(scene->markers[i].point.y)) return 0;
    }
    if ((scene->flags &
         V5_STATUS_SCENE_FLAG_TOOL_TIP_CONTOUR_ERROR) != 0U &&
        (!isfinite(scene->tool_tip_contour_error) ||
         scene->tool_tip_contour_error < 0.0)) return 0;
    return 1;
}

int v5_ui_status_view_from_frame(V5UiStatusView *view, const V5StatusShmFrame *frame)
{
    unsigned int axis;
    const uint32_t position_display_mask =
        V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
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
    view->position_writer_identity = frame->position_writer_identity;
    view->source_acquired_mono_ns = frame->source_acquired_mono_ns;
    view->source_generation = frame->source_generation;
    view->scene_generation = frame->scene_generation;
    if (view->position_writer_identity == 0U ||
        view->source_acquired_mono_ns == 0ULL ||
        view->source_generation == 0ULL) {
        return 0;
    }
    if ((frame->typed_valid_mask & position_display_mask) != 0U) {
        for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
            if (!isfinite(frame->unit_per_count[axis]) ||
                frame->unit_per_count[axis] <= 0.0 ||
                !isfinite(frame->following_error[axis]) ||
                frame->display_digits[axis] != 3U) {
                return 0;
            }
        }
        memcpy(view->unit_per_count, frame->unit_per_count, sizeof(view->unit_per_count));
        memcpy(view->following_error, frame->following_error, sizeof(view->following_error));
        memcpy(view->display_digits, frame->display_digits, sizeof(view->display_digits));
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_MCS) && finite_axis(frame->mcs)) {
        memcpy(view->mcs, frame->mcs, sizeof(view->mcs));
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_MCS;
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_CMD_MCS) && finite_axis(frame->cmd_mcs)) {
        memcpy(view->cmd_mcs, frame->cmd_mcs, sizeof(view->cmd_mcs));
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
    if (frame->scene_generation != 0ULL &&
        (frame->typed_valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) &&
        display_scene_valid(&frame->display_scene)) {
        view->display_scene = &frame->display_scene;
    } else {
        view->valid_mask &= ~V5_STATUS_VALID_DISPLAY_SCENE;
        view->scene_generation = 0ULL;
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
