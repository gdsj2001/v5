#include "v5_state_publisher_service.h"

#include "v5_cpu_usage_snapshot.h"
#include "v5_native_sample.h"
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

void v5_status_shm_writer_seed_display_frame(V5StatusShmFrame *frame);
void v5_status_shm_writer_apply_sample(V5StatusShmFrame *frame, const V5NativeDisplaySample *sample);

static volatile sig_atomic_t g_v5_state_publisher_stop_requested = 0;
static V5CpuUsageSnapshotSampler g_v5_cpu_usage_sampler;
static int g_v5_cpu_usage_sampler_initialized;

#define V5_CPU_USAGE_MAX_AGE_NS 5000000000ull

void v5_state_publisher_request_stop(void)
{
    g_v5_state_publisher_stop_requested = 1;
}

void v5_state_publisher_reset_stop(void)
{
    g_v5_state_publisher_stop_requested = 0;
    v5_cpu_usage_snapshot_sampler_init(&g_v5_cpu_usage_sampler);
    g_v5_cpu_usage_sampler_initialized = 1;
}

static uint64_t v5_state_publisher_epoch_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return ((uint64_t)now.tv_sec * 1000000000ull) + (uint64_t)now.tv_nsec;
}

static void v5_state_publisher_apply_cpu_usage(
    V5StatusShmFrame *frame,
    uint64_t now_ns)
{
    V5CpuUsageSnapshot snapshot;

    if (!frame || !now_ns) {
        return;
    }
    if (!g_v5_cpu_usage_sampler_initialized) {
        v5_cpu_usage_snapshot_sampler_init(&g_v5_cpu_usage_sampler);
        g_v5_cpu_usage_sampler_initialized = 1;
    }
    if (!v5_cpu_usage_snapshot_read_at(
            &g_v5_cpu_usage_sampler,
            &snapshot,
            now_ns,
            V5_CPU_USAGE_SNAPSHOT_DEFAULT_SYSFS_ROOT,
            V5_CPU_USAGE_SNAPSHOT_DEFAULT_PROC_PATH) ||
        snapshot.valid_mask != 3U || snapshot.generation == 0ULL ||
        snapshot.monotonic_ns == 0ULL || snapshot.monotonic_ns > now_ns ||
        now_ns - snapshot.monotonic_ns > V5_CPU_USAGE_MAX_AGE_NS ||
        !isfinite(snapshot.busy_percent[0]) || snapshot.busy_percent[0] < 0.0 || snapshot.busy_percent[0] > 100.0 ||
        !isfinite(snapshot.busy_percent[1]) || snapshot.busy_percent[1] < 0.0 || snapshot.busy_percent[1] > 100.0) {
        frame->typed_valid_mask &= ~V5_STATUS_VALID_CPU_USAGE;
        return;
    }
    frame->cpu0_percent = snapshot.busy_percent[0];
    frame->cpu1_percent = snapshot.busy_percent[1];
    frame->cpu_sample_generation = snapshot.generation;
    frame->cpu_sample_monotonic_ns = snapshot.monotonic_ns;
    frame->typed_valid_mask |= V5_STATUS_VALID_CPU_USAGE;
}


static void v5_state_publisher_wait_for_next_start(
    uint64_t start_ns,
    unsigned int interval_ms)
{
    while (!g_v5_state_publisher_stop_requested) {
        uint64_t now_ns = v5_state_publisher_epoch_ns();
        uint64_t wait_ns = v5_state_publisher_cadence_wait_ns(
            start_ns, now_ns, interval_ms);
        struct timespec delay;

        if (wait_ns == 0ull) {
            break;
        }
        delay.tv_sec = (time_t)(wait_ns / 1000000000ull);
        delay.tv_nsec = (long)(wait_ns % 1000000000ull);
        if (nanosleep(&delay, 0) == 0) {
            break;
        }
        if (errno != EINTR) {
            break;
        }
    }
}

int v5_state_publisher_build_frame(V5StatusShmFrame *frame, V5StatePublisherReport *report)
{
    V5NativeDisplaySample sample;
    int available;

    if (!frame) {
        return 0;
    }

    available = v5_native_display_sample_read(&sample);
    if (available) {
        v5_status_shm_writer_apply_sample(frame, &sample);
    } else {
        v5_status_shm_writer_seed_display_frame(frame);
    }
    frame->status_epoch = v5_state_publisher_epoch_ns();
    v5_state_publisher_apply_cpu_usage(frame, frame->status_epoch);

    if (report) {
        report->sample_available = available;
        report->valid_mask = frame->typed_valid_mask;
        report->frame_flags = frame->flags;
    }

    return 1;
}

int v5_state_publisher_publish_once(const char *path, V5StatePublisherReport *report)
{
    V5StatusShmFrame frame;
    V5StatePublisherReport local_report;
    V5StatePublisherReport *out = report ? report : &local_report;

    if (!v5_state_publisher_build_frame(&frame, out)) {
        return 0;
    }
    if (!v5_status_shm_publish_to_path(path, &frame, 0)) {
        return 0;
    }
    out->publish_count += 1u;
    out->interval_ms = V5_STATE_PUBLISHER_INTERVAL_MS;
    out->path = (path && path[0]) ? path : V5_STATUS_SHM_PATH;
    return 1;
}

int v5_state_publisher_run_loop(const char *path, unsigned int interval_ms, unsigned int max_frames, V5StatePublisherReport *report)
{
    unsigned int count = 0;
    unsigned int period = interval_ms ? interval_ms : V5_STATE_PUBLISHER_INTERVAL_MS;
    V5StatusShmMmapWriter writer;
    V5StatePublisherReport local_report = {0};
    V5StatePublisherReport *out = report ? report : &local_report;

    v5_state_publisher_reset_stop();
    v5_status_shm_mmap_writer_init(&writer);
    if (!v5_status_shm_mmap_writer_open(&writer, path)) {
        return 0;
    }

    do {
        V5StatusShmFrame frame;
        uint64_t sample_start_ns = v5_state_publisher_epoch_ns();

        if (!v5_state_publisher_build_frame(&frame, out) ||
            !v5_status_shm_mmap_writer_publish(&writer, &frame, 0)) {
            v5_status_shm_mmap_writer_close(&writer);
            return 0;
        }
        count += 1u;
        out->publish_count = count;
        out->interval_ms = period;
        if (max_frames && count >= max_frames) {
            break;
        }
        if (g_v5_state_publisher_stop_requested) {
            break;
        }
        v5_state_publisher_wait_for_next_start(sample_start_ns, period);
    } while (!g_v5_state_publisher_stop_requested && (!max_frames || count < max_frames));

    out->path = (path && path[0]) ? path : V5_STATUS_SHM_PATH;
    v5_status_shm_mmap_writer_close(&writer);
    return 1;
}
