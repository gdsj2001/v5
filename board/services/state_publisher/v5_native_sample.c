#include "v5_native_sample.h"

#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define V5_POSITION_MAGIC 0x56504F53u
#define V5_POSITION_VERSION 3u
#define V5_POSITION_DEFAULT_PATH "/dev/shm/v5_native_position_status.bin"
#define V5_POSITION_DEFAULT_MAX_AGE_MS 1000U
#define V5_POSITION_READ_RETRY_COUNT 3U

typedef struct V5NativePositionStatusBlock {
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
} V5NativePositionStatusBlock;

typedef char V5NativePositionStatusBlockSize[
    sizeof(V5NativePositionStatusBlock) == 256U ? 1 : -1];

static void memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

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

static int display_metadata_valid(const V5NativePositionStatusBlock *block)
{
    unsigned int axis;
    if (!block) return 0;
    for (axis = 0U; axis < V5_STATUS_AXIS_COUNT; ++axis) {
        if (!isfinite(block->unit_per_count[axis]) ||
            block->unit_per_count[axis] <= 0.0 ||
            !isfinite(block->following_error[axis]) ||
            block->display_digits[axis] != 3U) {
            return 0;
        }
    }
    return 1;
}

static int block_fresh(const V5NativePositionStatusBlock *block)
{
    uint64_t now = monotonic_ns();
    uint64_t max_age_ns = (uint64_t)V5_POSITION_DEFAULT_MAX_AGE_MS * 1000000ULL;
    if (!now || !block->source_acquired_mono_ns ||
        now < block->source_acquired_mono_ns) {
        return 0;
    }
    return now - block->source_acquired_mono_ns <= max_age_ns;
}

void v5_native_display_sample_reader_init(V5NativeDisplaySampleReader *reader)
{
    if (!reader) {
        return;
    }
    memset(reader, 0, sizeof(*reader));
    reader->fd = -1;
}

void v5_native_display_sample_reader_close(V5NativeDisplaySampleReader *reader)
{
    if (!reader) {
        return;
    }
    if (reader->page) {
        munmap(reader->page, reader->mapped_size);
    }
    if (reader->fd >= 0) {
        close(reader->fd);
    }
    v5_native_display_sample_reader_init(reader);
}

