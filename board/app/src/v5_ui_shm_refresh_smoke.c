#include "v5_ui_model.h"
#include "v5_ui_refresh_schedule.h"
#include "v5_status_shm_mmap.h"

#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned long long smoke_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000000000ULL) + (unsigned long long)ts.tv_nsec;
}

static void set_source_metadata(V5StatusShmFrame *frame)
{
    static uint64_t generation = 0ULL;
    generation += 1ULL;
    frame->position_writer_identity = 17U;
    frame->source_acquired_mono_ns = smoke_monotonic_ns();
    frame->source_generation = generation;
    frame->scene_generation = generation;
}

static void build_good_frame(V5StatusShmFrame *frame)
{
    unsigned int i;
    v5_status_shm_frame_init(frame);
    frame->typed_valid_mask =
        V5_STATUS_VALID_MCS |
        V5_STATUS_VALID_CMD_MCS |
        V5_STATUS_VALID_DISPLAY_SCENE |
        V5_STATUS_VALID_SPINDLE_SPEED |
        V5_STATUS_VALID_LINEAR_VELOCITY |
        V5_STATUS_VALID_FEED_OVERRIDE |
        V5_STATUS_VALID_SPINDLE_OVERRIDE;
    set_source_metadata(frame);
    frame->status_epoch = frame->source_acquired_mono_ns;
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        frame->mcs[i] = (double)i + 1.0;
        frame->cmd_mcs[i] = (double)i + 0.5;
        frame->unit_per_count[i] = 0.0001;
        frame->following_error[i] = frame->mcs[i] - frame->cmd_mcs[i];
        frame->display_digits[i] = 3U;
    }
    frame->spindle_speed_rpm = 0.0;
    frame->linear_velocity_mm_per_min = 0.0;
    frame->feedrate_override = 100.0;
    frame->spindle_override = 100.0;
    frame->display_scene.flags = V5_STATUS_SCENE_FLAG_VALID;
    frame->display_scene.native_generation = frame->source_generation;
    frame->display_scene.view_generation = 1ULL;
    frame->display_scene.fit_generation = 1ULL;
    frame->display_scene.build_count = 1ULL;
    frame->display_scene.project_count = 1ULL;
    frame->display_scene.point_count = 1U;
    frame->display_scene.points[0].x = 10.0f;
    frame->display_scene.points[0].y = 20.0f;
}

static void build_degraded_frame(V5StatusShmFrame *frame)
{
    v5_status_shm_frame_init(frame);
    frame->typed_valid_mask = V5_STATUS_VALID_SPINDLE_SPEED;
    frame->flags = V5_STATUS_FRAME_FLAG_DEGRADED;
    set_source_metadata(frame);
    frame->status_epoch = frame->source_acquired_mono_ns;
    frame->spindle_speed_rpm = 0.0;
}

static void build_cpu_only_frame(V5StatusShmFrame *frame)
{
    v5_status_shm_frame_init(frame);
    frame->typed_valid_mask = V5_STATUS_VALID_CPU_USAGE;
    frame->flags = V5_STATUS_FRAME_FLAG_DEGRADED |
                   V5_STATUS_FRAME_FLAG_UNAVAILABLE;
    set_source_metadata(frame);
    frame->status_epoch = frame->source_acquired_mono_ns;
    frame->cpu0_percent = 12.5;
    frame->cpu1_percent = 34.5;
    frame->cpu_sample_generation = 7ULL;
    frame->cpu_sample_monotonic_ns = frame->status_epoch;
}

static uint32_t smoke_crc32_update(
    uint32_t crc,
    const unsigned char *data,
    size_t size)
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

static uint32_t smoke_frame_crc32(const V5StatusShmFrame *frame)
{
    const unsigned char *base = (const unsigned char *)frame;
    uint32_t crc = 0U;
    crc = smoke_crc32_update(
        crc, base, offsetof(V5StatusShmFrame, seq));
    return smoke_crc32_update(
        crc,
        base + offsetof(V5StatusShmFrame, status_epoch),
        sizeof(*frame) - offsetof(V5StatusShmFrame, status_epoch));
}

