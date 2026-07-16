#ifndef V5_BUS_HOME_MOTION_H
#define V5_BUS_HOME_MOTION_H

static void publish(int j, int phase, int failure, int64_t error)
{
    *P->joint[j].phase = phase;
    *P->joint[j].failure = failure;
    *P->joint[j].error_counts = (double)error;
}

static void publish_sample(
    int j, int64_t actual, unsigned int generation, int motion_active)
{
    v5_joint_state *s = &S[j];
    s->sample_valid = 1;
    s->actual_counts = actual;
    *P->joint[j].start_counts = (double)s->start_counts;
    *P->joint[j].readback_counts = (double)actual;
    *P->joint[j].readback_generation = generation;
    *P->joint[j].readback_valid = 1;
    *P->joint[j].motion_active = motion_active ? 1 : 0;
}

static unsigned int active_stage_mask(void)
{
    unsigned int mask = 0;
    int j;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j)
        if ((run_mask & (1u << j)) && H[j].joint_in_sequence &&
            H[j].home_state != HOME_IDLE) mask |= 1u << j;
    return mask;
}

static unsigned int current_sequence_stage_mask(void)
{
    int sequences[V5_HOME_JOINTS] = {0};
    int j;
    unsigned int eligible = 0;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        sequences[j] = H[j].home_sequence;
        if (H[j].joint_in_sequence) eligible |= 1u << j;
    }
    return v5_home_sequence_stage_mask(
        run_mask & eligible, sequences, V5_HOME_JOINTS, current_sequence);
}

static int update_stage_start_barrier(int *failed_joint)
{
    unsigned int mask = current_sequence_stage_mask();
    unsigned int ready = 0;
    unsigned int started = 0;
    int j;
    if (failed_joint) *failed_joint = -1;
    if (mask != start_stage_mask || current_sequence != start_sequence) {
        start_stage_mask = mask;
        start_sequence = current_sequence;
        start_ready_mask = 0;
        start_release_mask = 0;
        for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
            if (!(mask & (1u << j))) continue;
            S[j].plan_sample_valid = 0;
            S[j].plan_sample_stable = 0;
            S[j].settle_timeout = 0;
        }
    }
    if (!mask) return V5_OK;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        if ((mask & (1u << j)) && S[j].phase != V5_IDLE)
            started |= 1u << j;
    }
    if (started) {
        if (started != mask) {
            for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
                if ((mask & (1u << j)) && !(started & (1u << j))) break;
            }
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_SEQUENCE_NOT_STARTED;
        }
        start_ready_mask = start_release_mask = mask;
        return V5_OK;
    }
    /* First pass: every same-sequence member is sampled before any member can
       receive a trajectory.  The second pass in do_homing() starts the group. */
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        v5_joint_state *s;
        int64_t current, base, runtime;
        unsigned int generation;
        if (!(mask & (1u << j))) continue;
        s = &S[j];
        if (H[j].home_state != HOME_START || joints[j].free_tp.active ||
            !GET_JOINT_INPOS_FLAG(&joints[j])) {
            s->plan_sample_valid = 0;
            s->plan_sample_stable = 0;
        } else if (!current_counts(
                j, &current, &base, &runtime, &generation)) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_COUNT_INPUT;
        } else {
            publish_sample(j, current, generation, 0);
            if (v5_home_start_sample_ready(
                    current, generation, V5_HOME_STABLE_CYCLES,
                    &s->plan_sample_counts, &s->plan_sample_generation,
                    &s->plan_sample_stable, &s->plan_sample_valid)) {
                ready |= 1u << j;
                s->settle_timeout = 0;
                continue;
            }
        }
        if (++s->settle_timeout > V5_HOME_WAIT_CYCLES) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_MOVING_MACHINE;
        }
    }
    start_ready_mask = ready;
    start_release_mask = v5_home_stage_release_ready(ready, mask) ? mask : 0;
    return V5_OK;
}