static int reader_open(V5NativeDisplaySampleReader *reader)
{
    struct stat status;
    const char *path = position_status_path();
    void *page;
    int fd;
    if (!reader) {
        return 0;
    }
    if (reader->fd >= 0 && reader->page) {
        return 1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    if (fstat(fd, &status) != 0 ||
        status.st_size < (off_t)sizeof(V5NativePositionStatusBlock)) {
        close(fd);
        return 0;
    }
    page = mmap(0, sizeof(V5NativePositionStatusBlock), PROT_READ, MAP_SHARED, fd, 0);
    if (page == MAP_FAILED) {
        close(fd);
        return 0;
    }
    reader->fd = fd;
    reader->page = page;
    reader->mapped_size = sizeof(V5NativePositionStatusBlock);
    reader->device_id = (uint64_t)status.st_dev;
    reader->inode_id = (uint64_t)status.st_ino;
    reader->open_count += 1U;
    return 1;
}

static int reader_backing_matches(const V5NativeDisplaySampleReader *reader)
{
    struct stat descriptor_status;
    struct stat path_status;
    if (!reader || reader->fd < 0 || !reader->page ||
        fstat(reader->fd, &descriptor_status) != 0 ||
        stat(position_status_path(), &path_status) != 0) {
        return 0;
    }
    return descriptor_status.st_size >= (off_t)reader->mapped_size &&
        path_status.st_size >= (off_t)reader->mapped_size &&
        (uint64_t)descriptor_status.st_dev == reader->device_id &&
        (uint64_t)descriptor_status.st_ino == reader->inode_id &&
        (uint64_t)path_status.st_dev == reader->device_id &&
        (uint64_t)path_status.st_ino == reader->inode_id;
}

static int reader_copy_block(
    V5NativeDisplaySampleReader *reader,
    V5NativePositionStatusBlock *block)
{
    const V5NativePositionStatusBlock *page;
    unsigned int attempt;
    if (!reader_open(reader) || !block) {
        return 0;
    }
    page = (const V5NativePositionStatusBlock *)reader->page;
    for (attempt = 0U; attempt < V5_POSITION_READ_RETRY_COUNT; ++attempt) {
        uint32_t before_seq = page->seq;
        if (!before_seq || (before_seq & 1U)) {
            continue;
        }
        memory_barrier();
        memcpy(block, page, sizeof(*block));
        memory_barrier();
        if (before_seq == page->seq && block->seq == before_seq &&
            !(block->seq & 1U)) {
            return 1;
        }
    }
    return 0;
}

void v5_native_display_sample_init(V5NativeDisplaySample *sample)
{
    if (sample) {
        memset(sample, 0, sizeof(*sample));
    }
}

int v5_native_display_sample_reader_read(
    V5NativeDisplaySampleReader *reader,
    V5NativeDisplaySample *sample)
{
    V5NativePositionStatusBlock block;
    uint32_t mask;

    v5_native_display_sample_init(sample);
    if (!reader || !sample || !reader_copy_block(reader, &block)) {
        if (reader) {
            reader->failure_count += 1U;
            if (reader->failure_count >= V5_POSITION_READ_RETRY_COUNT &&
                !reader_backing_matches(reader)) {
                v5_native_display_sample_reader_close(reader);
            }
        }
        return 0;
    }
    reader->failure_count = 0U;

    if (block.magic != V5_POSITION_MAGIC ||
        block.version != V5_POSITION_VERSION ||
        block.size != (uint32_t)sizeof(block) ||
        block.axis_count != V5_STATUS_AXIS_COUNT ||
        block.writer_identity == 0U ||
        block.source_generation == 0ULL ||
        block.crc32 != crc32_like(&block) ||
        !block_fresh(&block) ||
        !display_metadata_valid(&block)) {
        if (!reader_backing_matches(reader)) {
            v5_native_display_sample_reader_close(reader);
        }
        return 0;
    }

    mask = block.valid_mask;
    if ((mask & V5_STATUS_VALID_MCS) && !finite_axis(block.mcs)) {
        mask &= ~V5_STATUS_VALID_MCS;
    }
    if ((mask & V5_STATUS_VALID_CMD_MCS) && !finite_axis(block.cmd_mcs)) {
        mask &= ~V5_STATUS_VALID_CMD_MCS;
    }
    if ((mask & V5_STATUS_VALID_SPINDLE_SPEED) &&
        !isfinite(block.spindle_speed_rpm)) {
        mask &= ~V5_STATUS_VALID_SPINDLE_SPEED;
    }
    if ((mask & V5_STATUS_VALID_LINEAR_VELOCITY) &&
        !isfinite(block.linear_velocity_mm_per_min)) {
        mask &= ~V5_STATUS_VALID_LINEAR_VELOCITY;
    }
    if ((mask & V5_STATUS_VALID_FEED_OVERRIDE) &&
        !isfinite(block.feedrate_override)) {
        mask &= ~V5_STATUS_VALID_FEED_OVERRIDE;
    }
    if ((mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) &&
        !isfinite(block.spindle_override)) {
        mask &= ~V5_STATUS_VALID_SPINDLE_OVERRIDE;
    }
    if (!(mask & V5_STATUS_NATIVE_DISPLAY_VALID_MASK)) {
        return 0;
    }

    sample->available = 1;
    sample->valid_mask = mask;
    sample->writer_identity = block.writer_identity;
    sample->source_acquired_mono_ns = block.source_acquired_mono_ns;
    sample->source_generation = block.source_generation;
    memcpy(sample->mcs, block.mcs, sizeof(sample->mcs));
    memcpy(sample->cmd_mcs, block.cmd_mcs, sizeof(sample->cmd_mcs));
    memcpy(sample->unit_per_count, block.unit_per_count, sizeof(sample->unit_per_count));
    memcpy(sample->following_error, block.following_error, sizeof(sample->following_error));
    memcpy(sample->display_digits, block.display_digits, sizeof(sample->display_digits));
    sample->spindle_speed_rpm = block.spindle_speed_rpm;
    sample->linear_velocity_mm_per_min = block.linear_velocity_mm_per_min;
    sample->feedrate_override = block.feedrate_override;
    sample->spindle_override = block.spindle_override;
    reader->writer_identity = block.writer_identity;
    reader->source_generation = block.source_generation;
    return 1;
}
