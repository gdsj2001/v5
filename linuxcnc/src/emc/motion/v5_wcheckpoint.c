#include "v5_wcheckpoint.h"

#include <float.h>
#include <stdint.h>

#include "cubic.h"
#include "hal.h"
#include "mot_priv.h"
#include "homing.h"
#include "rtapi_math.h"

#define V5_FULL_TURN_DEG 360.0
#define V5_U32_BASE 4294967296.0
#define V5_PERIODICITY_TOLERANCE 1.0e-7

extern int v5_wrapped_rotary_mask;
extern int v5_wcheckpoint_command_raw_bits[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_feedback_raw_bits[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_command_raw_signed[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_feedback_raw_signed[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_command_modulus_hi[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_command_modulus_lo[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_feedback_modulus_hi[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_feedback_modulus_lo[V5_WCHECKPOINT_ROTARY_AXES];
extern long v5_wcheckpoint_counts_per_rev[V5_WCHECKPOINT_ROTARY_AXES];
extern long v5_wcheckpoint_reducer_num[V5_WCHECKPOINT_ROTARY_AXES];
extern long v5_wcheckpoint_reducer_den[V5_WCHECKPOINT_ROTARY_AXES];
extern int v5_wcheckpoint_drive_periodic[V5_WCHECKPOINT_ROTARY_AXES];

typedef struct {
    double base_turns;
    double logical_counts;
    double base_counts;
    double runtime_counts;
    double safe_half_counts;
    unsigned int generation;
    unsigned int sample_sequence;
    unsigned int raw_bits;
    unsigned int router_synced;
    unsigned int valid;
    enum v5_wcheckpoint_reason reason;
} v5_wcheckpoint_axis_state_t;

static v5_wcheckpoint_axis_state_t axis_state[V5_WCHECKPOINT_ROTARY_AXES];

static int pose_mask(unsigned int index)
{
    return 1 << (index + 3U);
}

static double pose_rotary(const EmcPose *pose, unsigned int index)
{
    return index == 0U ? pose->a : (index == 1U ? pose->b : pose->c);
}

static void set_pose_rotary(EmcPose *pose, unsigned int index, double value)
{
    if (index == 0U) {
        pose->a = value;
    } else if (index == 1U) {
        pose->b = value;
    } else {
        pose->c = value;
    }
}

static unsigned long gcd_unsigned(unsigned long lhs, unsigned long rhs)
{
    while (rhs != 0U) {
        unsigned long remainder = lhs % rhs;
        lhs = rhs;
        rhs = remainder;
    }
    return lhs;
}

static double mapped_modulus(int hi, int lo)
{
    return ((double)(uint32_t)hi * V5_U32_BASE) + (double)(uint32_t)lo;
}

static enum v5_wcheckpoint_reason profile_reason(
    unsigned int index,
    double *safe_half_counts,
    unsigned long *turn_quantum)
{
    int command_bits = v5_wcheckpoint_command_raw_bits[index];
    int feedback_bits = v5_wcheckpoint_feedback_raw_bits[index];
    double command_modulus;
    double feedback_modulus;
    double effective_modulus;
    unsigned long numerator;
    unsigned long denominator;
    unsigned long divisor;

    if (!(v5_wrapped_rotary_mask & pose_mask(index))) {
        return V5_WCHECKPOINT_DISABLED;
    }
    if (command_bits < 2 || feedback_bits < 2) {
        return V5_WCHECKPOINT_RAW_WIDTH_MISSING;
    }
    if (!v5_wcheckpoint_command_raw_signed[index] ||
        !v5_wcheckpoint_feedback_raw_signed[index]) {
        return V5_WCHECKPOINT_RAW_TYPE_UNSUPPORTED;
    }
    command_modulus = mapped_modulus(
        v5_wcheckpoint_command_modulus_hi[index],
        v5_wcheckpoint_command_modulus_lo[index]);
    feedback_modulus = mapped_modulus(
        v5_wcheckpoint_feedback_modulus_hi[index],
        v5_wcheckpoint_feedback_modulus_lo[index]);
    if (!(command_modulus > 0.0) || !(feedback_modulus > 0.0)) {
        return V5_WCHECKPOINT_RAW_MODULUS_MISSING;
    }
    if (command_modulus > ldexp(1.0, command_bits) ||
        feedback_modulus > ldexp(1.0, feedback_bits)) {
        return V5_WCHECKPOINT_RAW_MODULUS_MISSING;
    }
    if (command_bits > DBL_MANT_DIG || feedback_bits > DBL_MANT_DIG) {
        return V5_WCHECKPOINT_LOGICAL_STORAGE_INSUFFICIENT;
    }
    if (v5_wcheckpoint_counts_per_rev[index] <= 0) {
        return V5_WCHECKPOINT_CREV_MISSING;
    }
    if (v5_wcheckpoint_reducer_num[index] <= 0 ||
        v5_wcheckpoint_reducer_den[index] <= 0) {
        return V5_WCHECKPOINT_REDUCER_RATIO_MISSING;
    }
    effective_modulus = command_modulus < feedback_modulus
        ? command_modulus : feedback_modulus;
    *safe_half_counts = effective_modulus / 4.0;
    numerator = (unsigned long)v5_wcheckpoint_reducer_num[index];
    denominator = (unsigned long)v5_wcheckpoint_reducer_den[index];
    divisor = gcd_unsigned(numerator, denominator);
    *turn_quantum = denominator / divisor;
    if (!v5_wcheckpoint_drive_periodic[index]) {
        return V5_WCHECKPOINT_DRIVE_WINDOW_UNPROVEN;
    }
    return V5_WCHECKPOINT_OK;
}

static int snapshot_truth_available(enum v5_wcheckpoint_reason reason)
{
    return reason == V5_WCHECKPOINT_OK ||
        reason == V5_WCHECKPOINT_DRIVE_WINDOW_UNPROVEN;
}

static void logical_to_runtime_pose(const EmcPose *logical, EmcPose *runtime)
{
    unsigned int index;

    *runtime = *logical;
    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        if (v5_wrapped_rotary_mask & pose_mask(index)) {
            set_pose_rotary(
                runtime,
                index,
                pose_rotary(logical, index) -
                    (axis_state[index].base_turns * V5_FULL_TURN_DEG));
        }
    }
}

static void runtime_to_logical_pose(EmcPose *pose)
{
    unsigned int index;

    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        if (v5_wrapped_rotary_mask & pose_mask(index)) {
            set_pose_rotary(
                pose,
                index,
                pose_rotary(pose, index) +
                    (axis_state[index].base_turns * V5_FULL_TURN_DEG));
        }
    }
}

static int pose_matches(const EmcPose *lhs, const EmcPose *rhs)
{
    return isfinite(lhs->tran.x) && isfinite(rhs->tran.x) &&
        isfinite(lhs->tran.y) && isfinite(rhs->tran.y) &&
        isfinite(lhs->tran.z) && isfinite(rhs->tran.z) &&
        isfinite(lhs->a) && isfinite(rhs->a) &&
        isfinite(lhs->b) && isfinite(rhs->b) &&
        isfinite(lhs->c) && isfinite(rhs->c) &&
        isfinite(lhs->u) && isfinite(rhs->u) &&
        isfinite(lhs->v) && isfinite(rhs->v) &&
        isfinite(lhs->w) && isfinite(rhs->w) &&
        fabs(lhs->tran.x - rhs->tran.x) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->tran.y - rhs->tran.y) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->tran.z - rhs->tran.z) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->a - rhs->a) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->b - rhs->b) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->c - rhs->c) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->u - rhs->u) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->v - rhs->v) <= V5_PERIODICITY_TOLERANCE &&
        fabs(lhs->w - rhs->w) <= V5_PERIODICITY_TOLERANCE;
}

static int apply_joint_shift(const EmcPose *logical, const double new_base_turns[3])
{
    EmcPose old_runtime;
    EmcPose new_runtime;
    EmcPose reconstructed;
    double old_joint[EMCMOT_MAX_JOINTS] = {0.0};
    double new_joint[EMCMOT_MAX_JOINTS] = {0.0};
    KINEMATICS_FORWARD_FLAGS local_fflags = fflags;
    KINEMATICS_INVERSE_FLAGS local_iflags = iflags;
    unsigned int index;
    int joint_num;

    logical_to_runtime_pose(logical, &old_runtime);
    new_runtime = *logical;
    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        if (v5_wrapped_rotary_mask & pose_mask(index)) {
            set_pose_rotary(
                &new_runtime,
                index,
                pose_rotary(logical, index) -
                    (new_base_turns[index] * V5_FULL_TURN_DEG));
        }
    }
    if (kinematicsInverse(&old_runtime, old_joint, &local_iflags, &local_fflags) != 0 ||
        kinematicsInverse(&new_runtime, new_joint, &local_iflags, &local_fflags) != 0) {
        return V5_WCHECKPOINT_KINEMATICS_FAILED;
    }
    reconstructed = new_runtime;
    if (kinematicsForward(new_joint, &reconstructed, &local_fflags, &local_iflags) != 0) {
        return V5_WCHECKPOINT_KINEMATICS_FAILED;
    }
    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        if (v5_wrapped_rotary_mask & pose_mask(index)) {
            set_pose_rotary(
                &reconstructed,
                index,
                pose_rotary(&reconstructed, index) +
                    (new_base_turns[index] * V5_FULL_TURN_DEG));
        }
    }
    if (!pose_matches(logical, &reconstructed)) {
        return V5_WCHECKPOINT_PERIODICITY_FAILED;
    }

    for (joint_num = 0; joint_num < emcmotConfig->numJoints; ++joint_num) {
        emcmot_joint_t *joint = &joints[joint_num];
        double shift = new_joint[joint_num] - old_joint[joint_num];
        if (!isfinite(shift)) {
            return V5_WCHECKPOINT_KINEMATICS_FAILED;
        }
        joint->pos_cmd += shift;
        joint->pos_fb += shift;
        joint->coarse_pos += shift;
        joint->free_tp.curr_pos += shift;
        joint->free_tp.pos_cmd += shift;
        cubicOffset(&joint->cubic, shift);
    }
    return V5_WCHECKPOINT_OK;
}

int v5_wcheckpoint_export_hal(int component_id)
{
    static const char axis_name[V5_WCHECKPOINT_ROTARY_AXES] = {'a', 'b', 'c'};
    unsigned int index;
    int result;

    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        v5_wcheckpoint_hal_t *hal_axis = &emcmot_hal_data->v5_wcheckpoint[index];
        result = hal_pin_float_newf(HAL_IN, &hal_axis->router_base_counts,
                                    component_id, "motion.v5-wcheckpoint-%c-router-base-counts", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_s32_newf(HAL_IN, &hal_axis->router_runtime_counts,
                                  component_id, "motion.v5-wcheckpoint-%c-router-runtime-counts", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_u32_newf(HAL_IN, &hal_axis->router_generation,
                                  component_id, "motion.v5-wcheckpoint-%c-router-generation", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_bit_newf(HAL_IN, &hal_axis->router_valid,
                                  component_id, "motion.v5-wcheckpoint-%c-router-valid", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_float_newf(HAL_OUT, &hal_axis->logical_abs_counts,
                                    component_id, "motion.v5-wcheckpoint-%c-logical-counts", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_float_newf(HAL_OUT, &hal_axis->base_counts,
                                    component_id, "motion.v5-wcheckpoint-%c-base-counts", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_float_newf(HAL_OUT, &hal_axis->runtime_window_counts,
                                    component_id, "motion.v5-wcheckpoint-%c-runtime-counts", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_u32_newf(HAL_OUT, &hal_axis->generation,
                                  component_id, "motion.v5-wcheckpoint-%c-generation", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_u32_newf(HAL_OUT, &hal_axis->raw_count_bits,
                                  component_id, "motion.v5-wcheckpoint-%c-raw-count-bits", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_u32_newf(HAL_OUT, &hal_axis->logical_storage_bits,
                                  component_id, "motion.v5-wcheckpoint-%c-logical-storage-bits", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_s32_newf(HAL_OUT, &hal_axis->invalid_reason,
                                  component_id, "motion.v5-wcheckpoint-%c-invalid-reason", axis_name[index]);
        if (result != 0) return result;
        result = hal_pin_bit_newf(HAL_OUT, &hal_axis->valid,
                                  component_id, "motion.v5-wcheckpoint-%c-valid", axis_name[index]);
        if (result != 0) return result;
    }
    return 0;
}

void v5_wcheckpoint_reset(void)
{
    unsigned int index;
    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        axis_state[index].base_turns = 0.0;
        axis_state[index].generation++;
        axis_state[index].router_synced = 0U;
        axis_state[index].valid = 0U;
        axis_state[index].reason = V5_WCHECKPOINT_DISABLED;
    }
}

void v5_wcheckpoint_update_before_inputs(void)
{
    double new_base_turns[V5_WCHECKPOINT_ROTARY_AXES];
    unsigned long turn_quantum[V5_WCHECKPOINT_ROTARY_AXES] = {1U, 1U, 1U};
    unsigned int router_generation[V5_WCHECKPOINT_ROTARY_AXES] = {0U, 0U, 0U};
    unsigned char router_candidate[V5_WCHECKPOINT_ROTARY_AXES] = {0U, 0U, 0U};
    int local_shift_needed = 0;
    int router_shift_needed = 0;
    unsigned int index;

    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        double logical_deg = pose_rotary(&emcmotStatus->carte_pos_cmd, index);
        double runtime_deg;
        double runtime_counts;
        enum v5_wcheckpoint_reason reason;
        v5_wcheckpoint_hal_t *hal_axis = &emcmot_hal_data->v5_wcheckpoint[index];
        new_base_turns[index] = axis_state[index].base_turns;
        reason = profile_reason(index, &axis_state[index].safe_half_counts,
                                &turn_quantum[index]);
        axis_state[index].raw_bits = (unsigned int)(
            v5_wcheckpoint_command_raw_bits[index] < v5_wcheckpoint_feedback_raw_bits[index]
                ? v5_wcheckpoint_command_raw_bits[index]
                : v5_wcheckpoint_feedback_raw_bits[index]);
        if (*hal_axis->router_valid && *hal_axis->router_generation != 0U &&
            (reason == V5_WCHECKPOINT_OK ||
             reason == V5_WCHECKPOINT_DRIVE_WINDOW_UNPROVEN)) {
            double router_base_counts = *hal_axis->router_base_counts;
            double quantum_counts =
                (double)v5_wcheckpoint_counts_per_rev[index] *
                (double)turn_quantum[index];
            if (isfinite(router_base_counts) &&
                fabs(router_base_counts) < ldexp(1.0, DBL_MANT_DIG) &&
                floor(fabs(router_base_counts)) == fabs(router_base_counts) &&
                quantum_counts > 0.0 &&
                fmod(fabs(router_base_counts), quantum_counts) == 0.0) {
                new_base_turns[index] = router_base_counts /
                    (double)v5_wcheckpoint_counts_per_rev[index];
                router_generation[index] = *hal_axis->router_generation;
                router_candidate[index] = 1U;
                if (new_base_turns[index] != axis_state[index].base_turns) {
                    router_shift_needed = 1;
                }
                continue;
            }
        }
        axis_state[index].router_synced = 0U;
        axis_state[index].reason = reason;
        /* A drive without proven periodic command/feedback support cannot
         * rebase, but its current logical/base/runtime window is still valid
         * and may be used for bounded motion inside the mapped safe range. */
        axis_state[index].valid = snapshot_truth_available(reason);
        if (reason != V5_WCHECKPOINT_OK || !isfinite(logical_deg)) {
            continue;
        }
        runtime_deg = logical_deg -
            (axis_state[index].base_turns * V5_FULL_TURN_DEG);
        runtime_counts = runtime_deg *
            ((double)v5_wcheckpoint_counts_per_rev[index] / V5_FULL_TURN_DEG);
        if (fabs(runtime_counts) >= (axis_state[index].safe_half_counts / 2.0)) {
            double quantum = (double)turn_quantum[index];
            double logical_turns = logical_deg / V5_FULL_TURN_DEG;
            new_base_turns[index] = floor((logical_turns / quantum) + 0.5) * quantum;
            if (new_base_turns[index] != axis_state[index].base_turns) {
                local_shift_needed = 1;
            }
        }
    }

    if (local_shift_needed && (!get_allhomed() || get_homing_is_active())) {
        for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
            if (!router_candidate[index]) {
                new_base_turns[index] = axis_state[index].base_turns;
            }
        }
        local_shift_needed = 0;
    }

    if (router_shift_needed || local_shift_needed) {
        int result = apply_joint_shift(&emcmotStatus->carte_pos_cmd, new_base_turns);
        if (result == V5_WCHECKPOINT_OK) {
            for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
                if (new_base_turns[index] != axis_state[index].base_turns) {
                    axis_state[index].base_turns = new_base_turns[index];
                    if (router_candidate[index]) {
                        axis_state[index].generation = router_generation[index];
                    } else {
                        axis_state[index].generation++;
                    }
                }
            }
        } else {
            for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
                if (new_base_turns[index] != axis_state[index].base_turns) {
                    axis_state[index].valid = 0U;
                    axis_state[index].reason = (enum v5_wcheckpoint_reason)result;
                    axis_state[index].router_synced = 0U;
                }
            }
        }
    }

    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        if (!router_candidate[index]) {
            continue;
        }
        if (new_base_turns[index] != axis_state[index].base_turns) {
            continue;
        }
        axis_state[index].generation = router_generation[index];
        axis_state[index].reason = V5_WCHECKPOINT_OK;
        axis_state[index].router_synced = 1U;
        axis_state[index].valid = 1U;
    }
}

void v5_wcheckpoint_publish(void)
{
    unsigned int index;
    for (index = 0U; index < V5_WCHECKPOINT_ROTARY_AXES; ++index) {
        v5_wcheckpoint_axis_state_t *state = &axis_state[index];
        v5_wcheckpoint_hal_t *hal_axis = &emcmot_hal_data->v5_wcheckpoint[index];
        double logical_deg = pose_rotary(&emcmotStatus->carte_pos_fb, index);
        double counts_per_degree = v5_wcheckpoint_counts_per_rev[index] > 0
            ? (double)v5_wcheckpoint_counts_per_rev[index] / V5_FULL_TURN_DEG : 0.0;
        if (!isfinite(logical_deg)) {
            logical_deg = 0.0;
            state->valid = 0U;
            state->reason = V5_WCHECKPOINT_KINEMATICS_FAILED;
        }
        if (state->valid && state->generation == 0U) {
            state->generation = 1U;
        }
        if (state->router_synced && *hal_axis->router_valid) {
            state->base_counts = *hal_axis->router_base_counts;
            state->runtime_counts = (double)*hal_axis->router_runtime_counts;
            state->logical_counts = state->base_counts + state->runtime_counts;
        } else {
            state->logical_counts = logical_deg * counts_per_degree;
            state->base_counts = state->base_turns *
                (double)v5_wcheckpoint_counts_per_rev[index];
            state->runtime_counts = state->logical_counts - state->base_counts;
        }
        state->sample_sequence++;
        if (state->sample_sequence == 0U) state->sample_sequence = 1U;
        *hal_axis->logical_abs_counts = state->logical_counts;
        *hal_axis->base_counts = state->base_counts;
        *hal_axis->runtime_window_counts = state->runtime_counts;
        *hal_axis->generation = state->generation;
        *hal_axis->raw_count_bits = state->raw_bits;
        *hal_axis->logical_storage_bits = DBL_MANT_DIG;
        *hal_axis->invalid_reason = state->reason;
        *hal_axis->valid = state->valid;
    }
}

int v5_wcheckpoint_read_snapshot(
    unsigned int index, v5_wcheckpoint_snapshot_t *snapshot)
{
    v5_wcheckpoint_axis_state_t *state;
    if (!snapshot || index >= V5_WCHECKPOINT_ROTARY_AXES) return 0;
    state = &axis_state[index];
    snapshot->logical_counts = state->logical_counts;
    snapshot->base_counts = state->base_counts;
    snapshot->runtime_counts = state->runtime_counts;
    snapshot->generation = state->generation;
    snapshot->sample_sequence = state->sample_sequence;
    snapshot->valid = state->valid;
    return 1;
}

int v5_wcheckpoint_forward(
    const double *joint,
    EmcPose *logical_pose,
    KINEMATICS_FORWARD_FLAGS *forward_flags,
    KINEMATICS_INVERSE_FLAGS *inverse_flags)
{
    int result = kinematicsForward(joint, logical_pose, forward_flags, inverse_flags);
    if (result == 0) {
        runtime_to_logical_pose(logical_pose);
    }
    return result;
}

int v5_wcheckpoint_inverse(
    const EmcPose *logical_pose,
    double *joint,
    KINEMATICS_INVERSE_FLAGS *inverse_flags,
    KINEMATICS_FORWARD_FLAGS *forward_flags)
{
    EmcPose runtime_pose;
    logical_to_runtime_pose(logical_pose, &runtime_pose);
    return kinematicsInverse(&runtime_pose, joint, inverse_flags, forward_flags);
}

int v5_wcheckpoint_target_allowed(unsigned int index, double target_deg)
{
    double safe_half_counts = 0.0;
    unsigned long turn_quantum = 1U;
    enum v5_wcheckpoint_reason reason;
    double logical_counts;
    double runtime_counts;

    if (index >= V5_WCHECKPOINT_ROTARY_AXES || !isfinite(target_deg)) {
        return 0;
    }
    reason = profile_reason(index, &safe_half_counts, &turn_quantum);
    if (reason == V5_WCHECKPOINT_DISABLED) {
        return 1;
    }
    if (reason != V5_WCHECKPOINT_OK &&
        reason != V5_WCHECKPOINT_DRIVE_WINDOW_UNPROVEN) {
        axis_state[index].valid = 0U;
        axis_state[index].reason = reason;
        return 0;
    }
    logical_counts = target_deg *
        ((double)v5_wcheckpoint_counts_per_rev[index] / V5_FULL_TURN_DEG);
    if (!isfinite(logical_counts) || fabs(logical_counts) >= ldexp(1.0, DBL_MANT_DIG)) {
        axis_state[index].valid = 0U;
        axis_state[index].reason = V5_WCHECKPOINT_LOGICAL_STORAGE_INSUFFICIENT;
        return 0;
    }
    runtime_counts = (target_deg -
        (axis_state[index].base_turns * V5_FULL_TURN_DEG)) *
        ((double)v5_wcheckpoint_counts_per_rev[index] / V5_FULL_TURN_DEG);
    if (fabs(runtime_counts) >= safe_half_counts) {
        axis_state[index].valid = 0U;
        axis_state[index].reason = V5_WCHECKPOINT_RUNTIME_WINDOW_FAILED;
        return 0;
    }
    if (axis_state[index].router_synced || reason == V5_WCHECKPOINT_OK) {
        return 1;
    }
    return 1;
}
