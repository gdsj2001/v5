#ifndef V5_NATIVE_POSITION_STATUS_H
#define V5_NATIVE_POSITION_STATUS_H

#include <stddef.h>
#include <stdint.h>

#define V5_POSITION_MAGIC 0x56504F53u
#define V5_POSITION_VERSION 3u
#define V5_POSITION_AXIS_COUNT 5u
#define V5_POSITION_BLOCK_SIZE 256u
#define V5_POSITION_DEFAULT_PATH "/dev/shm/v5_native_position_status.bin"
#define V5_POSITION_DEFAULT_MAX_AGE_MS 1000u

#define V5_POSITION_VALID_MCS (1u << 0)
#define V5_POSITION_VALID_CMD_MCS (1u << 1)
#define V5_POSITION_VALID_SPINDLE_SPEED (1u << 4)
#define V5_POSITION_VALID_LINEAR_VELOCITY (1u << 5)
#define V5_POSITION_VALID_FEED_OVERRIDE (1u << 6)
#define V5_POSITION_VALID_SPINDLE_OVERRIDE (1u << 7)

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
    double mcs[V5_POSITION_AXIS_COUNT];
    double cmd_mcs[V5_POSITION_AXIS_COUNT];
    double unit_per_count[V5_POSITION_AXIS_COUNT];
    double following_error[V5_POSITION_AXIS_COUNT];
    uint8_t display_digits[V5_POSITION_AXIS_COUNT];
    uint8_t reserved_display[3];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
    uint32_t crc32;
    uint32_t reserved2;
} V5NativePositionStatusBlock;

typedef char V5NativePositionStatusBlockSize[
    sizeof(V5NativePositionStatusBlock) == V5_POSITION_BLOCK_SIZE ? 1 : -1];
typedef char V5NativePositionSeqOffset[
    offsetof(V5NativePositionStatusBlock, seq) == 24u ? 1 : -1];
typedef char V5NativePositionCrcOffset[
    offsetof(V5NativePositionStatusBlock, crc32) == 248u ? 1 : -1];

static inline uint32_t v5_native_position_status_crc32(
    const V5NativePositionStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    const size_t limit = offsetof(V5NativePositionStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0u; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

#endif
