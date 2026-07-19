#include "v5_state_publisher_service.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct V5StatePublisherSmokePositionBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid_mask;
    uint32_t axis_count;
    uint32_t writer_identity;
    uint32_t seq;
    uint32_t reserved;
    uint64_t source_acquired_mono_ns;
    uint64_t source_generation;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    double unit_per_count[V5_STATUS_AXIS_COUNT];
    double following_error[V5_STATUS_AXIS_COUNT];
    uint8_t display_digits[V5_STATUS_AXIS_COUNT];
    uint8_t reserved_display[3];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
    uint32_t crc32;
    uint32_t reserved2;
} V5StatePublisherSmokePositionBlock;

typedef char V5StatePublisherSmokePositionBlockSize[
    sizeof(V5StatePublisherSmokePositionBlock) == 256U ? 1 : -1];

static uint64_t smoke_monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0ULL;
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static uint32_t smoke_position_crc(
    const V5StatePublisherSmokePositionBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    uint32_t hash = 2166136261U;
    size_t i;
    for (i = 0U; i < offsetof(
            V5StatePublisherSmokePositionBlock, crc32); ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static int write_all(int fd, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t written = 0U;
    while (written < size) {
        ssize_t count = write(fd, bytes + written, size - written);
        if (count <= 0) return 0;
        written += (size_t)count;
    }
    return 1;
}

static int create_position_fixture(
    const char *state_path,
    char *fixture_path,
    size_t fixture_path_size)
{
    V5StatePublisherSmokePositionBlock block;
    uint64_t now_ns = smoke_monotonic_ns();
    unsigned int axis;
    int fd;
    int length;
    if (!state_path || !fixture_path || fixture_path_size == 0U || !now_ns) {
        return 0;
    }
    length = snprintf(
        fixture_path, fixture_path_size, "%s.position-fixture", state_path);
    if (length < 0 || (size_t)length >= fixture_path_size) return 0;
    memset(&block, 0, sizeof(block));
    block.magic = 0x56504f53U;
    block.version = 3U;
    block.size = (uint32_t)sizeof(block);
    block.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    block.axis_count = V5_STATUS_AXIS_COUNT;
    block.writer_identity = 0x53544d4bU;
    block.seq = 2U;
    block.source_acquired_mono_ns = now_ns;
    block.source_generation = 1ULL;
    for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
        block.mcs[axis] = (double)axis;
        block.cmd_mcs[axis] = (double)axis;
        block.unit_per_count[axis] = 0.001;
        block.following_error[axis] = 0.0;
        block.display_digits[axis] = 3U;
    }
    block.feedrate_override = 100.0;
    block.spindle_override = 100.0;
    block.crc32 = smoke_position_crc(&block);
    fd = open(fixture_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 0;
    if (!write_all(fd, &block, sizeof(block)) || fsync(fd) != 0) {
        close(fd);
        unlink(fixture_path);
        return 0;
    }
    close(fd);
    return 1;
}

static int scene_readback_merge_smoke(void)
{
    V5NativeReadback rtcp;
    V5NativeReadback wcs;
    V5NativeReadback g53;
    V5NativeReadback modal_tool;
    V5NativeReadback merged;
    double offsets[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT] = {{0}};
    double centers[V5_NATIVE_READBACK_G53_CENTER_COUNT][V5_NATIVE_READBACK_G53_AXIS_COUNT] = {{0}};
    v5_native_readback_init(&rtcp);
    v5_native_readback_init(&wcs);
    v5_native_readback_init(&g53);
    v5_native_readback_init(&modal_tool);
    v5_native_readback_set_rtcp_actual(&rtcp, 0);
    v5_native_readback_set_wcs_table(
        &wcs, 0, &offsets[0][0],
        V5_NATIVE_READBACK_WCS_COUNT,
        V5_NATIVE_READBACK_WCS_AXIS_COUNT, 7U);
    v5_native_readback_set_g53_geometry(
        &g53, &centers[0][0],
        V5_NATIVE_READBACK_G53_CENTER_COUNT,
        V5_NATIVE_READBACK_G53_AXIS_COUNT, 9U);
    v5_native_readback_set_motion_model(&g53, "XYZAC_TRT");
    v5_native_readback_set_modal_actual(&modal_tool, "G54");
    v5_native_readback_set_tool_actual(&modal_tool, 1, 1, 25.0);
    v5_state_publisher_merge_scene_readbacks(
        &merged, &rtcp, &wcs, &g53, &modal_tool);
    return v5_native_readback_rtcp_known(&merged) &&
        !merged.rtcp_enabled &&
        v5_native_readback_wcs_table_known(&merged) &&
        v5_native_readback_g53_geometry_known(&merged) &&
        v5_native_readback_motion_model_known(&merged) &&
        v5_native_readback_modal_known(&merged) &&
        v5_native_readback_tool_length_known(&merged);
}

static int scene_failure_preserves_position_smoke(void)
{
    V5StatusShmFrame frame;
    V5ProgramSceneProducer producer;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    unsigned int axis;
    size_t byte;
    v5_status_shm_frame_init(&frame);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 17ULL;
    for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
        sample.mcs[axis] = 10.0 + (double)axis;
        sample.cmd_mcs[axis] = 20.0 + (double)axis;
        frame.mcs[axis] = sample.mcs[axis];
        frame.cmd_mcs[axis] = sample.cmd_mcs[axis];
    }
    frame.typed_valid_mask = sample.valid_mask | V5_STATUS_VALID_DISPLAY_SCENE;
    frame.scene_generation = 99ULL;
    memset(&frame.display_scene, 0xa5, sizeof(frame.display_scene));
    v5_program_scene_producer_init(&producer);
    v5_native_readback_init(&readback);
    v5_native_readback_set_rtcp_actual(&readback, 1);
    v5_native_readback_set_motion_model(&readback, "UNKNOWN_MODEL");
    if (v5_state_publisher_apply_scene(
            &frame, &producer, &sample, &readback)) return 0;
    if ((frame.typed_valid_mask &
            (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS)) !=
            (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS) ||
        (frame.typed_valid_mask & V5_STATUS_VALID_DISPLAY_SCENE) != 0U ||
        frame.scene_generation != 0ULL) return 0;
    for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
        if (frame.mcs[axis] != sample.mcs[axis] ||
            frame.cmd_mcs[axis] != sample.cmd_mcs[axis]) return 0;
    }
    for (byte = 0U; byte < sizeof(frame.display_scene); ++byte) {
        if (((const unsigned char *)&frame.display_scene)[byte] != 0U) return 0;
    }
    return 1;
}

