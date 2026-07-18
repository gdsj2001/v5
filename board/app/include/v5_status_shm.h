#ifndef V5_STATUS_SHM_H
#define V5_STATUS_SHM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_STATUS_SHM_PATH "/dev/shm/v3_status_shm"
#define V5_STATUS_SHM_MAGIC 0x56355348u
#define V5_STATUS_SHM_VERSION 2u
#define V5_STATUS_SHM_FRAME_SIZE 840u
#define V5_STATUS_AXIS_COUNT 5u
#define V5_STATUS_TRAJECTORY_POINT_COUNT 16u

enum {
    V5_STATUS_VALID_MCS = 1u << 0,
    V5_STATUS_VALID_CMD_MCS = 1u << 1,
    V5_STATUS_VALID_TRAJECTORY = 1u << 2,
    V5_STATUS_VALID_SPINDLE_SPEED = 1u << 4,
    V5_STATUS_VALID_LINEAR_VELOCITY = 1u << 5,
    V5_STATUS_VALID_FEED_OVERRIDE = 1u << 6,
    V5_STATUS_VALID_SPINDLE_OVERRIDE = 1u << 7,
    V5_STATUS_VALID_CPU_USAGE = 1u << 8,
};

#define V5_STATUS_NATIVE_DISPLAY_VALID_MASK \
    (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS | \
     V5_STATUS_VALID_TRAJECTORY | V5_STATUS_VALID_SPINDLE_SPEED | \
     V5_STATUS_VALID_LINEAR_VELOCITY | V5_STATUS_VALID_FEED_OVERRIDE | \
     V5_STATUS_VALID_SPINDLE_OVERRIDE)

enum {
    V5_STATUS_FRAME_FLAG_DEGRADED = 1u << 0,
    V5_STATUS_FRAME_FLAG_STALE = 1u << 1,
    V5_STATUS_FRAME_FLAG_UNAVAILABLE = 1u << 2,
};

typedef struct V5StatusPoint {
    double axis[V5_STATUS_AXIS_COUNT];
} V5StatusPoint;

typedef struct V5StatusShmFrame {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t payload_size;
    uint32_t flags;
    uint32_t seq;
    uint32_t crc32;
    uint64_t status_epoch;
    uint32_t typed_valid_mask;
    uint32_t typed_flags;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    V5StatusPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    uint32_t trajectory_count;
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
    double cpu0_percent;
    double cpu1_percent;
    uint64_t cpu_sample_generation;
    uint64_t cpu_sample_monotonic_ns;
} V5StatusShmFrame;

typedef char V5StatusShmFrameSizeMustMatchAbi[
    sizeof(V5StatusShmFrame) == V5_STATUS_SHM_FRAME_SIZE ? 1 : -1];
typedef char V5StatusShmCpu0OffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu0_percent) == 808U ? 1 : -1];
typedef char V5StatusShmCpu1OffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu1_percent) == 816U ? 1 : -1];
typedef char V5StatusShmCpuGenerationOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu_sample_generation) == 824U ? 1 : -1];
typedef char V5StatusShmCpuTimeOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu_sample_monotonic_ns) == 832U ? 1 : -1];

void v5_status_shm_frame_init(V5StatusShmFrame *frame);
int v5_status_shm_frame_copy(V5StatusShmFrame *dst, const V5StatusShmFrame *src);
int v5_status_shm_publish_to_memory(void *dst, size_t dst_size, const V5StatusShmFrame *frame);
int v5_status_shm_read_from_memory(V5StatusShmFrame *dst, const void *src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif
