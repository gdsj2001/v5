#include "v5_position_status_publisher_core.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void assert_close(double actual, double expected)
{
    assert(fabs(actual - expected) <= 1.0e-12);
}

static V5PositionSourceSnapshot base_source(void)
{
    static const uint32_t codes[V5_POSITION_AXIS_COUNT] = {
        'X', 'Y', 'Z', 'B', 'C'
    };
    V5PositionSourceSnapshot source;
    size_t axis;
    memset(&source, 0, sizeof(source));
    source.display.generation = 7u;
    source.display.active_mask = 0x1fu;
    source.display.commit_seq = 12u;
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        source.display.axis_code[axis] = codes[axis];
        source.display.unit_per_count[axis] = 0.0001;
    }
    source.mapping.state = V5_POSITION_MAPPING_ABSENT;
    source.spindle_speed_rpm = 1200.0;
    source.linear_velocity_mm_per_min = 345.0;
    source.feed_override_percent = 90.0;
    source.spindle_override_percent = 110.0;
    return source;
}

static V5NativePositionStatusBlock build(
    const V5PositionSourceSnapshot *source,
    V5PositionDisplayStabilizer *stabilizer,
    uint64_t generation)
{
    V5NativePositionStatusBlock block;
    assert(v5_position_status_build(
        source, 0x1234abcdu, 2u, 1000000u + generation, generation,
        stabilizer, &block));
    assert(block.magic == V5_POSITION_MAGIC);
    assert(block.version == V5_POSITION_VERSION);
    assert(block.size == V5_POSITION_BLOCK_SIZE);
    assert(block.writer_identity == 0x1234abcdu);
    assert(block.seq == 2u);
    assert(block.source_generation == generation);
    assert(block.crc32 == v5_native_position_status_crc32(&block));
    return block;
}

static void test_absent_mapping_rounding_and_wrap(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    v5_position_display_stabilizer_reset(&stabilizer);
    source.actual[0] = 29.9825;
    source.commanded[0] = -29.9825;
    source.actual[3] = 359.9999;
    source.commanded[3] = -0.0001;
    block = build(&source, &stabilizer, 1u);
    assert((block.valid_mask &
        (V5_POSITION_VALID_MCS | V5_POSITION_VALID_CMD_MCS)) ==
        (V5_POSITION_VALID_MCS | V5_POSITION_VALID_CMD_MCS));
    assert_close(block.mcs[0], 29.983);
    assert_close(block.cmd_mcs[0], -29.983);
    assert_close(block.mcs[3], 0.0);
    assert_close(block.cmd_mcs[3], 0.0);
    assert_close(block.spindle_speed_rpm, 1200.0);
    assert_close(block.linear_velocity_mm_per_min, 345.0);
    assert_close(block.feedrate_override, 90.0);
    assert_close(block.spindle_override, 110.0);
}

static void test_mapping_projection(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    size_t joint;
    v5_position_display_stabilizer_reset(&stabilizer);
    source.mapping.state = V5_POSITION_MAPPING_VALID;
    source.mapping.generation = 44u;
    source.mapping.active_mask = 0x1fu;
    source.mapping.commit_seq = 8u;
    for (joint = 0u; joint < V5_POSITION_AXIS_COUNT; ++joint) {
        source.mapping.joints[joint].generation = 44u;
        source.mapping.joints[joint].status_slot = (uint32_t)joint;
        source.mapping.joints[joint].axis_code =
            source.display.axis_code[joint];
        source.mapping.joints[joint].counts_per_unit =
            joint >= 3u ? 100.0 : 10000.0;
    }
    source.checkpoint[1].valid = 1;
    source.checkpoint[1].generation = 9u;
    source.checkpoint[1].logical_counts = 36123.0;
    source.checkpoint[1].base_counts = 36000.0;
    source.checkpoint[1].runtime_counts = 123.0;
    source.checkpoint[2].valid = 1;
    source.checkpoint[2].generation = 10u;
    source.checkpoint[2].logical_counts = -50.0;
    source.checkpoint[2].base_counts = -100.0;
    source.checkpoint[2].runtime_counts = 50.0;
    source.commanded[3] = 1.0;
    source.commanded[4] = -1.0;
    block = build(&source, &stabilizer, 1u);
    assert_close(block.mcs[3], 1.230);
    assert_close(block.cmd_mcs[3], 1.000);
    assert_close(block.mcs[4], 359.500);
    assert_close(block.cmd_mcs[4], 358.000);
}

