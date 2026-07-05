#ifndef V5_STATUS_SHM_H
#define V5_STATUS_SHM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_STATUS_SHM_PATH "/dev/shm/v3_status_shm"
#define V5_STATUS_SHM_MAGIC 0x56355348u
#define V5_STATUS_SHM_VERSION 1u
#define V5_STATUS_AXIS_COUNT 5u
#define V5_STATUS_TRAJECTORY_POINT_COUNT 16u
#define V5_STATUS_MODAL_TEXT_CAP 128u

enum {
    V5_STATUS_VALID_MCS = 1u << 0,
    V5_STATUS_VALID_CMD_MCS = 1u << 1,
    V5_STATUS_VALID_TRAJECTORY = 1u << 2,
    V5_STATUS_VALID_MODAL = 1u << 3,
    V5_STATUS_VALID_SPINDLE_SPEED = 1u << 4,
    V5_STATUS_VALID_LINEAR_VELOCITY = 1u << 5,
    V5_STATUS_VALID_FEED_OVERRIDE = 1u << 6,
    V5_STATUS_VALID_SPINDLE_OVERRIDE = 1u << 7,
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
    char runtime_modal_text[V5_STATUS_MODAL_TEXT_CAP];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
} V5StatusShmFrame;

void v5_status_shm_frame_init(V5StatusShmFrame *frame);
int v5_status_shm_frame_copy(V5StatusShmFrame *dst, const V5StatusShmFrame *src);
int v5_status_shm_publish_to_memory(void *dst, size_t dst_size, const V5StatusShmFrame *frame);
int v5_status_shm_read_from_memory(V5StatusShmFrame *dst, const void *src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif
