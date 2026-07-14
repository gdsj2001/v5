#include "v5_native_home_runtime_owner.h"

#include "v5_native_hal_owner_client.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static SRWLOCK g_runtime_lock = SRWLOCK_INIT;
#define RUNTIME_LOCK() AcquireSRWLockExclusive(&g_runtime_lock)
#define RUNTIME_UNLOCK() ReleaseSRWLockExclusive(&g_runtime_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_runtime_lock = PTHREAD_MUTEX_INITIALIZER;
#define RUNTIME_LOCK() ((void)pthread_mutex_lock(&g_runtime_lock))
#define RUNTIME_UNLOCK() ((void)pthread_mutex_unlock(&g_runtime_lock))
#endif

static V5NativeHomeRuntimeState g_runtime_state;

static void set_code(char *code, size_t cap, const char *value)
{
    if (code && cap > 0U) {
        snprintf(code, cap, "%s", value ? value : "HOME_NATIVE_OWNER_FAILED");
    }
}

static int rotary_index(char axis, unsigned int *index)
{
    if (!index) {
        return 0;
    }
    if (axis == 'A') *index = 0U;
    else if (axis == 'B') *index = 1U;
    else if (axis == 'C') *index = 2U;
    else return 0;
    return 1;
}

static int exact_i64(double value, int64_t *out)
{
    double rounded;
    if (!out || !isfinite(value) || value < (double)INT64_MIN || value > (double)INT64_MAX) {
        return 0;
    }
    rounded = nearbyint(value);
    if (fabs(value - rounded) > 0.25) {
        return 0;
    }
    *out = (int64_t)rounded;
    return 1;
}

static int64_t positive_mod(int64_t value, int64_t modulus)
{
    int64_t result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int v5_native_home_runtime_begin(
    unsigned long long run_id,
    unsigned int generation,
    const char *kind)
{
    int accepted = 0;
    if (!run_id || !generation || !kind || !kind[0]) return 0;
    RUNTIME_LOCK();
    if (!g_runtime_state.active) {
        memset(&g_runtime_state, 0, sizeof(g_runtime_state));
        g_runtime_state.active = 1;
        g_runtime_state.progress.run_id = run_id;
        g_runtime_state.progress.generation = generation;
        g_runtime_state.progress.phase = V5_NATIVE_HOME_PHASE_PREPARING;
        snprintf(g_runtime_state.progress.mode, sizeof(g_runtime_state.progress.mode), "%s", kind);
        snprintf(g_runtime_state.progress.direct_reason, sizeof(g_runtime_state.progress.direct_reason), "%s", "HOME_PREPARING");
        accepted = 1;
    }
    RUNTIME_UNLOCK();
    return accepted;
}

void v5_native_home_runtime_publish(const V5NativeHomeProgress *progress)
{
    if (!progress || !progress->run_id || !progress->generation) return;
    RUNTIME_LOCK();
    if (g_runtime_state.active &&
        g_runtime_state.progress.run_id == progress->run_id &&
        g_runtime_state.progress.generation == progress->generation) {
        g_runtime_state.progress = *progress;
        if (progress->terminal) g_runtime_state.active = 0;
    }
    RUNTIME_UNLOCK();
}

int v5_native_home_runtime_snapshot(
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeRuntimeState *state)
{
    int matched = 0;
    if (!state || !run_id || !generation) return 0;
    RUNTIME_LOCK();
    if (g_runtime_state.progress.run_id == run_id &&
        g_runtime_state.progress.generation == generation) {
        *state = g_runtime_state;
        matched = 1;
    }
    RUNTIME_UNLOCK();
    return matched;
}

void v5_native_home_runtime_finish(
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomePhase phase,
    const char *reason,
    int cancelled)
{
    RUNTIME_LOCK();
    if (g_runtime_state.progress.run_id == run_id &&
        g_runtime_state.progress.generation == generation) {
        if (g_runtime_state.progress.cancelled && phase != V5_NATIVE_HOME_PHASE_CANCELLED) {
            RUNTIME_UNLOCK();
            return;
        }
        g_runtime_state.active = 0;
        if (phase == V5_NATIVE_HOME_PHASE_FAILED &&
            g_runtime_state.progress.phase != V5_NATIVE_HOME_PHASE_FAILED) {
            g_runtime_state.progress.failure_phase = g_runtime_state.progress.phase;
        }
        g_runtime_state.progress.phase = (unsigned int)phase;
        g_runtime_state.progress.terminal = 1;
        g_runtime_state.progress.cancelled = cancelled ? 1 : 0;
        snprintf(g_runtime_state.progress.direct_reason,
                 sizeof(g_runtime_state.progress.direct_reason), "%s", reason ? reason : "HOME_FAILED");
    }
    RUNTIME_UNLOCK();
}

int v5_native_home_force_rtcp_off(char *code, size_t code_cap)
{
    V5NativeHalOwnerResponse response;
    int status = v5_native_hal_owner_exchange(
        V5_NATIVE_HAL_OWNER_OP_RTCP_FORCE_OFF, 0U, 800U, &response);
    if (status != V5_NATIVE_HAL_OWNER_CLIENT_OK ||
        !response.rtcp_actual_known || response.rtcp_actual_active) {
        set_code(code, code_cap, "HOME_RTCP_FORCE_OFF_NOT_CONFIRMED");
        return 0;
    }
    set_code(code, code_cap, "HOME_RTCP_FORCE_OFF_OK");
    return 1;
}

int v5_native_home_wcheckpoint_read(
    char axis,
    V5NativeWcheckpointSnapshot *snapshot,
    char *code,
    size_t code_cap)
{
    V5NativeHalOwnerResponse response;
    unsigned int index;
    int status;
    if (!snapshot || !rotary_index(axis, &index)) {
        set_code(code, code_cap, "HOME_WCHECKPOINT_AXIS_INVALID");
        return 0;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    status = v5_native_hal_owner_exchange(
        V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS, index, 150U, &response);
    if (status != V5_NATIVE_HAL_OWNER_CLIENT_OK || !response.wcheckpoint_valid ||
        response.wcheckpoint_generation == 0U ||
        !exact_i64(response.wcheckpoint_logical_counts, &snapshot->logical_counts) ||
        !exact_i64(response.wcheckpoint_base_counts, &snapshot->base_counts) ||
        !exact_i64(response.wcheckpoint_runtime_counts, &snapshot->runtime_counts) ||
        snapshot->runtime_counts != snapshot->logical_counts - snapshot->base_counts) {
        set_code(code, code_cap, "HOME_WCHECKPOINT_READBACK_INVALID");
        return 0;
    }
    snapshot->valid = 1;
    snapshot->generation = response.wcheckpoint_generation;
    set_code(code, code_cap, "HOME_WCHECKPOINT_READBACK_OK");
    return 1;
}

int v5_native_home_safe_zero_plan(
    const V5NativeMotionAxisParameters *axis,
    const V5NativeWcheckpointSnapshot *snapshot,
    V5NativeSafeZeroPlan *plan,
    char *code,
    size_t code_cap)
{
    int64_t zero_counts;
    int64_t counts_per_rev;
    int64_t phase_counts;
    int64_t twice_phase;
    if (!axis || !snapshot || !snapshot->valid || !plan ||
        !rotary_index(axis->axis, &(unsigned int){0}) ||
        !exact_i64(axis->bus_zero_counts, &zero_counts) ||
        !exact_i64(fabs(axis->bus_counts_per_unit) * 360.0, &counts_per_rev) ||
        counts_per_rev <= 0) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_INPUT_INVALID");
        return 0;
    }
    phase_counts = positive_mod(snapshot->logical_counts - zero_counts, counts_per_rev);
    if (phase_counts > INT64_MAX / 2) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_RANGE_INVALID");
        return 0;
    }
    twice_phase = phase_counts * 2;
    memset(plan, 0, sizeof(*plan));
    plan->axis = axis->axis;
    plan->generation = snapshot->generation;
    plan->zero_abs_counts = zero_counts;
    plan->counts_per_rev = counts_per_rev;
    if (!isfinite(axis->arrival_tolerance_units) || axis->arrival_tolerance_units <= 0.0 ||
        !exact_i64(ceil(axis->arrival_tolerance_units * fabs(axis->bus_counts_per_unit)),
                   &plan->arrival_tolerance_counts) ||
        plan->arrival_tolerance_counts <= 0) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_TOLERANCE_INVALID");
        return 0;
    }
    if (twice_phase < counts_per_rev) {
        plan->delta_counts = -phase_counts;
    } else if (twice_phase > counts_per_rev) {
        plan->delta_counts = counts_per_rev - phase_counts;
    } else {
        set_code(code, code_cap, "HOME_SAFE_ZERO_TIE_AMBIGUOUS");
        return 0;
    }
    plan->logical_target_counts = snapshot->logical_counts + plan->delta_counts;
    plan->runtime_target_counts = plan->logical_target_counts - snapshot->base_counts;
    set_code(code, code_cap, "HOME_SAFE_ZERO_PLAN_OK");
    return 1;
}