static void latch_failure(int failed_j, int failure)
{
    unsigned int mask;
    if (failure_latched) return;
    failure_latched = 1;
    mask = active_stage_mask();
    *P->failed_joint = failed_j >= 0 ? (hal_u32_t)failed_j : UINT32_MAX;
    *P->failure_code = failure;
    *P->failure_current_mask = mask ? mask : (run_mask & ~physical_complete_mask);
    if (failed_j >= 0 && failed_j < all_joints && failed_j < V5_HOME_JOINTS) {
        v5_joint_state *s = &S[failed_j];
        *P->failure_phase = *P->joint[failed_j].phase;
        *P->failure_readback_valid = s->sample_valid ? 1 : 0;
        *P->failure_actual_counts = s->sample_valid ? (double)s->actual_counts : 0.0;
        *P->failure_target_counts = (double)s->target_counts;
        *P->failure_error_counts = *P->joint[failed_j].error_counts;
        *P->failure_generation = s->wcp_generation;
        *P->failure_motion_active = joints[failed_j].free_tp.active ? 1 : 0;
    } else {
        *P->failure_phase = txn_phase == V5_TXN_RTCP ? V5_RTCP_WAIT :
            (txn_phase == V5_TXN_RUN ?
                (failure == V5_ZERO_DELTA_UNPROVEN ? V5_SETTLING : V5_MOVING) :
                V5_PRECHECK);
        *P->failure_readback_valid = 0;
        *P->failure_actual_counts = 0.0;
        *P->failure_target_counts = 0.0;
        *P->failure_error_counts = 0.0;
        *P->failure_generation = 0;
        *P->failure_motion_active = 0;
    }
}

static void publish_run(int phase, int failure)
{
    int j;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        if (!(run_mask & (1u << j))) continue;
        *P->joint[j].transaction = txn_id;
        publish(j, phase, failure, 0);
    }
}

static void reset_transaction_outputs(void)
{
    *P->transaction = txn_id;
    *P->run_active_mask = run_mask;
    *P->completed_mask = 0;
    *P->terminal_mask = 0;
    *P->failed_joint = UINT32_MAX;
    *P->failure_code = V5_OK;
    *P->failure_phase = V5_IDLE;
    *P->failure_current_mask = 0;
    *P->failure_actual_counts = 0.0;
    *P->failure_target_counts = 0.0;
    *P->failure_error_counts = 0.0;
    *P->failure_generation = 0;
    *P->failure_readback_valid = 0;
    *P->failure_motion_active = 0;
}

static void finish_transaction(void)
{
    txn_phase = V5_TXN_TERMINAL;
    homing_active = 0;
    *P->completed_mask = physical_complete_mask;
    *P->terminal_mask = run_mask;
    /* The request remains asserted: Home never restores the prior RTCP state. */
    context_started = 0;
}

static void fail_transaction(int failed_j, int failure, int cancelled)
{
    int j;
    if (txn_phase == V5_TXN_TERMINAL) return;
    latch_failure(failed_j, failure);
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        if (!(run_mask & (1u << j))) continue;
        if (context_started) {
            joints[j].free_tp.enable = 0;
            H[j].homing = 0;
            H[j].homed = 0;
            H[j].joint_in_sequence = 0;
            H[j].index_enable = 0;
            H[j].home_state = HOME_IDLE;
            S[j].phase = V5_IDLE;
        }
        publish(j, cancelled ? V5_CANCELLED : V5_FAILED, failure,
                j == failed_j ? (int64_t)*P->joint[j].error_counts : 0);
    }
    if (context_started) sequence_state = HOME_SEQUENCE_IDLE;
    physical_complete_mask = 0;
    finish_transaction();
}

static int v5_plan_target(int j, int64_t current, int64_t *target, int64_t *delta)
{
    int64_t counts_per_rev;
    unsigned int rotary;
    v5_config_snapshot *cfg = &C[j];
    if (!rotary_index(cfg->axis_code, &rotary)) {
        *target = 0;
        return v5_home_sub_i64(0, current, delta) ? V5_OK : V5_COUNT_INPUT;
    }
    if (!v5_home_exact_i64(fabs(cfg->counts_per_unit) * 360.0, &counts_per_rev) ||
        counts_per_rev <= 0) return V5_CONFIG;
    rotary = v5_home_rotary_target(current, 0, counts_per_rev, target, delta);
    return rotary ? V5_COUNT_INPUT : V5_OK;
}

static int v5_zero_cycle_excursion_target(
    int j, int64_t current, int64_t *target);

