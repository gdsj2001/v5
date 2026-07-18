#include "v5_ui_model.h"

#include "v5_status_shm_mmap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_STATUS_STALE_MARK_NS 200000000ULL
#define V5_STATUS_STALE_HOLD_NS 300000000ULL
#define V5_STATUS_STALE_DYNAMIC_HIDE_NS 500000000ULL
#define V5_STATUS_READER_RETRY_NS 100000000ULL
#define V5_STATUS_READER_FAILURE_CHECK_COUNT 3U
#define V5_STATUS_READER_STAGNANT_CHECK_COUNT 6U

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

static int apply_current_cpu_usage(
    V5UiModel *model,
    const V5UiStatusView *current)
{
    if (!model) {
        return 0;
    }
    model->status_view.valid_mask &= ~V5_STATUS_VALID_CPU_USAGE;
    model->status_view.cpu0_percent = 0.0;
    model->status_view.cpu1_percent = 0.0;
    model->status_view.cpu_sample_generation = 0ULL;
    model->status_view.cpu_sample_monotonic_ns = 0ULL;
    if (!current || (current->valid_mask & V5_STATUS_VALID_CPU_USAGE) == 0U) {
        return 0;
    }
    model->status_view.cpu0_percent = current->cpu0_percent;
    model->status_view.cpu1_percent = current->cpu1_percent;
    model->status_view.cpu_sample_generation = current->cpu_sample_generation;
    model->status_view.cpu_sample_monotonic_ns = current->cpu_sample_monotonic_ns;
    model->status_view.valid_mask |= V5_STATUS_VALID_CPU_USAGE;
    return 1;
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
    v5_status_shm_mmap_reader_init(&model->status_reader);
    model->status_reader_retry_after_ns = 0ULL;
    model->status_reader_last_epoch = 0ULL;
    model->status_reader_failure_count = 0U;
    model->status_reader_stagnant_count = 0U;
}

void v5_ui_model_close_status_reader(V5UiModel *model)
{
    if (!model) {
        return;
    }
    v5_status_shm_mmap_reader_close(&model->status_reader);
    model->status_reader_retry_after_ns = 0ULL;
    model->status_reader_last_epoch = 0ULL;
    model->status_reader_failure_count = 0U;
    model->status_reader_stagnant_count = 0U;
}

static int status_reader_backing_replaced(V5UiModel *model)
{
    if (!model || v5_status_shm_mmap_reader_backing_matches(&model->status_reader)) {
        return 0;
    }
    v5_ui_model_close_status_reader(model);
    return 1;
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
        int retained_native = apply_last_good_status(model, now_ns);
        int applied_cpu = 0;
        if (age_ns <= V5_STATUS_STALE_DYNAMIC_HIDE_NS) {
            applied_cpu = apply_current_cpu_usage(model, &view);
        } else {
            (void)apply_current_cpu_usage(model, 0);
        }
        return retained_native || applied_cpu;
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
    unsigned long long now_ns;

    if (!model) {
        return 0;
    }
    now_ns = monotonic_ns();
    if (model->status_reader.fd < 0 &&
        model->status_reader_retry_after_ns != 0ULL &&
        now_ns < model->status_reader_retry_after_ns) {
        return apply_last_good_status(model, now_ns);
    }
    if (!v5_status_shm_mmap_reader_open(&model->status_reader, path)) {
        model->status_reader_retry_after_ns = now_ns + V5_STATUS_READER_RETRY_NS;
        return apply_last_good_status(model, now_ns);
    }
    model->status_reader_retry_after_ns = 0ULL;
    if (!v5_status_shm_mmap_reader_read(&model->status_reader, &frame)) {
        model->status_reader_failure_count += 1U;
        if (model->status_reader_failure_count >= V5_STATUS_READER_FAILURE_CHECK_COUNT) {
            model->status_reader_failure_count = 0U;
            (void)status_reader_backing_replaced(model);
        }
        return apply_last_good_status(model, now_ns);
    }
    model->status_reader_failure_count = 0U;
    if (frame.status_epoch != 0ULL && frame.status_epoch == model->status_reader_last_epoch) {
        model->status_reader_stagnant_count += 1U;
        if (model->status_reader_stagnant_count >= V5_STATUS_READER_STAGNANT_CHECK_COUNT) {
            model->status_reader_stagnant_count = 0U;
            if (status_reader_backing_replaced(model)) {
                return apply_last_good_status(model, now_ns);
            }
        }
    } else {
        model->status_reader_last_epoch = frame.status_epoch;
        model->status_reader_stagnant_count = 0U;
    }
    return v5_ui_model_apply_status_frame(model, &frame);
}