int v5_native_home_safe_zero_arrived(
    const V5NativeSafeZeroPlan *plan,
    const V5NativeWcheckpointSnapshot *snapshot,
    int64_t *logical_error_counts,
    char *code,
    size_t code_cap)
{
    int64_t phase;
    int64_t error;
    if (!plan || !snapshot || !snapshot->valid || snapshot->generation != plan->generation ||
        snapshot->runtime_counts != snapshot->logical_counts - snapshot->base_counts) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_GENERATION_MISMATCH");
        return 0;
    }
    phase = positive_mod(snapshot->logical_counts - plan->zero_abs_counts, plan->counts_per_rev);
    error = phase;
    if (error > plan->counts_per_rev - error) {
        error = plan->counts_per_rev - error;
    }
    if (logical_error_counts) {
        *logical_error_counts = error;
    }
    if (error > plan->arrival_tolerance_counts) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_ARRIVAL_OUT_OF_TOLERANCE");
        return 0;
    }
    set_code(code, code_cap, "HOME_SAFE_ZERO_ARRIVAL_OK");
    return 1;
}

int v5_native_home_safe_zero_remap(
    V5NativeSafeZeroPlan *plan,
    const V5NativeWcheckpointSnapshot *snapshot,
    char *code,
    size_t code_cap)
{
    if (!plan || !snapshot || !snapshot->valid || snapshot->generation == 0U ||
        snapshot->runtime_counts != snapshot->logical_counts - snapshot->base_counts) {
        set_code(code, code_cap, "HOME_SAFE_ZERO_REMAP_READBACK_INVALID");
        return 0;
    }
    plan->generation = snapshot->generation;
    plan->runtime_target_counts = plan->logical_target_counts - snapshot->base_counts;
    set_code(code, code_cap, "HOME_SAFE_ZERO_REMAP_OK");
    return 1;
}