static int freeze_execution_plan(int *failed_joint, unsigned int *cycle_mask)
{
    int j;
    if (failed_joint) *failed_joint = -1;
    if (cycle_mask) *cycle_mask = 0;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        int failure;
        int64_t logical, base, runtime, target, delta;
        unsigned int generation;
        S[j].plan_valid = 0;
        if (!(run_mask & (1u << j))) continue;
        if (!current_counts(j, &logical, &base, &runtime, &generation)) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_COUNT_INPUT;
        }
        failure = v5_plan_target(j, logical, &target, &delta);
        if (failure) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return failure;
        }
        S[j].plan_start_counts = logical;
        S[j].plan_target_counts = target;
        S[j].plan_delta_counts = delta;
        S[j].plan_generation = generation;
        S[j].zero_cycle_required = delta == 0;
        S[j].zero_cycle_return_pending = 0;
        S[j].start_counts = logical;
        S[j].target_counts = target;
        S[j].wcp_generation = generation;
        publish_sample(j, logical, generation, 0);
        *P->joint[j].target_counts = (double)target;
        if (S[j].zero_cycle_required &&
            (failure = v5_zero_cycle_excursion_target(
                j, logical, &S[j].zero_cycle_excursion_target_counts)) != V5_OK) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return failure;
        }
        S[j].plan_valid = 1;
        S[j].plan_sample_valid = 0;
        S[j].plan_sample_stable = 0;
        S[j].settle_timeout = 0;
        /* Every active joint owns a real native cycle.  A joint already at
           its registered zero performs a bounded excursion and return. */
        if (cycle_mask) *cycle_mask |= 1u << j;
    }
    return V5_OK;
}

static int validate_frozen_execution_plan(int *failed_joint)
{
    int j;
    v5_home_set_side_effect_gate(0, &context_started);
    if (failed_joint) *failed_joint = -1;
    if (!*P->config_mapping_valid || !*P->router_mapping_valid ||
        *P->config_mapping_generation != run_generation ||
        *P->router_mapping_generation != run_generation ||
        *P->config_commit_seq != run_commit_seq ||
        *P->router_commit_seq != run_commit_seq ||
        *P->config_active_mask != mapping_mask ||
        *P->router_active_mask != mapping_mask || active_mask() != mapping_mask)
        return V5_MAPPING;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        int64_t logical, base, runtime;
        unsigned int generation;
        if (!(run_mask & (1u << j))) continue;
        if (!S[j].plan_valid ||
            !current_counts(j, &logical, &base, &runtime, &generation)) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_COUNT_INPUT;
        }
        publish_sample(j, logical, generation, joints[j].free_tp.active);
        S[j].wcp_generation = generation;
        if (!GET_JOINT_INPOS_FLAG(&joints[j]) || joints[j].free_tp.active) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_MOVING_MACHINE;
        }
        if (!v5_home_frozen_target_valid(
                S[j].plan_start_counts, S[j].plan_target_counts,
                S[j].plan_delta_counts)) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            return V5_PLAN_STALE;
        }
    }
    return v5_home_set_side_effect_gate(1, &context_started)
        ? V5_OK : V5_PLAN_STALE;
}

static int v5_target_allowed(emcmot_joint_t *joint, double target)
{
    return isfinite(target) &&
        !(joint->max_pos_limit > joint->min_pos_limit &&
          (target < joint->min_pos_limit || target > joint->max_pos_limit));
}

static int v5_zero_cycle_excursion_target(
    int j, int64_t current, int64_t *target)
{
    emcmot_joint_t *joint = &joints[j];
    double counts_per_unit = C[j].counts_per_unit;
    int64_t step_counts, positive_target, negative_target;
    int positive_allowed = 0, negative_allowed = 0;
    if (!v5_home_zero_cycle_step_counts(counts_per_unit, &step_counts))
        return V5_CONFIG;
    if (v5_home_add_i64(current, step_counts, &positive_target)) {
        positive_allowed = v5_target_allowed(
            joint, joint->pos_fb + (double)step_counts / counts_per_unit);
    }
    if (v5_home_sub_i64(current, step_counts, &negative_target)) {
        negative_allowed = v5_target_allowed(
            joint, joint->pos_fb - (double)step_counts / counts_per_unit);
    }
    return v5_home_zero_cycle_target(
        current, step_counts, positive_allowed, negative_allowed, target)
        ? V5_OK : V5_LIMIT;
}