static void test_invalid_mapping_degrades_coordinates_only(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    v5_position_display_stabilizer_reset(&stabilizer);
    source.mapping.state = V5_POSITION_MAPPING_INVALID;
    block = build(&source, &stabilizer, 1u);
    assert(!(block.valid_mask & V5_POSITION_VALID_MCS));
    assert(!(block.valid_mask & V5_POSITION_VALID_CMD_MCS));
    assert(block.valid_mask & V5_POSITION_VALID_SPINDLE_SPEED);
    assert(block.valid_mask & V5_POSITION_VALID_LINEAR_VELOCITY);
    assert(block.valid_mask & V5_POSITION_VALID_FEED_OVERRIDE);
    assert(block.valid_mask & V5_POSITION_VALID_SPINDLE_OVERRIDE);
}

static void test_one_pulse_boundary_stabilizer(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock first;
    V5NativePositionStatusBlock boundary;
    V5NativePositionStatusBlock candidate;
    V5NativePositionStatusBlock confirmed;
    v5_position_display_stabilizer_reset(&stabilizer);
    first = build(&source, &stabilizer, 1u);
    source.actual[0] = 0.00051;
    boundary = build(&source, &stabilizer, 2u);
    assert_close(boundary.mcs[0], first.mcs[0]);
    source.actual[0] = 0.00080;
    candidate = build(&source, &stabilizer, 3u);
    assert_close(candidate.mcs[0], first.mcs[0]);
    confirmed = build(&source, &stabilizer, 4u);
    assert_close(confirmed.mcs[0], 0.001);
    source.actual[0] = 0.2504;
    confirmed = build(&source, &stabilizer, 5u);
    assert_close(confirmed.mcs[0], 0.250);
}

static void test_descriptor_driven_rotary_slot(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    static const uint32_t codes[V5_POSITION_AXIS_COUNT] = {
        'B', 'X', 'Y', 'Z', 'C'
    };
    size_t axis;
    v5_position_display_stabilizer_reset(&stabilizer);
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        source.display.axis_code[axis] = codes[axis];
    }
    source.actual[0] = 359.9999;
    source.commanded[0] = -0.0001;
    block = build(&source, &stabilizer, 1u);
    assert_close(block.mcs[0], 0.0);
    assert_close(block.cmd_mcs[0], 0.0);
}

static void test_generation_and_payload_identity(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock first;
    V5NativePositionStatusBlock repeated;
    V5NativePositionStatusBlock changed;
    v5_position_display_stabilizer_reset(&stabilizer);
    first = build(&source, &stabilizer, 1u);
    source.actual[0] = 10.0;
    repeated = build(&source, &stabilizer, 1u);
    assert_close(repeated.mcs[0], first.mcs[0]);
    assert(v5_position_status_display_equal(&first, &repeated));
    changed = build(&source, &stabilizer, 2u);
    assert_close(changed.mcs[0], 10.0);
    assert(!v5_position_status_display_equal(&first, &changed));
    changed.source_acquired_mono_ns += 999u;
    changed.source_generation += 999u;
    changed.seq += 2u;
    assert(v5_position_status_display_equal(&changed, &changed));
}

static void test_fail_closed_metadata(void)
{
    V5PositionSourceSnapshot source = base_source();
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    v5_position_display_stabilizer_reset(&stabilizer);
    source.display.axis_code[4] = source.display.axis_code[3];
    assert(!v5_position_status_build(
        &source, 1u, 2u, 1u, 1u, &stabilizer, &block));
    source = base_source();
    source.display.unit_per_count[2] = 0.0;
    assert(!v5_position_status_build(
        &source, 1u, 2u, 1u, 1u, &stabilizer, &block));
}

int main(void)
{
    test_absent_mapping_rounding_and_wrap();
    test_mapping_projection();
    test_invalid_mapping_degrades_coordinates_only();
    test_one_pulse_boundary_stabilizer();
    test_descriptor_driven_rotary_slot();
    test_generation_and_payload_identity();
    test_fail_closed_metadata();
    puts("V5_POSITION_STATUS_PUBLISHER_SMOKE_OK");
    return 0;
}