static int bounded_count_rejection_smoke(void)
{
    unsigned int variant;
    for (variant = 0U; variant < 4U; ++variant) {
        V5StatusShmFrame source;
        V5StatusShmFrame page;
        V5StatusShmFrame decoded;
        build_good_frame(&source);
        memset(&page, 0, sizeof(page));
        if (!v5_status_shm_publish_to_memory(
                &page, sizeof(page), &source)) return 0;
        switch (variant) {
        case 0U:
            page.trajectory_count = V5_STATUS_TRAJECTORY_POINT_COUNT + 1U;
            break;
        case 1U:
            page.display_scene.point_count = V5_STATUS_SCENE_POINT_COUNT + 1U;
            break;
        case 2U:
            page.display_scene.segment_count = V5_STATUS_SCENE_SEGMENT_COUNT + 1U;
            break;
        default:
            page.display_scene.marker_count = V5_STATUS_SCENE_MARKER_COUNT + 1U;
            break;
        }
        page.crc32 = smoke_frame_crc32(&page);
        if (v5_status_shm_read_from_memory(
                &decoded, &page, sizeof(page))) return 0;
    }
    return 1;
}

static int payload_contract_rejection_smoke(void)
{
    unsigned int variant;
    for (variant = 0U; variant < 6U; ++variant) {
        V5StatusShmFrame source;
        V5StatusShmFrame page;
        V5StatusShmFrame decoded;
        build_good_frame(&source);
        memset(&page, 0, sizeof(page));
        if (!v5_status_shm_publish_to_memory(
                &page, sizeof(page), &source)) return 0;
        switch (variant) {
        case 0U: page.position_writer_identity = 0U; break;
        case 1U: page.source_generation = 0ULL; break;
        case 2U: page.scene_generation = 0ULL; break;
        case 3U: page.unit_per_count[0] = NAN; break;
        case 4U: page.following_error[0] = NAN; break;
        default: page.display_scene.points[0].x = NAN; break;
        }
        page.crc32 = smoke_frame_crc32(&page);
        if (v5_status_shm_read_from_memory(
                &decoded, &page, sizeof(page))) return 0;
    }
    return 1;
}

static int full_capacity_independence_smoke(void)
{
    V5StatusShmFrame source;
    V5StatusShmFrame page;
    V5StatusShmFrame decoded;
    V5UiModel model;
    unsigned int axis;
    unsigned int i;
    build_good_frame(&source);
    source.typed_valid_mask |=
        V5_STATUS_VALID_TRAJECTORY | V5_STATUS_VALID_DISPLAY_SCENE;
    source.trajectory_count = V5_STATUS_TRAJECTORY_POINT_COUNT;
    for (i = 0U; i < V5_STATUS_TRAJECTORY_POINT_COUNT; ++i) {
        for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
            source.trajectory[i].axis[axis] =
                (double)(i * 100U + axis);
        }
    }
    source.display_scene.flags |= V5_STATUS_SCENE_FLAG_PROGRAM;
    source.display_scene.point_count = V5_STATUS_SCENE_POINT_COUNT;
    for (i = 0U; i < V5_STATUS_SCENE_POINT_COUNT; ++i) {
        source.display_scene.points[i].x = (float)i;
        source.display_scene.points[i].y = (float)(1000U + i);
        source.display_scene.break_before[i] = i == 0U ? 1U : 0U;
    }
    memset(&page, 0, sizeof(page));
    if (!v5_status_shm_publish_to_memory(
            &page, sizeof(page), &source) ||
        !v5_status_shm_read_from_memory(
            &decoded, &page, sizeof(page)) ||
        decoded.trajectory_count != V5_STATUS_TRAJECTORY_POINT_COUNT ||
        decoded.display_scene.point_count != V5_STATUS_SCENE_POINT_COUNT ||
        decoded.trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT - 1U].axis[4] != 1504.0 ||
        decoded.display_scene.points[V5_STATUS_SCENE_POINT_COUNT - 1U].x != 511.0f ||
        decoded.display_scene.points[V5_STATUS_SCENE_POINT_COUNT - 1U].y != 1511.0f) {
        return 0;
    }
    v5_ui_model_init(&model);
    if (!v5_ui_model_apply_status_frame(&model, &decoded) ||
        model.status_view.trajectory_count != V5_STATUS_TRAJECTORY_POINT_COUNT ||
        !model.status_view.display_scene ||
        model.status_view.display_scene->point_count != V5_STATUS_SCENE_POINT_COUNT ||
        model.status_view.trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT - 1U].axis[4] != 1504.0 ||
        model.status_view.display_scene->points[V5_STATUS_SCENE_POINT_COUNT - 1U].x != 511.0f ||
        model.status_view.display_scene->points[V5_STATUS_SCENE_POINT_COUNT - 1U].y != 1511.0f) {
        v5_ui_model_close_status_reader(&model);
        return 0;
    }
    v5_ui_model_close_status_reader(&model);
    return 1;
}

static int rewrite_frame(const char *path, V5StatusShmFrame *frame)
{
    FILE *fp = fopen(path, "r+b");
    if (!fp) {
        return 0;
    }
    if (fwrite(frame, 1U, sizeof(*frame), fp) != sizeof(*frame)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int read_raw_frame(const char *path, V5StatusShmFrame *frame)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fread(frame, 1U, sizeof(*frame), fp) != sizeof(*frame)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int corrupt_crc(const char *path)
{
    V5StatusShmFrame frame;
    if (!read_raw_frame(path, &frame)) {
        return 0;
    }
    frame.crc32 ^= 0x55aa55aaU;
    return rewrite_frame(path, &frame);
}

static int force_odd_seq(const char *path)
{
    V5StatusShmFrame frame;
    if (!read_raw_frame(path, &frame)) {
        return 0;
    }
    frame.seq |= 1U;
    return rewrite_frame(path, &frame);
}

int main(void)
{
    const char *path = "/dev/shm/v5_ui_shm_refresh_smoke";
    V5UiModel model;
    V5StatusShmFrame frame;
    uint64_t refresh_anchor_ns = 0ULL;
    uint64_t resident_reader_inode = 0ULL;
    int resident_reader_fd = -1;
    int reconnect_attempt;
    V5UiModel cpu_only_model;
    V5UiModel zero_copy_model;
    V5StatusShmFrame second_frame;
    const V5StatusDisplayScene *first_scene;

    if (!bounded_count_rejection_smoke()) {
        return 18;
    }
    if (!payload_contract_rejection_smoke()) {
        return 19;
    }
    if (!full_capacity_independence_smoke()) {
        return 23;
    }

    v5_ui_model_init(&zero_copy_model);
    build_good_frame(&frame);
    if (!v5_ui_model_apply_status_frame(&zero_copy_model, &frame) ||
        !zero_copy_model.status_view.display_scene) return 20;
    first_scene = zero_copy_model.status_view.display_scene;
    if (first_scene != &zero_copy_model.status_frame_slots[
            zero_copy_model.status_frame_active_slot].display_scene) return 21;
    build_good_frame(&second_frame);
    second_frame.display_scene.points[0].x = 30.0f;
    if (!v5_ui_model_apply_status_frame(&zero_copy_model, &second_frame) ||
        !zero_copy_model.status_view.display_scene ||
        zero_copy_model.status_view.display_scene == first_scene ||
        zero_copy_model.status_view.display_scene !=
            &zero_copy_model.status_frame_slots[
                zero_copy_model.status_frame_active_slot].display_scene ||
        zero_copy_model.status_view.display_scene->points[0].x != 30.0f) return 22;

    if (!v5_ui_refresh_deadline_due(1000000000ULL, &refresh_anchor_ns, 33333333ULL) ||
        refresh_anchor_ns != 1000000000ULL ||
        v5_ui_refresh_deadline_due(1030000000ULL, &refresh_anchor_ns, 33333333ULL) ||
        !v5_ui_refresh_deadline_due(1040000000ULL, &refresh_anchor_ns, 33333333ULL) ||
        refresh_anchor_ns != 1033333333ULL ||
        !v5_ui_refresh_deadline_due(1070000000ULL, &refresh_anchor_ns, 33333333ULL) ||
        refresh_anchor_ns != 1066666666ULL ||
        v5_ui_refresh_wait_ns(1070000000ULL, refresh_anchor_ns, 33333333ULL, 10000000ULL) != 10000000ULL ||
        v5_ui_refresh_wait_ns(1099500000ULL, refresh_anchor_ns, 33333333ULL, 10000000ULL) != 499999ULL) {
        return 13;
    }

    unlink(path);
    v5_ui_model_init(&model);

    v5_ui_model_init(&cpu_only_model);
    build_cpu_only_frame(&frame);
    if (!v5_ui_model_apply_status_frame(&cpu_only_model, &frame) ||
        cpu_only_model.status_view.valid_mask != V5_STATUS_VALID_CPU_USAGE ||
        cpu_only_model.status_view.cpu0_percent != 12.5 ||
        cpu_only_model.status_view.cpu1_percent != 34.5 ||
        cpu_only_model.status_view.cpu_sample_generation != 7ULL) {
        v5_ui_model_close_status_reader(&cpu_only_model);
        return 17;
    }
    v5_ui_model_close_status_reader(&cpu_only_model);

    build_good_frame(&frame);
    if (!v5_status_shm_publish_to_path(path, &frame, 0)) {
        return 1;
    }
    if (!v5_ui_model_refresh_status_from_shm(&model, path)) {
        unlink(path);
        return 2;
    }
    resident_reader_fd = model.status_reader.fd;
    resident_reader_inode = model.status_reader.inode_id;
    if (resident_reader_fd < 0 || model.status_reader.open_count != 1U) {
        v5_ui_model_close_status_reader(&model);
        unlink(path);
        return 14;
    }
    if ((model.status_view.valid_mask & V5_STATUS_VALID_MCS) == 0u ||
        (model.status_view.valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) == 0u) {
        unlink(path);
        return 4;
    }
    if ((model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_STALE) != 0u) {
        unlink(path);
        return 8;
    }
    if (!corrupt_crc(path) ||
        !v5_ui_model_refresh_status_from_shm(&model, path) ||
        model.status_reader.fd != resident_reader_fd ||
        model.status_reader.open_count != 1U ||
        (model.status_view.valid_mask & V5_STATUS_VALID_MCS) == 0u ||
        (model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_STALE) == 0U) {
        unlink(path);
        return 10;
    }
    if (!v5_status_shm_read_from_path(path, &frame) ||
        (frame.typed_flags & V5_STATUS_FRAME_FLAG_STALE) == 0U ||
        (frame.flags & V5_STATUS_FRAME_FLAG_STALE) == 0U) {
        unlink(path);
        return 12;
    }
    build_good_frame(&frame);
    if (!v5_status_shm_publish_to_path(path, &frame, 0) ||
        !force_odd_seq(path) ||
        !v5_ui_model_refresh_status_from_shm(&model, path) ||
        model.status_reader.fd != resident_reader_fd ||
        model.status_reader.open_count != 1U ||
        (model.status_view.valid_mask & V5_STATUS_VALID_MCS) == 0u ||
        (model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_STALE) == 0U) {
        unlink(path);
        return 11;
    }

    unlink(path);
    build_good_frame(&frame);
    frame.mcs[0] = 42.0001;
    if (!v5_status_shm_publish_to_path(path, &frame, 0)) {
        v5_ui_model_close_status_reader(&model);
        unlink(path);
        return 15;
    }
    for (reconnect_attempt = 0; reconnect_attempt < 8; ++reconnect_attempt) {
        (void)v5_ui_model_refresh_status_from_shm(&model, path);
        if (model.status_reader.open_count == 2U && model.status_view.mcs[0] == 42.0001) {
            break;
        }
        usleep(40000);
    }
    if (model.status_reader.open_count != 2U ||
        model.status_reader.inode_id == resident_reader_inode ||
        model.status_view.mcs[0] != 42.0001) {
        v5_ui_model_close_status_reader(&model);
        unlink(path);
        return 16;
    }

    build_degraded_frame(&frame);
    if (!v5_ui_model_apply_status_frame(&model, &frame) ||
        (model.status_view.valid_mask & V5_STATUS_VALID_MCS) == 0u ||
        (model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_STALE) == 0U) {
        unlink(path);
        return 5;
    }

    v5_ui_model_close_status_reader(&model);
    unlink(path);
    if (!v5_ui_model_refresh_status_from_shm(&model, path) ||
        (model.status_view.valid_mask & V5_STATUS_VALID_MCS) == 0u) {
        return 6;
    }
    usleep(600000);
    if (v5_ui_model_refresh_status_from_shm(&model, path) ||
        (model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_UNAVAILABLE) == 0U ||
        model.status_view.valid_mask != 0u) {
        return 7;
    }


    build_good_frame(&frame);
    frame.source_acquired_mono_ns =
        smoke_monotonic_ns() + 10000000000ULL;
    if (v5_ui_model_apply_status_frame(&model, &frame) ||
        (model.status_view.frame_flags & V5_STATUS_FRAME_FLAG_UNAVAILABLE) == 0U ||
        model.status_view.valid_mask != 0u) {
        unlink(path);
        return 9;
    }

    printf(
        "v5 ui shm refresh: valid_mask=0x%08x flags=0x%08x epoch=%llu reader_opens=%u\n",
        model.status_view.valid_mask,
        model.status_view.frame_flags,
        (unsigned long long)model.status_view.status_epoch,
        model.status_reader.open_count);
    v5_ui_model_close_status_reader(&model);
    return 0;
}