static int v5_start_motion_leg(
    int j, int64_t current, int64_t runtime, int64_t target)
{
    v5_joint_state *s = &S[j];
    v5_config_snapshot *cfg = &C[j];
    emcmot_joint_t *joint = &joints[j];
    int64_t delta;
    double target_position;
    if (!v5_home_sub_i64(target, current, &delta)) return V5_COUNT_INPUT;
    s->phase = V5_MOVING;
    s->motion_start_runtime_counts = runtime;
    s->motion_target_counts = target;
    s->stable = s->settle_timeout = s->motion_cycles = 0;
    s->motion_required = delta != 0;
    s->motion_observed = s->last_counts_valid = 0;
    if (!s->motion_required) {
        s->motion_timeout = V5_HOME_WAIT_CYCLES;
        joint->free_tp.enable = 0;
        return V5_OK;
    }
    target_position = joint->pos_fb + (double)delta / cfg->counts_per_unit;
    if (!v5_target_allowed(joint, target_position) ||
        !isfinite(joint->vel_limit) || joint->vel_limit <= 0) return V5_LIMIT;
    s->motion_timeout = v5_home_motion_timeout_cycles(
        delta, cfg->counts_per_unit, joint->vel_limit,
        joint->acc_limit, servo_period_seconds);
    if (!s->motion_timeout) return V5_CONFIG;
    joint->free_tp.curr_pos = joint->pos_fb;
    joint->free_tp.pos_cmd = target_position;
    joint->free_tp.max_vel = joint->vel_limit;
    joint->free_tp.enable = 1;
    return V5_OK;
}

static int v5_start_joint(
    int j, int64_t current, int64_t runtime, unsigned int generation)
{
    v5_joint_pins *jp = &P->joint[j];
    v5_joint_state *s = &S[j];
    int64_t target, delta, motion_target;
    if (!s->plan_valid) return V5_COUNT_INPUT;
    target = s->plan_target_counts;
    if (!v5_home_frozen_target_valid(
            s->plan_start_counts, target, s->plan_delta_counts))
        return V5_PLAN_STALE;
    if (!v5_home_rebind_plan_start(
            target, current, &s->plan_start_counts, &delta))
        return V5_COUNT_INPUT;
    s->plan_delta_counts = delta;
    s->plan_generation = generation;
    s->phase = V5_MOVING;
    s->start_counts = current;
    s->target_counts = target;
    s->wcp_generation = generation;
    s->zero_cycle_return_pending = 0;
    s->transaction_start_runtime_counts = runtime;
    s->motion_observed = s->last_counts_valid = s->remap_pending = 0;
    *jp->start_counts = (double)current;
    *jp->target_counts = (double)target;
    publish_sample(j, current, generation, 0);
    publish(j, V5_MOVING, V5_OK, delta);
    motion_target = target;
    if (s->zero_cycle_required) {
        motion_target = s->zero_cycle_excursion_target_counts;
        s->zero_cycle_return_pending = 1;
    }
    return v5_start_motion_leg(j, current, runtime, motion_target);
}

static int v5_restart_after_rebase(
    int j, int64_t base, int64_t runtime, unsigned int generation)
{
    v5_joint_state *s = &S[j];
    int64_t logical_current, runtime_target, remaining, resume_target;
    resume_target = s->zero_cycle_return_pending
        ? s->motion_target_counts : s->target_counts;
    if (!v5_home_bind_runtime_actual(base, runtime, &logical_current) ||
        !v5_home_rebase_remaining(
            resume_target, base, runtime, &runtime_target, &remaining))
        return V5_COUNT_INPUT;
    (void)runtime_target;
    (void)remaining;
    s->wcp_generation = generation;
    v5_home_reset_generation_proof(
        &s->stable, &s->last_counts_valid,
        &s->sample_valid, &s->motion_observed);
    s->remap_pending = 0;
    *P->joint[j].readback_valid = 0;
    *P->joint[j].motion_active = 0;
    return v5_start_motion_leg(j, logical_current, runtime, resume_target);
}

static void v5_mark_joint_physical_complete(int j, int64_t final_error)
{
    emcmot_joint_t *joint = &joints[j];
    joint->free_tp.enable = 0;
    H[j].homing = 0;
    H[j].homed = 0;
    H[j].joint_in_sequence = 0;
    H[j].home_state = HOME_IDLE;
    S[j].phase = V5_IDLE;
    S[j].terminal_sample_valid = 0;
    physical_complete_mask |= 1u << j;
    terminal_stable = 0;
    txn_timeout = 0;
    publish(j, V5_SETTLING, V5_OK, final_error);
}

static int v5_joint_common_guard(int j)
{
    emcmot_joint_t *joint = &joints[j];
    if (!*P->safety_actual_valid) return V5_SAFETY_UNKNOWN;
    if (*P->estop_active) return V5_CANCEL;
    if (!*P->machine_enabled) return V5_DISABLED;
    if (!*P->router_mapping_valid || active_mask() != mapping_mask ||
        *P->router_mapping_generation != run_generation ||
        *P->config_mapping_generation != run_generation ||
        *P->router_commit_seq != run_commit_seq ||
        *P->config_commit_seq != run_commit_seq ||
        *P->router_active_mask != mapping_mask ||
        *P->config_active_mask != mapping_mask)
        return V5_MAPPING;
    if (!*P->rtcp_actual_valid) return V5_RTCP_UNKNOWN;
    if (!*P->rtcp_force_latched ||
        *P->rtcp_latched_transaction != txn_id || *P->rtcp_active >= 0.5)
        return V5_RTCP;
    if (joint->flag & (EMCMOT_JOINT_FAULT_BIT | EMCMOT_JOINT_ERROR_BIT))
        return V5_DRIVE;
    return V5_OK;
}

/* 0=collecting fresh stable terminal samples, 1=ready, -1=failed. */
static int v5_terminal_readback_ready(int *failed_joint, int *failure)
{
    int all_same = 1;
    int j;
    if (failed_joint) *failed_joint = -1;
    if (failure) *failure = V5_OK;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        v5_joint_state *s = &S[j];
        int guard;
        int64_t current, base, runtime, error;
        unsigned int generation;
        if (!(run_mask & (1u << j))) continue;
        guard = v5_joint_common_guard(j);
        if (guard ||
            !current_counts(j, &current, &base, &runtime, &generation) ||
            !v5_home_sub_i64(s->target_counts, current, &error)) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            if (failure) *failure = guard ? guard : V5_COUNT_INPUT;
            return -1;
        }
        publish_sample(j, current, generation, joints[j].free_tp.active);
        publish(j, V5_SETTLING, V5_OK, error);
        if (joints[j].free_tp.active || !GET_JOINT_INPOS_FLAG(&joints[j])) {
            terminal_stable = 0;
            s->terminal_sample_valid = 0;
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            if (failure) *failure = V5_MOTION_NOT_COMPLETE;
            return 0;
        }
        if (s->wcp_generation != generation) {
            v5_home_set_failed_joint(j, V5_HOME_JOINTS, failed_joint);
            if (failure) *failure = V5_PLAN_STALE;
            return -1;
        }
        if (!v5_home_terminal_sample_stable(
                0, 1, current, generation,
                &s->terminal_last_counts,
                &s->terminal_generation,
                &s->terminal_sample_valid)) {
            all_same = 0;
        }
    }
    terminal_stable = all_same
        ? (terminal_stable < UINT32_MAX ? terminal_stable + 1u : terminal_stable)
        : 1u;
    if (terminal_stable < V5_HOME_STABLE_CYCLES) return 0;
    return 1;
}

static void v5_commit_all_homed(void)
{
    int j;
    for (j = 0; j < all_joints && j < V5_HOME_JOINTS; ++j) {
        int64_t error = 0;
        if (!(run_mask & (1u << j))) continue;
        joints[j].free_tp.enable = 0;
        H[j].homing = 0;
        H[j].homed = 1;
        H[j].joint_in_sequence = 0;
        H[j].home_state = HOME_IDLE;
        S[j].phase = V5_IDLE;
        (void)v5_home_sub_i64(
            S[j].target_counts, S[j].actual_counts, &error);
        publish(j, V5_COMPLETE, V5_OK, error);
    }
    *P->completed_mask = run_mask;
}

