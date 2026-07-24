#include "v5_status_shm.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define V5_STATUS_SHM_READ_RETRY_COUNT 3U

static V5StatusShmFrame g_status_shm_last_good_frame;
static int g_status_shm_has_last_good_frame;

static void v5_status_shm_memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

static uint32_t v5_status_shm_crc32_update(uint32_t crc, const unsigned char *data, size_t size)
{
    size_t i;
    crc = ~crc;
    for (i = 0U; i < size; ++i) {
        unsigned int bit;
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static uint32_t v5_status_shm_crc32(const V5StatusShmFrame *frame)
{
    uint32_t crc = 0U;
    const unsigned char *base = (const unsigned char *)frame;
    size_t seq_offset = offsetof(V5StatusShmFrame, seq);
    size_t payload_offset = offsetof(V5StatusShmFrame, status_epoch);

    crc = v5_status_shm_crc32_update(crc, base, seq_offset);
    crc = v5_status_shm_crc32_update(crc, base + payload_offset, sizeof(*frame) - payload_offset);
    return crc;
}

static int v5_status_shm_frame_header_ok(const V5StatusShmFrame *frame)
{
    return frame &&
           frame->magic == V5_STATUS_SHM_MAGIC &&
           frame->version == V5_STATUS_SHM_VERSION &&
           frame->header_size == (uint32_t)sizeof(*frame) &&
           frame->total_size == (uint32_t)sizeof(*frame) &&
           frame->payload_size == (uint32_t)(sizeof(*frame) - offsetof(V5StatusShmFrame, status_epoch)) &&
           frame->trajectory_count <= V5_STATUS_TRAJECTORY_POINT_COUNT &&
           frame->display_scene.point_count <= V5_STATUS_SCENE_POINT_COUNT &&
           frame->display_scene.segment_count <= V5_STATUS_SCENE_SEGMENT_COUNT &&
           frame->display_scene.marker_count <= V5_STATUS_SCENE_MARKER_COUNT;
}

static int v5_status_shm_axis_finite(const double axis[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(axis[i])) return 0;
    }
    return 1;
}

static int v5_status_shm_scene_payload_ok(const V5StatusDisplayScene *scene)
{
    uint32_t i;
    if (!scene || (scene->flags & V5_STATUS_SCENE_FLAG_VALID) == 0U ||
        scene->native_generation == 0ULL || scene->view_generation == 0ULL ||
        scene->fit_generation == 0ULL || scene->build_count == 0ULL ||
        scene->project_count == 0ULL ||
        scene->plane > V5_STATUS_SCENE_PLANE_3D ||
        (scene->program_wcs_mask & ~0x1ffU) != 0U ||
        (scene->current_wcs_index > 8U && scene->current_wcs_index != 255U)) {
        return 0;
    }
    for (i = 0U; i < scene->point_count; ++i) {
        if (!isfinite(scene->points[i].x) || !isfinite(scene->points[i].y) ||
            scene->break_before[i] > 1U) return 0;
    }
    for (i = 0U; i < scene->segment_count; ++i) {
        if (!isfinite(scene->segments[i].start.x) ||
            !isfinite(scene->segments[i].start.y) ||
            !isfinite(scene->segments[i].end.x) ||
            !isfinite(scene->segments[i].end.y)) return 0;
    }
    for (i = 0U; i < scene->marker_count; ++i) {
        if (!isfinite(scene->markers[i].point.x) ||
            !isfinite(scene->markers[i].point.y)) return 0;
    }
    if ((scene->flags & V5_STATUS_SCENE_FLAG_MODEL) != 0U) {
        for (i = 0U; i < 3U; ++i) {
            if (!isfinite(scene->primary_center[i]) ||
                !isfinite(scene->child_center[i])) return 0;
        }
    }
    if ((scene->flags &
         V5_STATUS_SCENE_FLAG_TOOL_TIP_CONTOUR_ERROR) != 0U &&
        (!isfinite(scene->tool_tip_contour_error) ||
         scene->tool_tip_contour_error < 0.0)) return 0;
    return 1;
}

static int v5_status_shm_frame_payload_ok(const V5StatusShmFrame *frame)
{
    const uint32_t known_mask =
        V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_TRAJECTORY | V5_STATUS_VALID_SPINDLE_SPEED |
        V5_STATUS_VALID_LINEAR_VELOCITY | V5_STATUS_VALID_FEED_OVERRIDE |
        V5_STATUS_VALID_SPINDLE_OVERRIDE | V5_STATUS_VALID_CPU_USAGE |
        V5_STATUS_VALID_DISPLAY_SCENE;
    uint32_t i;
    if (!frame || frame->position_writer_identity == 0U ||
        frame->source_acquired_mono_ns == 0ULL ||
        frame->source_generation == 0ULL ||
        (frame->typed_valid_mask & ~known_mask) != 0U) return 0;
    if ((frame->typed_valid_mask &
         (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS)) != 0U) {
        for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
            if (!isfinite(frame->unit_per_count[i]) ||
                frame->unit_per_count[i] <= 0.0 ||
                !isfinite(frame->following_error[i]) ||
                frame->display_digits[i] != 3U) return 0;
        }
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_MCS) != 0U &&
        !v5_status_shm_axis_finite(frame->mcs)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U &&
        !v5_status_shm_axis_finite(frame->cmd_mcs)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_TRAJECTORY) != 0U) {
        for (i = 0U; i < frame->trajectory_count; ++i) {
            if (!v5_status_shm_axis_finite(frame->trajectory[i].axis)) return 0;
        }
    }
    if ((frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) != 0U &&
        !isfinite(frame->spindle_speed_rpm)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_LINEAR_VELOCITY) != 0U &&
        !isfinite(frame->linear_velocity_mm_per_min)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_FEED_OVERRIDE) != 0U &&
        !isfinite(frame->feedrate_override)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) != 0U &&
        !isfinite(frame->spindle_override)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_CPU_USAGE) != 0U &&
        (!isfinite(frame->cpu0_percent) || frame->cpu0_percent < 0.0 ||
         frame->cpu0_percent > 100.0 ||
         !isfinite(frame->cpu1_percent) || frame->cpu1_percent < 0.0 ||
         frame->cpu1_percent > 100.0 ||
         frame->cpu_sample_generation == 0ULL ||
         frame->cpu_sample_monotonic_ns == 0ULL)) return 0;
    if ((frame->typed_valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) != 0U &&
        (frame->scene_generation == 0ULL ||
         !v5_status_shm_scene_payload_ok(&frame->display_scene))) return 0;
    return 1;
}