static unsigned int parse_u32(const char *text, unsigned int default_value)
{
    char *end = 0;
    unsigned long value;
    if (!text || !text[0]) {
        return default_value;
    }
    value = strtoul(text, &end, 10);
    if (!end || *end || value > 0xfffffffful) {
        return default_value;
    }
    return (unsigned int)value;
}

int main(int argc, char **argv)
{
    const char *path = V5_STATUS_SHM_PATH;
    unsigned int frames = 1u;
    unsigned int interval_ms = V5_STATE_PUBLISHER_INTERVAL_MS;
    int unlink_after = 0;
    int result = 0;
    int i;
    char position_fixture_path[1024] = {0};
    V5StatusShmFrame readback;
    V5StatePublisherReport report = {0};

    if (!scene_readback_merge_smoke()) {
        fprintf(stderr, "scene readback merge smoke failed\n");
        return 6;
    }
    if (!scene_failure_preserves_position_smoke()) {
        fprintf(stderr, "scene failure must preserve Position coordinates\n");
        return 7;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = parse_u32(argv[++i], frames);
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            interval_ms = parse_u32(argv[++i], interval_ms);
        } else if (strcmp(argv[i], "--daemon") == 0) {
            frames = 0u;
        } else if (strcmp(argv[i], "--unlink-after") == 0) {
            unlink_after = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_state_publisher_smoke [--path PATH] [--frames N|--daemon] [--interval-ms 33] [--unlink-after]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!create_position_fixture(
            path, position_fixture_path, sizeof(position_fixture_path)) ||
        setenv("V5_NATIVE_POSITION_STATUS_PATH", position_fixture_path, 1) != 0) {
        unlink(position_fixture_path);
        fprintf(stderr, "cannot create valid Position smoke fixture\n");
        return 8;
    }
    if (!v5_state_publisher_run_loop(path, interval_ms, frames, &report)) {
        result = 1;
        goto cleanup;
    }
    if (!v5_status_shm_read_from_path(path, &readback)) {
        result = 3;
        goto cleanup;
    }
    if (!report.sample_available &&
        (readback.typed_valid_mask & V5_STATUS_NATIVE_DISPLAY_VALID_MASK) != 0U) {
        fprintf(stderr, "unavailable native sample must not publish valid native defaults: mask=0x%08x\n", readback.typed_valid_mask);
        result = 4;
        goto cleanup;
    }
    if (readback.version != V5_STATUS_SHM_VERSION ||
        readback.total_size != V5_STATUS_SHM_FRAME_SIZE ||
        readback.payload_size != V5_STATUS_SHM_FRAME_SIZE - 32U) {
        fprintf(stderr, "unexpected status ABI: version=%u total=%u payload=%u\n",
                readback.version, readback.total_size, readback.payload_size);
        result = 5;
        goto cleanup;
    }
    printf(
        "v5 state publisher shm: version=%u valid_mask=0x%08x flags=0x%08x sample_available=%d frames=%u interval_ms=%u bytes=%u path=%s\n",
        readback.version,
        readback.typed_valid_mask,
        readback.flags,
        report.sample_available,
        report.publish_count,
        report.interval_ms,
        readback.total_size,
        path);
cleanup:
    unsetenv("V5_NATIVE_POSITION_STATUS_PATH");
    unlink(position_fixture_path);
    if (unlink_after) unlink(path);
    return result;
}
