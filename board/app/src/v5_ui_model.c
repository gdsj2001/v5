#include "v5_ui_model.h"

#include "v5_status_shm_mmap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_STATUS_STALE_MARK_NS 200000000ULL
#define V5_STATUS_STALE_HOLD_NS 300000000ULL
#define V5_STATUS_STALE_DYNAMIC_HIDE_NS 500000000ULL

static unsigned long long monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000000000ULL) + (unsigned long long)ts.tv_nsec;
}

static int frame_time_anomaly(const V5StatusShmFrame *frame, unsigned long long now_ns)
{
    return !frame || frame->status_epoch == 0ULL || now_ns == 0ULL || frame->status_epoch > now_ns;
}

static unsigned long long frame_age_ns(const V5StatusShmFrame *frame, unsigned long long now_ns)
{
    if (frame_time_anomaly(frame, now_ns)) {
        return V5_STATUS_STALE_DYNAMIC_HIDE_NS + 1ULL;
    }
    return now_ns - (unsigned long long)frame->status_epoch;
}

static void apply_unavailable_status(V5UiModel *model)
{
    if (!model) {
        return;
    }
    v5_ui_status_view_init(&model->status_view);
    model->status_view.frame_flags = V5_STATUS_FRAME_FLAG_DEGRADED |
                                     V5_STATUS_FRAME_FLAG_STALE |
                                     V5_STATUS_FRAME_FLAG_UNAVAILABLE;
    model->status_view.valid_mask = 0U;
}

static int apply_last_good_status(V5UiModel *model, unsigned long long now_ns)
{
    unsigned long long age_ns;
    if (!model || !model->has_last_good_status || model->last_good_monotonic_ns == 0ULL || now_ns == 0ULL) {
        apply_unavailable_status(model);
        return 0;
    }
    age_ns = now_ns - model->last_good_monotonic_ns;
    if (age_ns <= V5_STATUS_STALE_HOLD_NS) {
        model->status_view = model->last_good_status_view;
        model->status_view.frame_flags |= V5_STATUS_FRAME_FLAG_STALE;
        return 1;
    }
    if (age_ns <= V5_STATUS_STALE_DYNAMIC_HIDE_NS) {
        model->status_view = model->last_good_status_view;
        model->status_view.frame_flags |= V5_STATUS_FRAME_FLAG_STALE | V5_STATUS_FRAME_FLAG_DEGRADED;
        v5_ui_status_view_clear_dynamic(&model->status_view);
        return 1;
    }
    apply_unavailable_status(model);
    return 0;
}

void v5_ui_model_init(V5UiModel *model)
{
    if (!model) {
        return;
    }
    model->boot_generation = 1U;
    model->lvgl_initialized = 0;
    model->boot_closure_abi = 0U;
    model->command_count = 0U;
    model->drive_profile_count = 0U;
    model->drive_profile_map_count = 0U;
    model->parameter_owner_count = 0U;
    model->resource_count = 0U;
    v5_ui_status_view_init(&model->status_view);
    v5_ui_status_view_init(&model->last_good_status_view);
    model->has_last_good_status = 0;
    model->last_good_monotonic_ns = 0ULL;
}

int v5_ui_model_apply_status_frame(V5UiModel *model, const V5StatusShmFrame *frame)
{
    V5UiStatusView view;
    unsigned long long now_ns;
    unsigned long long age_ns;
    if (!model || !frame) {
        return 0;
    }

    now_ns = monotonic_ns();
    if (!v5_ui_status_view_from_frame(&view, frame)) {
        return apply_last_good_status(model, now_ns);
    }

    if (frame_time_anomaly(frame, now_ns)) {
        return apply_last_good_status(model, now_ns);
    }
    age_ns = frame_age_ns(frame, now_ns);
    if (!v5_ui_status_view_has_dynamic(&view)) {
        return apply_last_good_status(model, now_ns);
    }
    if (age_ns > V5_STATUS_STALE_DYNAMIC_HIDE_NS) {
        return apply_last_good_status(model, now_ns);
    }
    if (age_ns > V5_STATUS_STALE_HOLD_NS) {
        view.frame_flags |= V5_STATUS_FRAME_FLAG_STALE | V5_STATUS_FRAME_FLAG_DEGRADED;
        v5_ui_status_view_clear_dynamic(&view);
    } else if (age_ns > V5_STATUS_STALE_MARK_NS) {
        view.frame_flags |= V5_STATUS_FRAME_FLAG_STALE;
    }

    model->status_view = view;
    if (v5_ui_status_view_has_dynamic(&view)) {
        model->last_good_status_view = view;
        model->has_last_good_status = 1;
        model->last_good_monotonic_ns = now_ns;
    }
    return 1;
}

int v5_ui_model_refresh_status_from_shm(V5UiModel *model, const char *path)
{
    V5StatusShmFrame frame;

    if (!model) {
        return 0;
    }
    if (!v5_status_shm_read_from_path(path, &frame)) {
        return apply_last_good_status(model, monotonic_ns());
    }
    return v5_ui_model_apply_status_frame(model, &frame);
}
