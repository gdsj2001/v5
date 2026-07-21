#ifndef V5_STATUS_SHM_H
#define V5_STATUS_SHM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_STATUS_SHM_PATH "/dev/shm/v3_status_shm"
#define V5_STATUS_SHM_MAGIC 0x56355348u
#define V5_STATUS_SHM_VERSION 3u
#define V5_STATUS_SHM_FRAME_SIZE 7128u
#define V5_STATUS_AXIS_COUNT 5u
#define V5_STATUS_TRAJECTORY_POINT_COUNT 16u
#define V5_STATUS_SCENE_POINT_COUNT 512u
#define V5_STATUS_SCENE_SEGMENT_COUNT 48u
#define V5_STATUS_SCENE_MARKER_COUNT 16u

enum {
    V5_STATUS_VALID_MCS = 1u << 0,
    V5_STATUS_VALID_CMD_MCS = 1u << 1,
    V5_STATUS_VALID_TRAJECTORY = 1u << 2,
    V5_STATUS_VALID_SPINDLE_SPEED = 1u << 4,
    V5_STATUS_VALID_LINEAR_VELOCITY = 1u << 5,
    V5_STATUS_VALID_FEED_OVERRIDE = 1u << 6,
    V5_STATUS_VALID_SPINDLE_OVERRIDE = 1u << 7,
    V5_STATUS_VALID_CPU_USAGE = 1u << 8,
    V5_STATUS_VALID_DISPLAY_SCENE = 1u << 9,
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

typedef struct V5StatusScreenPoint {
    float x;
    float y;
} V5StatusScreenPoint;

enum {
    V5_STATUS_SCENE_SEGMENT_MCS_AXIS = 1u,
    V5_STATUS_SCENE_SEGMENT_WCS_AXIS = 2u,
    V5_STATUS_SCENE_SEGMENT_MODEL_AXIS = 3u,
    V5_STATUS_SCENE_SEGMENT_HOLDER = 4u,
};

enum {
    V5_STATUS_SCENE_MARKER_MCS_ORIGIN = 1u,
    V5_STATUS_SCENE_MARKER_WCS_ORIGIN = 2u,
    V5_STATUS_SCENE_MARKER_MODEL_CENTER = 3u,
    V5_STATUS_SCENE_MARKER_MCS_ACTUAL = 4u,
    V5_STATUS_SCENE_MARKER_CMD_TIP = 5u,
};

enum {
    V5_STATUS_SCENE_FLAG_VALID = 1u << 0,
    V5_STATUS_SCENE_FLAG_PROGRAM = 1u << 1,
    V5_STATUS_SCENE_FLAG_RTCP = 1u << 2,
    V5_STATUS_SCENE_FLAG_MODEL = 1u << 3,
    V5_STATUS_SCENE_FLAG_WCS = 1u << 4,
    V5_STATUS_SCENE_FLAG_DIRTY_STATIC = 1u << 8,
    V5_STATUS_SCENE_FLAG_DIRTY_MODEL = 1u << 9,
    V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC = 1u << 10,
    V5_STATUS_SCENE_FLAG_DIRTY_KNOWN = 1u << 11,
    V5_STATUS_SCENE_FLAG_DIRTY_PROGRAM = 1u << 12,
};

#define V5_STATUS_SCENE_FLAG_DIRTY_MASK \
    (V5_STATUS_SCENE_FLAG_DIRTY_STATIC | \
     V5_STATUS_SCENE_FLAG_DIRTY_MODEL | \
     V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC | \
     V5_STATUS_SCENE_FLAG_DIRTY_PROGRAM)

enum {
    V5_STATUS_SCENE_PLANE_XY = 0u,
    V5_STATUS_SCENE_PLANE_XZ = 1u,
    V5_STATUS_SCENE_PLANE_YZ = 2u,
    V5_STATUS_SCENE_PLANE_3D = 3u,
};

typedef struct V5StatusSceneSegment {
    V5StatusScreenPoint start;
    V5StatusScreenPoint end;
    uint16_t role;
    uint16_t index;
    uint32_t flags;
} V5StatusSceneSegment;

typedef struct V5StatusSceneMarker {
    V5StatusScreenPoint point;
    uint16_t role;
    uint16_t index;
    uint32_t flags;
} V5StatusSceneMarker;

typedef struct V5StatusDisplayScene {
    uint64_t program_source_identity;
    uint64_t program_generation;
    uint64_t native_generation;
    uint64_t active_model_generation;
    uint64_t rtcp_generation;
    uint64_t wcs_generation;
    uint64_t view_generation;
    uint64_t fit_generation;
    uint64_t build_count;
    uint64_t rtcp_transform_count;
    uint64_t project_count;
    uint32_t active_model_id;
    uint32_t flags;
    uint32_t point_count;
    uint32_t segment_count;
    uint32_t marker_count;
    uint32_t program_wcs_mask;
    /* Coarse v3 ABI damage bounds retained for readers and diagnostics. */
    int16_t dirty_x1;
    int16_t dirty_y1;
    int16_t dirty_x2;
    int16_t dirty_y2;
    uint8_t primary_axis;
    uint8_t child_axis;
    uint8_t current_wcs_index;
    uint8_t reserved_axis;
    uint32_t plane;
    float primary_center[3];
    float child_center[3];
    V5StatusScreenPoint points[V5_STATUS_SCENE_POINT_COUNT];
    uint8_t break_before[V5_STATUS_SCENE_POINT_COUNT];
    V5StatusSceneSegment segments[V5_STATUS_SCENE_SEGMENT_COUNT];
    V5StatusSceneMarker markers[V5_STATUS_SCENE_MARKER_COUNT];
} V5StatusDisplayScene;

typedef char V5StatusDisplaySceneSizeMustMatchAbi[
    sizeof(V5StatusDisplayScene) == 6168U ? 1 : -1];
typedef char V5StatusDisplayScenePlaneOffsetMustMatchAbi[
    offsetof(V5StatusDisplayScene, plane) == 124U ? 1 : -1];

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
    uint32_t position_writer_identity;
    uint32_t reserved_identity;
    uint64_t source_acquired_mono_ns;
    uint64_t source_generation;
    uint64_t scene_generation;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    double unit_per_count[V5_STATUS_AXIS_COUNT];
    double following_error[V5_STATUS_AXIS_COUNT];
    uint8_t display_digits[V5_STATUS_AXIS_COUNT];
    uint8_t reserved_display[3];
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
    V5StatusDisplayScene display_scene;
} V5StatusShmFrame;

