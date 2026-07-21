#include "v5_state_publisher_service.h"

#include "v5_cpu_usage_snapshot.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_sample.h"
#include "v5_native_wcs_status.h"
#include "v5_program_scene_ipc.h"
#include "v5_program_scene_producer.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

typedef int (*V5ResidentBlockDecode)(
    const void *,
    size_t,
    unsigned int,
    V5NativeReadback *);

typedef struct V5ResidentBlockReader {
    const char *path;
    size_t size;
    unsigned int max_age_ms;
    V5ResidentBlockDecode decode;
    int fd;
    void *memory;
    dev_t device;
    ino_t inode;
    unsigned int identity_probe_count;
} V5ResidentBlockReader;

void v5_status_shm_writer_seed_display_frame(V5StatusShmFrame *frame);
void v5_status_shm_writer_apply_sample(V5StatusShmFrame *frame, const V5NativeDisplaySample *sample);

typedef struct V5StatePublisherContext {
    V5NativeDisplaySampleReader reader;
    V5ProgramSceneRequestServer request_server;
    V5ProgramSceneProducer scene_producer;
    V5ProgramSceneRequest latest_request;
    V5ResidentBlockReader rtcp_reader;
    V5ResidentBlockReader wcs_reader;
    V5ResidentBlockReader g53_reader;
    V5ResidentBlockReader modal_reader;
} V5StatePublisherContext;

static volatile sig_atomic_t g_v5_state_publisher_stop_requested = 0;
static V5CpuUsageSnapshotSampler g_v5_cpu_usage_sampler;
static int g_v5_cpu_usage_sampler_initialized;

#define V5_CPU_USAGE_MAX_AGE_NS 5000000000ull

void v5_state_publisher_request_stop(void) { g_v5_state_publisher_stop_requested = 1; }

void v5_state_publisher_reset_stop(void)
{
    g_v5_state_publisher_stop_requested = 0;
    v5_cpu_usage_snapshot_sampler_init(&g_v5_cpu_usage_sampler);
    g_v5_cpu_usage_sampler_initialized = 1;
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0ULL;
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void apply_cpu_usage(V5StatusShmFrame *frame, uint64_t now_ns)
{
    V5CpuUsageSnapshot snapshot;
    if (!frame || !now_ns) return;
    if (!g_v5_cpu_usage_sampler_initialized) {
        v5_cpu_usage_snapshot_sampler_init(&g_v5_cpu_usage_sampler);
        g_v5_cpu_usage_sampler_initialized = 1;
    }
    if (!v5_cpu_usage_snapshot_read_at(&g_v5_cpu_usage_sampler, &snapshot, now_ns,
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

static void wait_for_next_start(uint64_t start_ns, unsigned int interval_ms)
{
    while (!g_v5_state_publisher_stop_requested) {
        uint64_t wait_ns = v5_state_publisher_cadence_wait_ns(start_ns, monotonic_ns(), interval_ms);
        struct timespec delay;
        if (!wait_ns) break;
        delay.tv_sec = (time_t)(wait_ns / 1000000000ULL);
        delay.tv_nsec = (long)(wait_ns % 1000000000ULL);
        if (nanosleep(&delay, 0) == 0 || errno != EINTR) break;
    }
}

void v5_state_publisher_merge_scene_readbacks(
    V5NativeReadback *merged,
    const V5NativeReadback *rtcp,
    const V5NativeReadback *wcs,
    const V5NativeReadback *g53,
    const V5NativeReadback *modal_tool)
{
    if (!merged) return;
    v5_native_readback_init(merged);
    if (v5_native_readback_rtcp_known(rtcp)) {
        v5_native_readback_set_rtcp_actual(merged, rtcp->rtcp_enabled);
    }
    if (v5_native_readback_wcs_table_known(wcs)) {
        v5_native_readback_set_wcs_table(
            merged, wcs->wcs_index, &wcs->wcs_offsets[0][0],
            V5_NATIVE_READBACK_WCS_COUNT,
            V5_NATIVE_READBACK_WCS_AXIS_COUNT,
            wcs->wcs_offsets_epoch);
    }
    if (v5_native_readback_g53_geometry_known(g53)) {
        v5_native_readback_set_g53_geometry(
            merged, &g53->g53_centers[0][0],
            V5_NATIVE_READBACK_G53_CENTER_COUNT,
            V5_NATIVE_READBACK_G53_AXIS_COUNT,
            g53->g53_geometry_epoch);
    }
    if (v5_native_readback_motion_model_known(g53)) {
        v5_native_readback_set_motion_model(merged, g53->motion_model);
    }
    if (v5_native_readback_modal_known(modal_tool)) {
        v5_native_readback_set_modal_actual(merged, modal_tool->modal_text);
    }
    if (v5_native_readback_tool_known(modal_tool)) {
        v5_native_readback_set_tool_actual(
            merged, modal_tool->tool_number,
            v5_native_readback_tool_length_known(modal_tool),
            modal_tool->tool_length_mm);
    }
}

int v5_state_publisher_apply_scene(
    V5StatusShmFrame *frame,
    V5ProgramSceneProducer *producer,
    const V5NativeDisplaySample *sample,
    const V5NativeReadback *readback)
{
    uint64_t scene_generation = 0ULL;
    if (!frame || !producer || !sample || !readback) return 0;
    if (!v5_program_scene_producer_build(
            producer, sample, readback,
            &frame->display_scene, &scene_generation)) {
        memset(&frame->display_scene, 0, sizeof(frame->display_scene));
        frame->typed_valid_mask &= ~V5_STATUS_VALID_DISPLAY_SCENE;
        frame->scene_generation = 0ULL;
        return 0;
    }
    frame->typed_valid_mask |= V5_STATUS_VALID_DISPLAY_SCENE;
    frame->scene_generation = scene_generation;
    return 1;
}

static void resident_block_reader_init(
    V5ResidentBlockReader *reader,
    const char *path,
    size_t size,
    unsigned int max_age_ms,
    V5ResidentBlockDecode decode)
{
    if (!reader) return;
    memset(reader, 0, sizeof(*reader));
    reader->path = path;
    reader->size = size;
    reader->max_age_ms = max_age_ms;
    reader->decode = decode;
    reader->fd = -1;
    reader->memory = MAP_FAILED;
}

static void resident_block_reader_close(V5ResidentBlockReader *reader)
{
    if (!reader) return;
    if (reader->memory != MAP_FAILED) {
        munmap(reader->memory, reader->size);
        reader->memory = MAP_FAILED;
    }
    if (reader->fd >= 0) {
        close(reader->fd);
        reader->fd = -1;
    }
    reader->device = 0;
    reader->inode = 0;
    reader->identity_probe_count = 0U;
}

static int resident_block_reader_open(V5ResidentBlockReader *reader)
{
    struct stat status;
    void *memory;
    int fd;
    if (!reader || !reader->path || !reader->decode || !reader->size) {
        return 0;
    }
    if (reader->memory != MAP_FAILED) return 1;
    fd = open(reader->path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &status) != 0 ||
        status.st_size != (off_t)reader->size) {
        if (fd >= 0) close(fd);
        return 0;
    }
    memory = mmap(0, reader->size, PROT_READ, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        close(fd);
        return 0;
    }
    reader->fd = fd;
    reader->memory = memory;
    reader->device = status.st_dev;
    reader->inode = status.st_ino;
    return 1;
}

static int resident_block_reader_backing_matches(
    const V5ResidentBlockReader *reader)
{
    struct stat descriptor;
    struct stat path;
    if (!reader || reader->fd < 0 ||
        fstat(reader->fd, &descriptor) != 0 ||
        stat(reader->path, &path) != 0) return 0;
    return descriptor.st_size == (off_t)reader->size &&
        path.st_size == (off_t)reader->size &&
        descriptor.st_dev == reader->device &&
        descriptor.st_ino == reader->inode &&
        path.st_dev == reader->device &&
        path.st_ino == reader->inode;
}

static int resident_block_reader_read(
    V5ResidentBlockReader *reader,
    V5NativeReadback *readback)
{
    if (!reader || !readback) return 0;
    if (reader->memory != MAP_FAILED &&
        ++reader->identity_probe_count >= 6U) {
        reader->identity_probe_count = 0U;
        if (!resident_block_reader_backing_matches(reader)) {
            resident_block_reader_close(reader);
        }
    }
    if (!resident_block_reader_open(reader)) return 0;
    if (reader->decode(
            reader->memory, reader->size,
            reader->max_age_ms, readback)) return 1;
    resident_block_reader_close(reader);
    return resident_block_reader_open(reader) &&
        reader->decode(
            reader->memory, reader->size,
            reader->max_age_ms, readback);
}

static void read_native_scene_inputs(
    V5StatePublisherContext *context,
    V5NativeReadback *readback)
{
    V5NativeReadback rtcp;
    V5NativeReadback wcs;
    V5NativeReadback g53;
    V5NativeReadback modal_tool;
    v5_native_readback_init(&rtcp);
    v5_native_readback_init(&wcs);
    v5_native_readback_init(&g53);
    v5_native_readback_init(&modal_tool);
    (void)resident_block_reader_read(&context->rtcp_reader, &rtcp);
    (void)resident_block_reader_read(&context->wcs_reader, &wcs);
    (void)resident_block_reader_read(&context->g53_reader, &g53);
    (void)resident_block_reader_read(&context->modal_reader, &modal_tool);
    v5_state_publisher_merge_scene_readbacks(
        readback, &rtcp, &wcs, &g53, &modal_tool);
}

static int canonical_state_path(const char *path)
{
    return !path || !path[0] || strcmp(path, V5_STATUS_SHM_PATH) == 0;
}

static int context_init(V5StatePublisherContext *context, int scene_request_owner)
{
    if (!context) return 0;
    v5_native_display_sample_reader_init(&context->reader);
    v5_program_scene_request_server_init(&context->request_server);
    v5_program_scene_producer_init(&context->scene_producer);
    v5_program_scene_request_init(&context->latest_request);
    v5_program_scene_producer_set_request(&context->scene_producer, &context->latest_request);
    resident_block_reader_init(
        &context->rtcp_reader,
        V5_NATIVE_RTCP_STATUS_DEFAULT_PATH,
        v5_native_rtcp_status_block_size(),
        V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS,
        v5_native_rtcp_status_read_from_memory);
    resident_block_reader_init(
        &context->wcs_reader,
        V5_NATIVE_WCS_STATUS_DEFAULT_PATH,
        v5_native_wcs_status_block_size(),
        V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS,
        v5_native_wcs_status_read_from_memory);
    resident_block_reader_init(
        &context->g53_reader,
        V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_PATH,
        v5_native_g53_geometry_status_block_size(),
        V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS,
        v5_native_g53_geometry_status_read_from_memory);
    resident_block_reader_init(
        &context->modal_reader,
        V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_PATH,
        v5_native_modal_tool_status_block_size(),
        V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS,
        v5_native_modal_tool_status_read_from_memory);
    if (!scene_request_owner) return 1;
    return v5_program_scene_request_server_open(&context->request_server, 0);
}

static void context_close(V5StatePublisherContext *context)
{
    if (!context) return;
    v5_native_display_sample_reader_close(&context->reader);
    resident_block_reader_close(&context->rtcp_reader);
    resident_block_reader_close(&context->wcs_reader);
    resident_block_reader_close(&context->g53_reader);
    resident_block_reader_close(&context->modal_reader);
    v5_program_scene_request_server_close(&context->request_server, 0);
}

static int build_frame(
    V5StatePublisherContext *context,
    V5StatusShmFrame *frame,
    V5StatePublisherReport *report)
{
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    int available;
    if (!context || !frame) return 0;
    if (v5_program_scene_request_server_drain_latest(
            &context->request_server, &context->latest_request)) {
        v5_program_scene_producer_set_request(
            &context->scene_producer, &context->latest_request);
    }
    available = v5_native_display_sample_reader_read(&context->reader, &sample);
    if (available) v5_status_shm_writer_apply_sample(frame, &sample);
    else v5_status_shm_writer_seed_display_frame(frame);
    frame->status_epoch = monotonic_ns();
    if (available) {
        read_native_scene_inputs(context, &readback);
        (void)v5_state_publisher_apply_scene(
            frame, &context->scene_producer, &sample, &readback);
    }
    apply_cpu_usage(frame, frame->status_epoch);
    if (report) {
        report->sample_available = available;
        report->valid_mask = frame->typed_valid_mask;
        report->frame_flags = frame->flags;
    }
    return 1;
}

int v5_state_publisher_run_loop(
    const char *path,
    unsigned int interval_ms,
    unsigned int max_frames,
    V5StatePublisherReport *report)
{
    unsigned int count = 0U;
    unsigned int period = interval_ms ? interval_ms : V5_STATE_PUBLISHER_INTERVAL_MS;
    V5StatusShmMmapWriter writer;
    V5StatePublisherContext context;
    V5StatePublisherReport local_report = {0};
    V5StatePublisherReport *out = report ? report : &local_report;
    v5_state_publisher_reset_stop();
    v5_status_shm_mmap_writer_init(&writer);
    if (!context_init(&context, canonical_state_path(path))) return 0;
    if (!v5_status_shm_mmap_writer_open(&writer, path)) {
        context_close(&context);
        return 0;
    }
    do {
        V5StatusShmFrame frame;
        uint64_t start_ns = monotonic_ns();
        if (!build_frame(&context, &frame, out) ||
            !v5_status_shm_mmap_writer_publish(&writer, &frame, 0)) {
            context_close(&context);
            v5_status_shm_mmap_writer_close(&writer);
            return 0;
        }
        out->publish_count = ++count;
        out->interval_ms = period;
        if ((max_frames && count >= max_frames) || g_v5_state_publisher_stop_requested) break;
        wait_for_next_start(start_ns, period);
    } while (!g_v5_state_publisher_stop_requested && (!max_frames || count < max_frames));
    out->path = path && path[0] ? path : V5_STATUS_SHM_PATH;
    context_close(&context);
    v5_status_shm_mmap_writer_close(&writer);
    return 1;
}
