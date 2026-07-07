#include "v5_state_publisher_service.h"

#include "v5_native_sample.h"
#include "v5_native_rtcp_status.h"

#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void v5_status_shm_writer_seed_display_frame(V5StatusShmFrame *frame);
void v5_status_shm_writer_apply_sample(V5StatusShmFrame *frame, const V5NativeDisplaySample *sample);

static volatile sig_atomic_t g_v5_state_publisher_stop_requested = 0;

void v5_state_publisher_request_stop(void)
{
    g_v5_state_publisher_stop_requested = 1;
}

void v5_state_publisher_reset_stop(void)
{
    g_v5_state_publisher_stop_requested = 0;
}

static uint64_t v5_state_publisher_epoch_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return ((uint64_t)now.tv_sec * 1000000000ull) + (uint64_t)now.tv_nsec;
}


static int v5_state_publisher_publish_rtcp_status_block(void)
{
    V5NativeReadback readback;

    mkdir("/run/8ax_v5_product_ui", 0755);
    v5_native_readback_init(&readback);
    if (v5_native_rtcp_status_read(0, V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS, &readback) &&
        v5_native_readback_rtcp_known(&readback)) {
        return 1;
    }
    return v5_native_rtcp_status_write(0, 0, 0);
}

static void v5_state_publisher_sleep_ms(unsigned int interval_ms)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(interval_ms / 1000u);
    delay.tv_nsec = (long)(interval_ms % 1000u) * 1000000L;
    while (!g_v5_state_publisher_stop_requested && nanosleep(&delay, &delay) != 0) {
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

    if (report) {
        report->rtcp_status_published = (unsigned int)v5_state_publisher_publish_rtcp_status_block();
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
    V5StatePublisherReport local_report = {0};
    V5StatePublisherReport *out = report ? report : &local_report;

    v5_state_publisher_reset_stop();

    do {
        if (!v5_state_publisher_publish_once(path, out)) {
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
        v5_state_publisher_sleep_ms(period);
    } while (!g_v5_state_publisher_stop_requested && (!max_frames || count < max_frames));

    return 1;
}
