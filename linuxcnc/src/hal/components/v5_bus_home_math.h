#ifndef V5_BUS_HOME_MATH_H
#define V5_BUS_HOME_MATH_H

#include <limits.h>
#include <stdint.h>

static int v5_home_exact_i64(double value, int64_t *result)
{
    double rounded;
    if (!result || !isfinite(value) || fabs(value) > 9007199254740991.0) return 0;
    rounded = value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5);
    if (fabs(value - rounded) > 1.0e-6 ||
        rounded < (double)INT64_MIN || rounded > (double)INT64_MAX) return 0;
    *result = (int64_t)rounded;
    return 1;
}

static int v5_home_sub_i64(int64_t lhs, int64_t rhs, int64_t *result)
{
    if (!result || (rhs < 0 && lhs > INT64_MAX + rhs) ||
        (rhs > 0 && lhs < INT64_MIN + rhs)) return 0;
    *result = lhs - rhs;
    return 1;
}

static int v5_home_add_i64(int64_t lhs, int64_t rhs, int64_t *result)
{
    if (!result || (rhs > 0 && lhs > INT64_MAX - rhs) ||
        (rhs < 0 && lhs < INT64_MIN - rhs)) return 0;
    *result = lhs + rhs;
    return 1;
}

static uint64_t v5_home_magnitude_i64(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

static int v5_home_sequence_valid(int sequence)
{
    return sequence >= -100 && sequence <= 100;
}

static int v5_home_sequence_disabled(int sequence)
{
    return sequence > 100;
}

static uint32_t v5_home_sequence_run_mask(
    uint32_t configured_mask, const int *sequences, unsigned int count)
{
    uint32_t mask = 0;
    unsigned int joint;
    if (!sequences || count > 32U) return 0;
    for (joint = 0; joint < count; ++joint) {
        if ((configured_mask & (1u << joint)) &&
            v5_home_sequence_valid(sequences[joint])) mask |= 1u << joint;
    }
    return mask;
}

static uint32_t v5_home_sequence_stage_mask(
    uint32_t run_mask, const int *sequences,
    unsigned int count, int current)
{
    uint32_t mask = 0;
    unsigned int joint;
    if (!sequences || count > 32U || current < 0) return 0;
    for (joint = 0; joint < count; ++joint) {
        int value;
        if (!(run_mask & (1u << joint)) ||
            !v5_home_sequence_valid(sequences[joint])) continue;
        value = sequences[joint] < 0 ? -sequences[joint] : sequences[joint];
        if (value == current) mask |= 1u << joint;
    }
    return mask;
}

static int v5_home_next_sequence(
    uint32_t run_mask, uint32_t completed_mask,
    const int *sequences, unsigned int count,
    int current, int *next)
{
    int candidate = 101;
    unsigned int joint;
    if (!sequences || !next || count > 32U) return 0;
    for (joint = 0; joint < count; ++joint) {
        int value;
        if (!(run_mask & (1u << joint)) ||
            (completed_mask & (1u << joint)) ||
            !v5_home_sequence_valid(sequences[joint])) continue;
        value = sequences[joint] < 0 ? -sequences[joint] : sequences[joint];
        if (value > current && value < candidate) candidate = value;
    }
    if (candidate > 100) return 0;
    *next = candidate;
    return 1;
}

/* Terminal commit is based on fresh stopped/in-position stability.  The
 * planner target remains diagnostic data and is deliberately not a second
 * completion gate after the native motion transaction has completed. */
static int v5_home_terminal_sample_stable(
    int motion_active, int in_position,
    int64_t actual, unsigned int generation,
    int64_t *last_actual, unsigned int *last_generation,
    int *sample_valid)
{
    if (!last_actual || !last_generation || !sample_valid) return 0;
    if (motion_active || !in_position) {
        *sample_valid = 0;
        return 0;
    }
    if (!*sample_valid || *last_actual != actual ||
        *last_generation != generation) {
        *last_actual = actual;
        *last_generation = generation;
        *sample_valid = 1;
        return 0;
    }
    return 1;
}

static int v5_home_joint_count_supported(int actual, int expected)
{
    return expected > 0 && actual == expected;
}

static int v5_home_expected_mask_complete(uint32_t expected, uint32_t completed)
{
    return expected != 0 && completed == expected;
}

static int v5_home_stage_mask_valid(uint32_t run_mask, uint32_t stage_mask)
{
    return stage_mask != 0 && (stage_mask & ~run_mask) == 0;
}

static int v5_home_transaction_motion_proven(
    uint32_t movement_mask, uint32_t expected_mask)
{
    return expected_mask != 0 && (movement_mask & expected_mask) == expected_mask;
}

static int v5_home_stage_release_ready(
    uint32_t ready_mask, uint32_t stage_mask)
{
    return stage_mask != 0 && (ready_mask & stage_mask) == stage_mask;
}

static int v5_home_atomic_commit_ready(
    uint32_t run_mask, uint32_t physical_complete_mask,
    uint32_t movement_mask, unsigned int stable,
    unsigned int stable_required)
{
    return stable_required != 0 && stable >= stable_required &&
        v5_home_expected_mask_complete(run_mask, physical_complete_mask) &&
        v5_home_transaction_motion_proven(movement_mask, run_mask);
}

static int v5_home_zero_cycle_step_counts(
    double counts_per_unit, int64_t *step_counts)
{
    return step_counts &&
        v5_home_exact_i64(fabs(counts_per_unit), step_counts) &&
        *step_counts > 0;
}

static int v5_home_zero_cycle_target(
    int64_t current, int64_t step_counts,
    int positive_allowed, int negative_allowed,
    int64_t *target)
{
    if (!target || step_counts <= 0) return 0;
    if (positive_allowed && v5_home_add_i64(current, step_counts, target)) return 1;
    if (negative_allowed && v5_home_sub_i64(current, step_counts, target)) return 1;
    return 0;
}

static int v5_home_precheck_sample(
    int moving, unsigned int stable_required, unsigned int *stable)
{
    if (!stable || !stable_required) return 0;
    if (moving) {
        *stable = 0;
        return 0;
    }
    if (*stable < UINT32_MAX) (*stable)++;
    return *stable >= stable_required;
}

static int v5_home_rtcp_ack_matches(
    uint32_t request_transaction, uint32_t latched_transaction,
    int actual_valid, int force_latched, double active)
{
    return request_transaction != 0 && actual_valid && force_latched &&
        request_transaction == latched_transaction && isfinite(active) && active < 0.5;
}

static int v5_home_frozen_target_valid(
    int64_t frozen_start, int64_t frozen_target, int64_t frozen_delta)
{
    int64_t expected_target;
    return v5_home_add_i64(frozen_start, frozen_delta, &expected_target) &&
        expected_target == frozen_target;
}

static int v5_home_start_sample_ready(
    int64_t actual, unsigned int generation, unsigned int stable_required,
    int64_t *sample_counts, unsigned int *sample_generation,
    unsigned int *stable, int *sample_valid)
{
    if (!stable_required || !sample_counts || !sample_generation ||
        !stable || !sample_valid) return 0;
    if (*sample_valid && *sample_counts == actual &&
        *sample_generation == generation) {
        if (*stable < UINT32_MAX) (*stable)++;
    } else {
        *sample_counts = actual;
        *sample_generation = generation;
        *stable = 1;
        *sample_valid = 1;
    }
    return *stable >= stable_required;
}

static int v5_home_rebind_plan_start(
    int64_t fixed_target, int64_t fresh_start,
    int64_t *start, int64_t *delta)
{
    if (!start || !delta ||
        !v5_home_sub_i64(fixed_target, fresh_start, delta)) return 0;
    *start = fresh_start;
    return 1;
}

static int v5_home_set_side_effect_gate(int plans_fresh, int *gate_open)
{
    if (!gate_open) return 0;
    *gate_open = plans_fresh ? 1 : 0;
    return *gate_open;
}

/* Before native preflight an E-stop is a rejected request; afterwards it cancels. */
static int v5_home_estop_is_cancel(int preflight_started)
{
    return preflight_started ? 1 : 0;
}

static int v5_home_rebase_remaining(
    int64_t logical_target, int64_t new_base, int64_t new_runtime,
    int64_t *runtime_target, int64_t *remaining)
{
    return v5_home_sub_i64(logical_target, new_base, runtime_target) &&
        v5_home_sub_i64(*runtime_target, new_runtime, remaining);
}

static int v5_home_bind_runtime_actual(
    int64_t base_counts, int64_t runtime_counts, int64_t *logical_counts)
{
    return v5_home_add_i64(base_counts, runtime_counts, logical_counts);
}

/* Build the absolute joint target from the fixed logical count target and the
 * current wcheckpoint base.  Do not add a count delta to joint->pos_fb: those
 * samples are acquired on different servo cycles and can bake their skew into
 * the final command even though the logical target itself is exact. */
static int v5_home_runtime_target_position(
    int64_t logical_current, int64_t runtime_current,
    int64_t logical_target, double counts_per_unit,
    int64_t *runtime_target, double *target_position)
{
    int64_t base_counts;
    int64_t roundtrip_counts;
    double position;
    if (!runtime_target || !target_position ||
        !isfinite(counts_per_unit) || counts_per_unit == 0.0 ||
        !v5_home_sub_i64(logical_current, runtime_current, &base_counts) ||
        !v5_home_sub_i64(logical_target, base_counts, runtime_target)) {
        return 0;
    }
    position = (double)*runtime_target / counts_per_unit;
    if (!isfinite(position) ||
        !v5_home_exact_i64(position * counts_per_unit, &roundtrip_counts) ||
        roundtrip_counts != *runtime_target) {
        return 0;
    }
    *target_position = position;
    return 1;
}

/* Completion comes only from observed motion, planner completion, in-position
 * and stable fresh actual samples. */
static int v5_home_motion_sample_complete(
    int64_t motion_start_runtime, int64_t current_runtime,
    int64_t actual,
    int motion_required, int motion_active, int in_position,
    unsigned int stable_required,
    int *motion_observed, unsigned int *stable,
    int64_t *last_actual, int *last_actual_valid)
{
    if (!motion_observed || !stable || !last_actual || !last_actual_valid ||
        !stable_required) return 0;
    if (current_runtime != motion_start_runtime) *motion_observed = 1;
    if ((motion_required && !*motion_observed) || motion_active || !in_position) {
        *stable = 0;
        *last_actual_valid = 0;
        return 0;
    }
    if (*last_actual_valid && actual == *last_actual) (*stable)++;
    else *stable = 1;
    *last_actual = actual;
    *last_actual_valid = 1;
    return *stable >= stable_required;
}

static int v5_home_set_failed_joint(int joint, int joint_count, int *failed_joint)
{
    if (!failed_joint || joint < 0 || joint >= joint_count) return 0;
    *failed_joint = joint;
    return 1;
}

static void v5_home_reset_generation_proof(
    unsigned int *stable, int *last_actual_valid,
    int *sample_valid, int *motion_observed)
{
    if (stable) *stable = 0;
    if (last_actual_valid) *last_actual_valid = 0;
    if (sample_valid) *sample_valid = 0;
    if (motion_observed) *motion_observed = 0;
}

/* 1=not started, 2=not complete/still. */
static int v5_home_motion_timeout_reason(
    int motion_required, int motion_observed, int motion_active,
    int in_position)
{
    if (motion_required && !motion_observed) return 1;
    (void)motion_active;
    (void)in_position;
    return 2;
}

static uint32_t v5_home_motion_timeout_cycles(
    int64_t delta_counts, double counts_per_unit, double max_velocity,
    double max_acceleration, double servo_period)
{
    double distance, seconds, cycles;
    if (!isfinite(counts_per_unit) || counts_per_unit == 0.0 ||
        !isfinite(max_velocity) || max_velocity <= 0.0 ||
        !isfinite(max_acceleration) || max_acceleration <= 0.0 ||
        !isfinite(servo_period) || servo_period <= 0.0) return 0;
    distance = (double)v5_home_magnitude_i64(delta_counts) / fabs(counts_per_unit);
    /* Conservative trapezoid bound plus one second for stop/in-position. */
    seconds = distance / max_velocity + (2.0 * max_velocity / max_acceleration) + 1.0;
    cycles = ceil(seconds / servo_period);
    if (!isfinite(cycles) || cycles < 1.0 || cycles > (double)UINT32_MAX) return 0;
    return (uint32_t)cycles;
}

/* Returns 0 on success and 1 on invalid/overflow.  The owner default for an
 * exact half-turn is prefer_negative; no floating-point tail may choose it. */
static int v5_home_rotary_target(
    int64_t current, int64_t zero, int64_t counts_per_rev,
    int64_t *target, int64_t *delta)
{
    int64_t phase;
    int64_t relative;
    if (counts_per_rev <= 0 || !target || !delta ||
        !v5_home_sub_i64(current, zero, &relative)) return 1;
    phase = relative % counts_per_rev;
    if (phase < 0) phase += counts_per_rev;
    *delta = phase <= counts_per_rev - phase ? -phase : counts_per_rev - phase;
    return v5_home_add_i64(current, *delta, target) ? 0 : 1;
}

#endif