static int v5_joint_machine(int j)
{
    emcmot_joint_t *joint = &joints[j];
    v5_joint_state *s = &S[j];
    int64_t current, base, runtime, error;
    unsigned int generation;
    int failure = v5_joint_common_guard(j);
    if (H[j].home_state == HOME_IDLE) return 0;
    if (H[j].home_state == HOME_ABORT) failure = V5_CANCEL;
    if (failure) {
        fail_transaction(j, failure, failure == V5_CANCEL);
        return 0;
    }
    if (H[j].home_state == HOME_START && s->phase == V5_IDLE) {
        H[j].homing = 1;
        H[j].homed = 0;
        if (!(start_release_mask & (1u << j))) return 1;
        if (!current_counts(j, &current, &base, &runtime, &generation)) {
            fail_transaction(j, V5_COUNT_INPUT, 0);
            return 0;
        }
        publish_sample(j, current, generation, 0);
        failure = v5_start_joint(j, current, runtime, generation);
        if (failure) fail_transaction(j, failure, 0);
        return failure ? 0 : 1;
    }
    if ((joint->on_pos_limit || joint->on_neg_limit) &&
        !(H[j].home_flags & HOME_IGNORE_LIMITS)) {
        fail_transaction(j, V5_LIMIT, 0);
        return 0;
    }
    if (!current_counts(j, &current, &base, &runtime, &generation) ||
        (s->wcp_generation && generation < s->wcp_generation) ||
        !v5_home_sub_i64(s->target_counts, current, &error)) {
        fail_transaction(j, V5_COUNT_INPUT, 0);
        return 0;
    }
    publish_sample(j, current, generation, joint->free_tp.active);
    if (runtime != s->transaction_start_runtime_counts)
        movement_mask |= 1u << j;
    publish(j, s->remap_pending ? V5_SETTLING : V5_MOVING, V5_OK, error);
    if (s->wcp_generation && generation > s->wcp_generation && !s->remap_pending) {
        joint->free_tp.enable = 0;
        s->remap_pending = 1;
    }
    if (joint->free_tp.active) {
        if (runtime != s->motion_start_runtime_counts) s->motion_observed = 1;
        if (++s->motion_cycles > s->motion_timeout)
            fail_transaction(j, V5_MOTION_NOT_COMPLETE, 0);
        return 1;
    }
    if (s->remap_pending) {
        if (!GET_JOINT_INPOS_FLAG(joint)) {
            if (++s->motion_cycles > s->motion_timeout)
                fail_transaction(j, V5_MOTION_NOT_COMPLETE, 0);
            return 1;
        }
        failure = v5_restart_after_rebase(j, base, runtime, generation);
        if (failure) fail_transaction(j, failure, 0);
        return failure ? 0 : 1;
    }
    if (s->zero_cycle_return_pending) {
        int64_t excursion_error;
        if (!v5_home_sub_i64(s->motion_target_counts, current, &excursion_error)) {
            fail_transaction(j, V5_COUNT_INPUT, 0);
            return 0;
        }
        s->phase = V5_SETTLING;
        publish(j, V5_SETTLING, V5_OK, excursion_error);
        if (v5_home_motion_sample_complete(
                s->motion_start_runtime_counts, runtime,
                current, 1,
                joint->free_tp.active, GET_JOINT_INPOS_FLAG(joint),
                V5_HOME_STABLE_CYCLES,
                &s->motion_observed, &s->stable,
                &s->last_counts, &s->last_counts_valid)) {
            movement_mask |= 1u << j;
            s->zero_cycle_return_pending = 0;
            failure = v5_start_motion_leg(
                j, current, runtime, s->target_counts);
            if (failure) fail_transaction(j, failure, 0);
            return failure ? 0 : 1;
        }
        if (++s->settle_timeout > V5_HOME_WAIT_CYCLES) {
            int timeout_reason = v5_home_motion_timeout_reason(
                1, s->motion_observed, joint->free_tp.active,
                GET_JOINT_INPOS_FLAG(joint));
            fail_transaction(j,
                timeout_reason == 1 ? V5_MOTION_PROOF : V5_MOTION_NOT_COMPLETE, 0);
            return 0;
        }
        return 1;
    }
    s->phase = V5_SETTLING;
    publish(j, V5_SETTLING, V5_OK, error);
    if (v5_home_motion_sample_complete(
            s->motion_start_runtime_counts, runtime,
            current, s->motion_required,
            joint->free_tp.active, GET_JOINT_INPOS_FLAG(joint),
            V5_HOME_STABLE_CYCLES,
            &s->motion_observed, &s->stable,
            &s->last_counts, &s->last_counts_valid)) {
        if (!(movement_mask & (1u << j))) {
            fail_transaction(j, V5_MOTION_PROOF, 0);
            return 0;
        }
        v5_mark_joint_physical_complete(j, error);
        return 0;
    }
    if (++s->settle_timeout > V5_HOME_WAIT_CYCLES) {
        int timeout_reason = v5_home_motion_timeout_reason(
            s->motion_required, s->motion_observed, joint->free_tp.active,
            GET_JOINT_INPOS_FLAG(joint));
        fail_transaction(j,
            timeout_reason == 1 ? V5_MOTION_PROOF : V5_MOTION_NOT_COMPLETE, 0);
        return 0;
    }
    return 1;
}

#endif