typedef char V5StatusShmFrameSizeMustMatchAbi[
    sizeof(V5StatusShmFrame) == V5_STATUS_SHM_FRAME_SIZE ? 1 : -1];
typedef char V5StatusShmCpu0OffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu0_percent) == 928U ? 1 : -1];
typedef char V5StatusShmCpu1OffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu1_percent) == 936U ? 1 : -1];
typedef char V5StatusShmCpuGenerationOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu_sample_generation) == 944U ? 1 : -1];
typedef char V5StatusShmCpuTimeOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, cpu_sample_monotonic_ns) == 952U ? 1 : -1];
typedef char V5StatusShmDisplaySceneOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, display_scene) == 960U ? 1 : -1];
typedef char V5StatusShmUnitPerCountOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, unit_per_count) == 160U ? 1 : -1];
typedef char V5StatusShmFollowingErrorOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, following_error) == 200U ? 1 : -1];
typedef char V5StatusShmDisplayDigitsOffsetMustMatchAbi[
    offsetof(V5StatusShmFrame, display_digits) == 240U ? 1 : -1];

void v5_status_shm_frame_init(V5StatusShmFrame *frame);
int v5_status_shm_frame_copy(V5StatusShmFrame *dst, const V5StatusShmFrame *src);
int v5_status_shm_publish_to_memory(void *dst, size_t dst_size, const V5StatusShmFrame *frame);
int v5_status_shm_read_from_memory(V5StatusShmFrame *dst, const void *src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif
