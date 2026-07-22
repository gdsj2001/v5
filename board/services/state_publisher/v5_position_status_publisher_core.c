#include "v5_position_status_publisher_core.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define V5_DISPLAY_EXPECTED_MASK ((1u << V5_POSITION_AXIS_COUNT) - 1u)
#define V5_INTEGER_TOLERANCE 1.0e-6

static int axis_code_valid(uint32_t code)
{
    return code == (uint32_t)'X' || code == (uint32_t)'Y' ||
        code == (uint32_t)'Z' || code == (uint32_t)'A' ||
        code == (uint32_t)'B' || code == (uint32_t)'C';
}

static int rotary_index(uint32_t code)
{
    if (code == (uint32_t)'A') return 0;
    if (code == (uint32_t)'B') return 1;
    if (code == (uint32_t)'C') return 2;
    return -1;
}

static int display_metadata_valid(const V5PositionDisplayMetadata *metadata)
{
    uint32_t seen = 0u;
    size_t axis;

    if (!metadata || !metadata->generation || !metadata->commit_seq ||
        metadata->active_mask != V5_DISPLAY_EXPECTED_MASK) {
        return 0;
    }
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        uint32_t code = metadata->axis_code[axis];
        uint32_t bit;
        if (!axis_code_valid(code) || !isfinite(metadata->unit_per_count[axis]) ||
            metadata->unit_per_count[axis] <= 0.0) {
            return 0;
        }
        bit = 1u << (code - (uint32_t)'A');
        if (seen & bit) return 0;
        seen |= bit;
    }
    return 1;
}

static int exact_count(double value, int64_t *count)
{
    double rounded;
    if (!count || !isfinite(value)) return 0;
    rounded = round(value);
    if (fabs(value - rounded) > V5_INTEGER_TOLERANCE) return 0;
    *count = (int64_t)rounded;
    return 1;
}

static double positive_phase(double value, double period)
{
    double phase = fmod(value, period);
    if (phase < 0.0) phase += period;
    return phase == 0.0 ? 0.0 : phase;
}

static int rotary_phase_degrees(
    double relative_counts,
    int64_t counts_per_revolution,
    double counts_per_unit,
    double *degrees)
{
    double rounded;
    double phase;
    if (!degrees || !isfinite(relative_counts) ||
        counts_per_revolution <= 0 || !isfinite(counts_per_unit) ||
        counts_per_unit == 0.0) {
        return 0;
    }
    rounded = round(relative_counts);
    if (fabs(relative_counts - rounded) <= V5_INTEGER_TOLERANCE) {
        relative_counts = rounded;
    }
    phase = positive_phase(relative_counts, (double)counts_per_revolution);
    *degrees = phase / fabs(counts_per_unit);
    return isfinite(*degrees);
}

static double display_projection(double value)
{
    double scaled = value * V5_POSITION_DISPLAY_SCALE;
    double bucket;
    double projected;
    if (!isfinite(scaled)) return value;
    bucket = scaled >= 0.0 ? floor(scaled + 0.5 + 1.0e-9) :
        ceil(scaled - 0.5 - 1.0e-9);
    projected = bucket / V5_POSITION_DISPLAY_SCALE;
    return projected == 0.0 ? 0.0 : projected;
}

static double rotary_display_projection(double value)
{
    double scaled = value * V5_POSITION_DISPLAY_SCALE;
    int64_t bucket;

    if (!isfinite(scaled)) return value;
    bucket = (int64_t)(scaled >= 0.0 ?
        floor(scaled + 0.5 + 1.0e-9) :
        ceil(scaled - 0.5 - 1.0e-9));
    bucket %= V5_POSITION_ROTARY_PERIOD_BUCKETS;
    if (bucket < 0) bucket += V5_POSITION_ROTARY_PERIOD_BUCKETS;
    return bucket == 0 ? 0.0 :
        (double)bucket / V5_POSITION_DISPLAY_SCALE;
}

static int project_positions(
    const V5PositionSourceSnapshot *source,
    double mcs[V5_POSITION_AXIS_COUNT],
    double commanded[V5_POSITION_AXIS_COUNT])
{
    size_t axis;
    int rotary_records = 0;
    uint32_t seen_slots = 0u;

    memcpy(mcs, source->actual, sizeof(source->actual));
    memcpy(commanded, source->commanded, sizeof(source->commanded));
    if (source->mapping.state == V5_POSITION_MAPPING_ABSENT) {
        for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
            if (rotary_index(source->display.axis_code[axis]) >= 0) {
                mcs[axis] = positive_phase(mcs[axis], 360.0);
                commanded[axis] = positive_phase(commanded[axis], 360.0);
            }
        }
        return 1;
    }
    if (source->mapping.state != V5_POSITION_MAPPING_VALID ||
        !source->mapping.generation || !source->mapping.commit_seq ||
        !source->mapping.active_mask ||
        (source->mapping.active_mask & ~V5_DISPLAY_EXPECTED_MASK)) {
        return 0;
    }
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        const V5PositionHomeJoint *joint;
        int index;
        int64_t counts_per_revolution;
        const V5PositionRotaryCheckpoint *checkpoint;
        int64_t base_counts;
        double command_counts;
        if (!(source->mapping.active_mask & (1u << axis))) continue;
        joint = &source->mapping.joints[axis];
        if (joint->generation != source->mapping.generation ||
            joint->status_slot >= V5_POSITION_AXIS_COUNT ||
            !isfinite(joint->counts_per_unit) || joint->counts_per_unit <= 0.0) {
            return 0;
        }
        index = rotary_index(joint->axis_code);
        if (index < 0) continue;
        if ((seen_slots & (1u << joint->status_slot)) ||
            source->display.axis_code[joint->status_slot] != joint->axis_code ||
            !exact_count(fabs(joint->counts_per_unit) * 360.0,
                &counts_per_revolution) || counts_per_revolution <= 0) {
            return 0;
        }
        seen_slots |= 1u << joint->status_slot;
        checkpoint = &source->checkpoint[index];
        if (!checkpoint->valid || !checkpoint->generation ||
            !isfinite(checkpoint->logical_counts) ||
            !exact_count(checkpoint->base_counts, &base_counts) ||
            !isfinite(checkpoint->runtime_counts) ||
            fabs(checkpoint->runtime_counts -
                (checkpoint->logical_counts - (double)base_counts)) >
                V5_INTEGER_TOLERANCE) {
            return 0;
        }
        if (!rotary_phase_degrees(
                checkpoint->logical_counts, counts_per_revolution,
                joint->counts_per_unit, &mcs[joint->status_slot])) {
            return 0;
        }
        command_counts = commanded[joint->status_slot] *
            joint->counts_per_unit + (double)base_counts;
        if (!rotary_phase_degrees(
                command_counts, counts_per_revolution,
                joint->counts_per_unit, &commanded[joint->status_slot])) {
            return 0;
        }
        ++rotary_records;
    }
    return rotary_records > 0;
}

int v5_position_status_build(
    const V5PositionSourceSnapshot *source,
    uint32_t writer_identity,
    uint32_t sequence,
    uint64_t source_acquired_mono_ns,
    uint64_t source_generation,
    V5NativePositionStatusBlock *block)
{
    double mcs[V5_POSITION_AXIS_COUNT];
    double commanded[V5_POSITION_AXIS_COUNT];
    int positions_valid;
    size_t axis;

    if (!source || !block || !writer_identity || !sequence ||
        (sequence & 1u) || !source_acquired_mono_ns || !source_generation ||
        !display_metadata_valid(&source->display)) {
        return 0;
    }
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        if (!isfinite(source->actual[axis]) ||
            !isfinite(source->commanded[axis]) ||
            !isfinite(source->following_error[axis])) return 0;
    }
    if (!isfinite(source->spindle_speed_rpm) ||
        !isfinite(source->linear_velocity_mm_per_min) ||
        !isfinite(source->feed_override_percent) ||
        !isfinite(source->spindle_override_percent)) {
        return 0;
    }
    positions_valid = project_positions(source, mcs, commanded);
    memset(block, 0, sizeof(*block));
    block->magic = V5_POSITION_MAGIC;
    block->version = V5_POSITION_VERSION;
    block->size = (uint32_t)sizeof(*block);
    block->axis_count = V5_POSITION_AXIS_COUNT;
    block->writer_identity = writer_identity;
    block->seq = sequence;
    block->source_acquired_mono_ns = source_acquired_mono_ns;
    block->source_generation = source_generation;
    block->valid_mask = V5_POSITION_VALID_SPINDLE_SPEED |
        V5_POSITION_VALID_LINEAR_VELOCITY |
        V5_POSITION_VALID_FEED_OVERRIDE |
        V5_POSITION_VALID_SPINDLE_OVERRIDE;
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        block->unit_per_count[axis] = source->display.unit_per_count[axis];
        block->display_digits[axis] = 3u;
    }
    if (positions_valid) {
        for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
            if (rotary_index(source->display.axis_code[axis]) >= 0) {
                mcs[axis] = rotary_display_projection(mcs[axis]);
                commanded[axis] = rotary_display_projection(commanded[axis]);
            } else {
                mcs[axis] = display_projection(mcs[axis]);
                commanded[axis] = display_projection(commanded[axis]);
            }
        }
        block->valid_mask |=
            V5_POSITION_VALID_MCS | V5_POSITION_VALID_CMD_MCS;
        for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
            block->mcs[axis] = mcs[axis];
            block->cmd_mcs[axis] = commanded[axis];
            block->following_error[axis] =
                display_projection(source->following_error[axis]);
        }
    }
    block->spindle_speed_rpm = source->spindle_speed_rpm;
    block->linear_velocity_mm_per_min =
        source->linear_velocity_mm_per_min;
    block->feedrate_override = source->feed_override_percent;
    block->spindle_override = source->spindle_override_percent;
    block->crc32 = v5_native_position_status_crc32(block);
    return 1;
}

int v5_position_status_display_equal(
    const V5NativePositionStatusBlock *left,
    const V5NativePositionStatusBlock *right)
{
    if (!left || !right || left->valid_mask != right->valid_mask) return 0;
    return memcmp(left->mcs, right->mcs, sizeof(left->mcs)) == 0 &&
        memcmp(left->cmd_mcs, right->cmd_mcs, sizeof(left->cmd_mcs)) == 0 &&
        memcmp(left->unit_per_count, right->unit_per_count,
            sizeof(left->unit_per_count)) == 0 &&
        memcmp(left->following_error, right->following_error,
            sizeof(left->following_error)) == 0 &&
        memcmp(left->display_digits, right->display_digits,
            sizeof(left->display_digits)) == 0 &&
        left->spindle_speed_rpm == right->spindle_speed_rpm &&
        left->linear_velocity_mm_per_min ==
            right->linear_velocity_mm_per_min &&
        left->feedrate_override == right->feedrate_override &&
        left->spindle_override == right->spindle_override;
}