static int v5_status_shm_frame_crc_ok(const V5StatusShmFrame *frame)
{
    return v5_status_shm_frame_header_ok(frame) &&
           v5_status_shm_frame_payload_ok(frame) &&
           (frame->seq & 1U) == 0U &&
           frame->seq != 0U &&
           frame->crc32 == v5_status_shm_crc32(frame);
}

static int v5_status_shm_copy_stale_last_good(V5StatusShmFrame *dst)
{
    if (!dst || !g_status_shm_has_last_good_frame) {
        return 0;
    }
    *dst = g_status_shm_last_good_frame;
    dst->flags |= V5_STATUS_FRAME_FLAG_STALE;
    dst->typed_flags |= V5_STATUS_FRAME_FLAG_STALE;
    return 1;
}

void v5_status_shm_frame_init(V5StatusShmFrame *frame)
{
    if (!frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->magic = V5_STATUS_SHM_MAGIC;
    frame->version = V5_STATUS_SHM_VERSION;
    frame->header_size = (uint32_t)sizeof(V5StatusShmFrame);
    frame->total_size = (uint32_t)sizeof(V5StatusShmFrame);
    frame->payload_size = (uint32_t)(sizeof(V5StatusShmFrame) - offsetof(V5StatusShmFrame, status_epoch));
}

int v5_status_shm_frame_copy(V5StatusShmFrame *dst, const V5StatusShmFrame *src)
{
    if (!dst || !src) {
        return 0;
    }
    if (!v5_status_shm_frame_crc_ok(src)) {
        return 0;
    }

    memcpy(dst, src, sizeof(*dst));
    return 1;
}

int v5_status_shm_publish_to_memory(void *dst, size_t dst_size, const V5StatusShmFrame *frame)
{
    V5StatusShmFrame *page;
    V5StatusShmFrame local;
    uint32_t base_seq;
    uint32_t odd_seq;
    uint32_t even_seq;

    if (!dst || !frame || dst_size < sizeof(*frame)) {
        return 0;
    }
    if (!v5_status_shm_frame_header_ok(frame)) {
        return 0;
    }

    page = (V5StatusShmFrame *)dst;
    local = *frame;
    base_seq = page->seq;
    if (base_seq & 1U) {
        ++base_seq;
    }
    odd_seq = base_seq + 1U;
    even_seq = base_seq + 2U;
    if (even_seq == 0U) {
        odd_seq = 1U;
        even_seq = 2U;
    }

    local.seq = odd_seq;
    local.crc32 = v5_status_shm_crc32(&local);

    page->seq = odd_seq;
    v5_status_shm_memory_barrier();
    memcpy(page, &local, sizeof(*page));
    v5_status_shm_memory_barrier();
    page->seq = even_seq;
    v5_status_shm_memory_barrier();
    return 1;
}

int v5_status_shm_read_from_memory(V5StatusShmFrame *dst, const void *src, size_t src_size)
{
    const V5StatusShmFrame *page;
    unsigned int attempt;

    if (!dst || !src || src_size < sizeof(*dst)) {
        return 0;
    }

    page = (const V5StatusShmFrame *)src;
    for (attempt = 0U; attempt < V5_STATUS_SHM_READ_RETRY_COUNT; ++attempt) {
        V5StatusShmFrame local;
        uint32_t before_seq = page->seq;
        if (before_seq == 0U || (before_seq & 1U) != 0U) {
            continue;
        }
        v5_status_shm_memory_barrier();
        memcpy(&local, page, sizeof(local));
        v5_status_shm_memory_barrier();
        if (before_seq != page->seq || local.seq != before_seq || (local.seq & 1U) != 0U) {
            continue;
        }
        if (v5_status_shm_frame_copy(dst, &local)) {
            g_status_shm_last_good_frame = *dst;
            g_status_shm_has_last_good_frame = 1;
            return 1;
        }
    }
    return v5_status_shm_copy_stale_last_good(dst);
}
