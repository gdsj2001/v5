#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "v5_bus_home_math.h"

static void test_motion_completion(void)
{
    int observed = 0, last_valid = 0;
    unsigned int stable = 0;
    int64_t last = 0;

    /* A matching actual while the planner is active must never complete. */
    assert(!v5_home_motion_sample_complete(
        0, 100, 100, 1, 1, 0, 3,
        &observed, &stable, &last, &last_valid));
    assert(observed == 1 && stable == 0);

    /* Completion requires three identical fresh stopped/in-position counts. */
    assert(!v5_home_motion_sample_complete(
        0, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(!v5_home_motion_sample_complete(
        0, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(v5_home_motion_sample_complete(
        0, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));

    /* Any changed count proves real motion. */
    observed = last_valid = 0;
    stable = 0;
    assert(!v5_home_motion_sample_complete(
        100, 101, 101, 1, 1, 0, 3,
        &observed, &stable, &last, &last_valid));
    assert(observed == 1);

    /* Non-zero delta cannot complete while actual never leaves start. */
    observed = last_valid = 0;
    stable = 0;
    assert(!v5_home_motion_sample_complete(
        100, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(!v5_home_motion_sample_complete(
        100, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(!v5_home_motion_sample_complete(
        100, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(observed == 0 && stable == 0);

    /* A zero-at-start axis closes only after a real excursion and return. */
    observed = last_valid = 0;
    stable = 0;
    assert(!v5_home_motion_sample_complete(
        100, 110, 110, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(!v5_home_motion_sample_complete(
        100, 110, 110, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(v5_home_motion_sample_complete(
        100, 110, 110, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    observed = last_valid = 0;
    stable = 0;
    assert(!v5_home_motion_sample_complete(
        110, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(!v5_home_motion_sample_complete(
        110, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(v5_home_motion_sample_complete(
        110, 100, 100, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));

    /* A logical/base shift without raw runtime movement is not motion proof. */
    observed = last_valid = 0;
    stable = 0;
    assert(!v5_home_motion_sample_complete(
        100, 100, 200, 1, 0, 1, 3,
        &observed, &stable, &last, &last_valid));
    assert(observed == 0);
}

static void test_timeout_and_rebase(void)
{
    int64_t runtime_target = 0, remaining = 0, rounded = 0, target = 0, delta = 0;
    int64_t logical = 0;
    unsigned int stable = 2;
    int last_valid = 1, sample_valid = 1, observed = 1;
    assert(v5_home_exact_i64(123.0, &rounded) && rounded == 123);
    assert(!v5_home_exact_i64(123.25, &rounded));
    assert(v5_home_motion_timeout_cycles(1000, 1000.0, 10.0, 100.0, 0.001) > 0);
    assert(v5_home_rotary_target(3599, 0, 3600, &target, &delta) == 0);
    assert(target == 3600 && delta == 1);
    assert(v5_home_rotary_target(1800, 0, 3600, &target, &delta) == 0);
    assert(target == 0 && delta == -1800); /* exact tie: prefer_negative */
    assert(v5_home_rotary_target(1800, 0, 3601, &target, &delta) == 0);
    assert(target == 0 && delta == -1800); /* odd period creates no half count */
    assert(v5_home_motion_timeout_reason(1, 0, 0, 1) == 1);
    assert(v5_home_motion_timeout_reason(1, 1, 1, 0) == 2);
    assert(v5_home_motion_timeout_reason(1, 1, 0, 1) == 2);
    assert(v5_home_rebase_remaining(1000, 250, 700, &runtime_target, &remaining));
    assert(runtime_target == 750 && remaining == 50);
    assert(runtime_target + 250 == 1000); /* fixed logical target unchanged */
    /* Rebase during a zero-cycle outbound leg keeps its fixed outbound target. */
    assert(v5_home_rebase_remaining(1100, 250, 800, &runtime_target, &remaining));
    assert(runtime_target == 850 && remaining == 50);
    /* Stale wcheckpoint logical=0 must not replace fresh raw runtime actual. */
    assert(v5_home_bind_runtime_actual(250, -100, &logical));
    assert(logical == 150);
    assert(v5_home_rotary_target(logical, 0, 3600, &target, &delta) == 0);
    assert(target == 0 && delta == -150);
    v5_home_reset_generation_proof(
        &stable, &last_valid, &sample_valid, &observed);
    assert(stable == 0 && last_valid == 0 && sample_valid == 0 && observed == 0);
}

static void test_transaction_contract(void)
{
    int failed_joint = -1;
    int next_sequence = -1;
    const int first_order[5] = {2, 0, 1, 1, 3};
    const int changed_order[5] = {0, 3, 2, 1, 1};
    const int with_disabled[5] = {2, 0, 999, -1, 3};
    unsigned int precheck_stable = 2;
    int coordinate_mutations = 0;
    int64_t plan_sample = 0, rebound_start = 0, rebound_delta = 0;
    unsigned int plan_sample_generation = 0, plan_sample_stable = 0;
    int plan_sample_valid = 0;
    assert(v5_home_sequence_valid(-100));
    assert(v5_home_sequence_valid(0));
    assert(v5_home_sequence_valid(100));
    assert(!v5_home_sequence_valid(-101));
    assert(!v5_home_sequence_valid(101));
    assert(v5_home_sequence_disabled(999));
    assert(!v5_home_sequence_disabled(-1));
    assert(!v5_home_joint_count_supported(4, 5));
    assert(v5_home_joint_count_supported(5, 5));
    assert(!v5_home_joint_count_supported(6, 5));

    /* Stages are derived from native values, including same-value groups. */
    assert(v5_home_sequence_run_mask(0x1fU, first_order, 5) == 0x1fU);
    assert(v5_home_sequence_stage_mask(0x1fU, first_order, 5, 0) == (1U << 1));
    assert(v5_home_sequence_stage_mask(0x1fU, first_order, 5, 1) ==
           ((1U << 2) | (1U << 3)));
    assert(v5_home_sequence_stage_mask(0x1fU, first_order, 5, 2) == (1U << 0));
    assert(v5_home_sequence_stage_mask(0x1fU, changed_order, 5, 0) == (1U << 0));
    assert(v5_home_sequence_stage_mask(0x1fU, changed_order, 5, 1) ==
           ((1U << 3) | (1U << 4)));
    assert(v5_home_sequence_run_mask(0x1fU, with_disabled, 5) == 0x1bU);
    assert(v5_home_sequence_stage_mask(0x1bU, with_disabled, 5, 1) == (1U << 3));
    assert(v5_home_next_sequence(
        0x1fU, 1U << 1, first_order, 5, 0, &next_sequence));
    assert(next_sequence == 1);
    assert(v5_home_next_sequence(
        0x1fU, (1U << 1) | (1U << 2) | (1U << 3),
        first_order, 5, 1, &next_sequence));
    assert(next_sequence == 2);
    assert(v5_home_stage_mask_valid(0x1fU, (1U << 2) | (1U << 3)));
    assert(!v5_home_stage_mask_valid(0x1fU, 0));
    assert(!v5_home_stage_mask_valid(0x1fU, 1U << 5));
    assert(!v5_home_stage_release_ready(1U << 0, (1U << 0) | (1U << 1)));
    assert(v5_home_stage_release_ready(
        (1U << 0) | (1U << 1), (1U << 0) | (1U << 1)));

    {
        int64_t terminal_last = 0;
        int64_t diagnostic_target = 0;
        unsigned int terminal_generation = 0;
        int terminal_valid = 0;

        /* A stopped fresh readback one count away from the planner target is
         * stable terminal evidence; target/error is diagnostic only. */
        assert(!v5_home_terminal_sample_stable(
            0, 1, -1, 7, &terminal_last, &terminal_generation,
            &terminal_valid));
        assert(v5_home_terminal_sample_stable(
            0, 1, -1, 7, &terminal_last, &terminal_generation,
            &terminal_valid));
        assert(terminal_last != diagnostic_target);

        /* Motion-active or not-in-position samples cannot commit. */
        assert(!v5_home_terminal_sample_stable(
            1, 0, -1, 7, &terminal_last, &terminal_generation,
            &terminal_valid));
        assert(!terminal_valid);
    }

    assert(!v5_home_expected_mask_complete(0, 0));
    assert(!v5_home_expected_mask_complete(0x1fU, 0x0fU));
    assert(v5_home_expected_mask_complete(0x1fU, 0x1fU));

    /* Every active axis must prove motion in this transaction. */
    assert(v5_home_transaction_motion_proven(0x1fU, 0x1fU));
    assert(!v5_home_transaction_motion_proven(1U << 2, 0x1fU));
    assert(!v5_home_transaction_motion_proven(0, 0x1fU));
    if (v5_home_transaction_motion_proven(0, 0x1fU)) coordinate_mutations++;
    assert(coordinate_mutations == 0);
    assert(!v5_home_atomic_commit_ready(0x1fU, 0x1fU, 0x0fU, 3, 3));
    assert(!v5_home_atomic_commit_ready(0x1fU, 0x0fU, 0x1fU, 3, 3));
    assert(!v5_home_atomic_commit_ready(0x1fU, 0x1fU, 0x1fU, 2, 3));
    assert(v5_home_atomic_commit_ready(0x1fU, 0x1fU, 0x1fU, 3, 3));

    {
        int64_t step = 0, excursion = 0;
        assert(v5_home_zero_cycle_step_counts(1000.0, &step) && step == 1000);
        assert(v5_home_zero_cycle_target(0, step, 1, 1, &excursion));
        assert(excursion == 1000);
        assert(v5_home_zero_cycle_target(0, step, 0, 1, &excursion));
        assert(excursion == -1000);
        assert(!v5_home_zero_cycle_target(0, step, 0, 0, &excursion));
    }

    assert(!v5_home_precheck_sample(1, 3, &precheck_stable));
    assert(precheck_stable == 0);
    assert(!v5_home_precheck_sample(0, 3, &precheck_stable));
    assert(!v5_home_precheck_sample(0, 3, &precheck_stable));
    assert(v5_home_precheck_sample(0, 3, &precheck_stable));

    assert(!v5_home_rtcp_ack_matches(7, 6, 1, 1, 0.0));
    assert(!v5_home_rtcp_ack_matches(7, 7, 1, 1, 1.0));
    assert(v5_home_rtcp_ack_matches(7, 7, 1, 1, 0.0));

    /* The post-RTCP plan fixes the target and its integer identity. */
    coordinate_mutations = 0;
    assert(v5_home_frozen_target_valid(0, 100, 100));
    assert(!v5_home_frozen_target_valid(0, 100, 99));
    assert(!v5_home_set_side_effect_gate(
        v5_home_frozen_target_valid(0, 100, 99), &coordinate_mutations));
    assert(coordinate_mutations == 0); /* no H/free_tp/coordinate side effects */
    assert(v5_home_set_side_effect_gate(
        v5_home_frozen_target_valid(0, 100, 100), &coordinate_mutations));
    assert(coordinate_mutations == 1);

    /* A one-count stationary sample change is stabilized, not called stale. */
    assert(!v5_home_start_sample_ready(
        -10799994, 4, 3, &plan_sample, &plan_sample_generation,
        &plan_sample_stable, &plan_sample_valid));
    assert(!v5_home_start_sample_ready(
        -10799993, 4, 3, &plan_sample, &plan_sample_generation,
        &plan_sample_stable, &plan_sample_valid));
    assert(!v5_home_start_sample_ready(
        -10799993, 4, 3, &plan_sample, &plan_sample_generation,
        &plan_sample_stable, &plan_sample_valid));
    assert(v5_home_start_sample_ready(
        -10799993, 4, 3, &plan_sample, &plan_sample_generation,
        &plan_sample_stable, &plan_sample_valid));
    assert(v5_home_rebind_plan_start(
        -10799976, plan_sample, &rebound_start, &rebound_delta));
    assert(rebound_start == -10799993 && rebound_delta == 17);
    assert(v5_home_frozen_target_valid(
        rebound_start, -10799976, rebound_delta));
    assert(!v5_home_start_sample_ready(
        -10799993, 5, 3, &plan_sample, &plan_sample_generation,
        &plan_sample_stable, &plan_sample_valid));
    assert(plan_sample_generation == 5 && plan_sample_stable == 1);

    assert(!v5_home_estop_is_cancel(0)); /* initial E-stop: FAILED */
    assert(v5_home_estop_is_cancel(1));  /* in-flight E-stop: CANCELLED */
    assert(v5_home_set_failed_joint(3, 5, &failed_joint));
    assert(failed_joint == 3);
    assert(!v5_home_set_failed_joint(5, 5, &failed_joint));
}

int main(void)
{
    test_motion_completion();
    test_timeout_and_rebase();
    test_transaction_contract();
    puts("V5_BUS_HOME_STATE_SMOKE_OK");
    return 0;
}
