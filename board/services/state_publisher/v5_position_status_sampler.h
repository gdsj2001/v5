#ifndef V5_POSITION_STATUS_SAMPLER_H
#define V5_POSITION_STATUS_SAMPLER_H

#include "v5_position_status_publisher_core.h"

#include <stdbool.h>
#include <stdint.h>

#define V5_BUS_JOINT_COUNT 5u

typedef enum V5HalType {
    V5_HAL_TYPE_UNSPECIFIED = -1,
    V5_HAL_TYPE_UNINITIALIZED = 0,
    V5_HAL_BIT = 1,
    V5_HAL_FLOAT = 2,
    V5_HAL_S32 = 3,
    V5_HAL_U32 = 4,
    V5_HAL_PORT = 5
} V5HalType;

typedef union V5HalData {
    volatile bool b;
    volatile int32_t s;
    volatile uint32_t u;
    volatile double f;
    volatile int p;
} V5HalData;

typedef struct V5HalApi {
    void *library;
    int component_id;
    int (*init)(const char *name);
    int (*ready)(int component_id);
    int (*exit)(int component_id);
    int (*get_pin)(
        const char *name,
        V5HalType *type,
        V5HalData **data,
        bool *connected);
} V5HalApi;

typedef struct V5HalRef {
    V5HalType type;
    V5HalData *data;
} V5HalRef;

typedef struct V5PositionHalPins {
    V5HalRef display_valid;
    V5HalRef display_generation;
    V5HalRef display_active_mask;
    V5HalRef display_commit_seq;
    V5HalRef display_axis_code[V5_POSITION_AXIS_COUNT];
    V5HalRef display_unit_per_count[V5_POSITION_AXIS_COUNT];
    V5HalRef mapping_valid;
    V5HalRef mapping_generation;
    V5HalRef mapping_active_mask;
    V5HalRef mapping_commit_seq;
    V5HalRef home_generation[V5_POSITION_AXIS_COUNT];
    V5HalRef home_status_slot[V5_POSITION_AXIS_COUNT];
    V5HalRef home_axis_code[V5_POSITION_AXIS_COUNT];
    V5HalRef home_slave_position[V5_POSITION_AXIS_COUNT];
    V5HalRef home_counts_per_unit[V5_POSITION_AXIS_COUNT];
    V5HalRef checkpoint_valid[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_generation[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_logical[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_base[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_runtime[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef actual[V5_POSITION_AXIS_COUNT];
    V5HalRef commanded[V5_POSITION_AXIS_COUNT];
    V5HalRef spindle_speed_rps;
    V5HalRef linear_velocity_per_second;
    V5HalRef feed_override_ratio;
    V5HalRef spindle_override_ratio;
    V5HalRef router_valid;
    V5HalRef router_generation;
    V5HalRef router_active_mask;
    V5HalRef master_link_up;
    V5HalRef master_state_op;
    V5HalRef master_all_op;
    V5HalRef slaves_responding;
    V5HalRef slave_statusword[V5_BUS_JOINT_COUNT];
} V5PositionHalPins;

typedef struct V5BusJointStatusBlock {
    uint32_t valid;
    uint32_t axis_code;
    uint32_t slave_position;
    uint32_t flags;
    uint32_t statusword;
} V5BusJointStatusBlock;

#pragma pack(push, 1)
typedef struct V5BusStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t sequence;
    uint32_t writer_identity;
    uint32_t mapping_generation;
    uint32_t active_mask;
    uint32_t master_flags;
    uint32_t slaves_responding;
    uint32_t joint_count;
    uint32_t source_generation;
    uint64_t monotonic_ns;
    V5BusJointStatusBlock joints[V5_BUS_JOINT_COUNT];
    uint32_t crc32;
    uint32_t reserved;
} V5BusStatusBlock;
#pragma pack(pop)

typedef char V5BusStatusBlockSize[
    sizeof(V5BusStatusBlock) == 164u ? 1 : -1];

typedef struct V5PositionStatusSampler {
    V5HalApi hal_api;
    V5PositionHalPins pins;
} V5PositionStatusSampler;

int v5_position_status_sampler_open(V5PositionStatusSampler *sampler);
void v5_position_status_sampler_close(V5PositionStatusSampler *sampler);
int v5_position_status_sampler_sample_source(
    const V5PositionStatusSampler *sampler,
    V5PositionSourceSnapshot *source);
int v5_position_status_sampler_sample_bus(
    const V5PositionStatusSampler *sampler,
    uint32_t writer_identity,
    uint32_t sequence,
    uint32_t source_generation,
    uint64_t timestamp,
    V5BusStatusBlock *block);

#endif
