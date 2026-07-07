#include "v5_native_sample.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define V5_POSITION_MAGIC 0x56504F53u
#define V5_POSITION_VERSION 2u
#define V5_POSITION_DEFAULT_PATH "/dev/shm/v5_native_position_status.bin"
#define V5_POSITION_DEFAULT_MAX_AGE_MS 1000U

typedef struct V5NativePositionStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid_mask;
    uint32_t axis_count;
    uint32_t reserved;
    uint64_t monotonic_ns;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
    uint32_t crc32;
    uint32_t reserved2;
} V5NativePositionStatusBlock;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t crc32_like(const V5NativePositionStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    size_t limit = offsetof(V5NativePositionStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static const char *position_status_path(void)
{
    const char *path = getenv("V5_NATIVE_POSITION_STATUS_PATH");
    return (path && path[0]) ? path : V5_POSITION_DEFAULT_PATH;
}

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

static int block_fresh(const V5NativePositionStatusBlock *block)
{
    uint64_t now = monotonic_ns();
    uint64_t max_age_ns = (uint64_t)V5_POSITION_DEFAULT_MAX_AGE_MS * 1000000ULL;
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    return now - block->monotonic_ns <= max_age_ns;
}

void v5_native_display_sample_init(V5NativeDisplaySample *sample)
{
    if (!sample) {
        return;
    }

    memset(sample, 0, sizeof(*sample));
}

int v5_native_display_sample_read(V5NativeDisplaySample *sample)
{
    FILE *fp;
    V5NativePositionStatusBlock block;
    uint32_t mask;

    v5_native_display_sample_init(sample);
    if (!sample) {
        return 0;
    }

    fp = fopen(position_status_path(), "rb");
    if (!fp) {
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    if (block.magic != V5_POSITION_MAGIC || block.version != V5_POSITION_VERSION ||
        block.size != (uint32_t)sizeof(block) || block.axis_count != V5_STATUS_AXIS_COUNT ||
        block.crc32 != crc32_like(&block) || !block_fresh(&block)) {
        return 0;
    }

    mask = block.valid_mask;
    if ((mask & V5_STATUS_VALID_MCS) && !finite_axis(block.mcs)) {
        mask &= ~V5_STATUS_VALID_MCS;
    }
    if ((mask & V5_STATUS_VALID_CMD_MCS) && !finite_axis(block.cmd_mcs)) {
        mask &= ~V5_STATUS_VALID_CMD_MCS;
    }
    if ((mask & V5_STATUS_VALID_SPINDLE_SPEED) && !isfinite(block.spindle_speed_rpm)) {
        mask &= ~V5_STATUS_VALID_SPINDLE_SPEED;
    }
    if ((mask & V5_STATUS_VALID_LINEAR_VELOCITY) && !isfinite(block.linear_velocity_mm_per_min)) {
        mask &= ~V5_STATUS_VALID_LINEAR_VELOCITY;
    }
    if ((mask & V5_STATUS_VALID_FEED_OVERRIDE) && !isfinite(block.feedrate_override)) {
        mask &= ~V5_STATUS_VALID_FEED_OVERRIDE;
    }
    if ((mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) && !isfinite(block.spindle_override)) {
        mask &= ~V5_STATUS_VALID_SPINDLE_OVERRIDE;
    }
    if (!(mask & (V5_STATUS_VALID_MCS |
                   V5_STATUS_VALID_CMD_MCS |
                   V5_STATUS_VALID_SPINDLE_SPEED |
                   V5_STATUS_VALID_LINEAR_VELOCITY |
                   V5_STATUS_VALID_FEED_OVERRIDE |
                   V5_STATUS_VALID_SPINDLE_OVERRIDE))) {
        return 0;
    }

    sample->available = 1;
    sample->valid_mask = mask;
    memcpy(sample->mcs, block.mcs, sizeof(sample->mcs));
    memcpy(sample->cmd_mcs, block.cmd_mcs, sizeof(sample->cmd_mcs));
    sample->spindle_speed_rpm = block.spindle_speed_rpm;
    sample->linear_velocity_mm_per_min = block.linear_velocity_mm_per_min;
    sample->feedrate_override = block.feedrate_override;
    sample->spindle_override = block.spindle_override;
    return 1;
}
