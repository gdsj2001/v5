#ifndef V5_POSITION_STATUS_PUBLISHER_CORE_H
#define V5_POSITION_STATUS_PUBLISHER_CORE_H

#include "v5_native_position_status.h"

#include <stdint.h>

#define V5_POSITION_ROTARY_AXIS_COUNT 3u
#define V5_POSITION_DISPLAY_SCALE 1000.0
#define V5_POSITION_ROTARY_PERIOD_BUCKETS 360000LL

enum {
    V5_POSITION_MAPPING_ABSENT = 0,
    V5_POSITION_MAPPING_VALID = 1,
    V5_POSITION_MAPPING_INVALID = 2
};

typedef struct V5PositionDisplayMetadata {
    uint32_t generation;
    uint32_t active_mask;
    uint32_t commit_seq;
    uint32_t axis_code[V5_POSITION_AXIS_COUNT];
    double unit_per_count[V5_POSITION_AXIS_COUNT];
} V5PositionDisplayMetadata;

typedef struct V5PositionHomeJoint {
    uint32_t generation;
    uint32_t status_slot;
    uint32_t axis_code;
    double counts_per_unit;
} V5PositionHomeJoint;

typedef struct V5PositionHomeMapping {
    int state;
    uint32_t generation;
    uint32_t active_mask;
    uint32_t commit_seq;
    V5PositionHomeJoint joints[V5_POSITION_AXIS_COUNT];
} V5PositionHomeMapping;

typedef struct V5PositionRotaryCheckpoint {
    int valid;
    uint32_t generation;
    double logical_counts;
    double base_counts;
    double runtime_counts;
} V5PositionRotaryCheckpoint;

typedef struct V5PositionSourceSnapshot {
    double actual[V5_POSITION_AXIS_COUNT];
    double commanded[V5_POSITION_AXIS_COUNT];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feed_override_percent;
    double spindle_override_percent;
    V5PositionDisplayMetadata display;
    V5PositionHomeMapping mapping;
    V5PositionRotaryCheckpoint checkpoint[V5_POSITION_ROTARY_AXIS_COUNT];
} V5PositionSourceSnapshot;

typedef struct V5PositionDisplayStabilizer {
    int64_t stable[V5_POSITION_AXIS_COUNT * 2u];
    int64_t candidate[V5_POSITION_AXIS_COUNT * 2u];
    uint8_t stable_valid[V5_POSITION_AXIS_COUNT * 2u];
    uint8_t candidate_valid[V5_POSITION_AXIS_COUNT * 2u];
    uint64_t last_generation;
    int last_generation_valid;
} V5PositionDisplayStabilizer;

void v5_position_display_stabilizer_reset(
    V5PositionDisplayStabilizer *stabilizer);

int v5_position_status_build(
    const V5PositionSourceSnapshot *source,
    uint32_t writer_identity,
    uint32_t sequence,
    uint64_t source_acquired_mono_ns,
    uint64_t source_generation,
    V5PositionDisplayStabilizer *stabilizer,
    V5NativePositionStatusBlock *block);

int v5_position_status_display_equal(
    const V5NativePositionStatusBlock *left,
    const V5NativePositionStatusBlock *right);

#endif
