#include "v5_native_home.h"
#include "v5_native_safety.h"
#include "v5_native_home_runtime_owner.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_HOME_LINEAR_TARGET_TOLERANCE 0.01
#define V5_HOME_MOTION_TOLERANCE 0.001
#define V5_HOME_LINEAR_PROOF 0.1
#define V5_HOME_ROTARY_PROOF 1.0
#define V5_HOME_AXIS_WAIT_ATTEMPTS 240U
#define V5_HOME_PULSE_WAIT_ATTEMPTS 2200U
#define V5_HOME_WAIT_US 50000U
#define V5_HOME_STILLNESS_SAMPLE_COUNT 3U
#define V5_HOME_STILLNESS_WAIT_US 100000U

static void result_init(V5NativeHomeResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->code, sizeof(result->code), "%s", "HOME_NOT_ATTEMPTED");
}

static void result_code(V5NativeHomeResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s", code ? code : "HOME_FAILED");
    }
}

typedef struct V5NativeHomeProgressContext {
    V5NativeHomeProgress value;
    V5NativeHomeProgressCallback callback;
    void *user_data;
} V5NativeHomeProgressContext;

#ifdef _WIN32
__declspec(thread) static V5NativeHomeProgressContext *g_home_progress_context;
#else
static __thread V5NativeHomeProgressContext *g_home_progress_context;
#endif

static void progress_emit(
    V5NativeHomeProgressContext *context,
    V5NativeHomePhase phase,
    const char *axes,
    int detail_valid,
    double actual,
    double target,
    double tolerance,
    const char *reason,
    int terminal,
    int cancelled)
{
    const char *p;
    if (!context) return;
    if (phase == V5_NATIVE_HOME_PHASE_FAILED &&
        context->value.phase != V5_NATIVE_HOME_PHASE_FAILED) {
        context->value.failure_phase = context->value.phase;
    }
    context->value.phase = (unsigned int)phase;
    context->value.terminal = terminal;
    context->value.cancelled = cancelled;
    context->value.detail_valid = detail_valid;
    context->value.actual = actual;
    context->value.target = target;
    context->value.tolerance = tolerance;
    context->value.current_axis_mask = 0U;
    for (p = axes ? axes : ""; *p; ++p) {
        if (*p == 'X') context->value.current_axis_mask |= 1U << 0;
        else if (*p == 'Y') context->value.current_axis_mask |= 1U << 1;
        else if (*p == 'Z') context->value.current_axis_mask |= 1U << 2;
        else if (*p == 'A') context->value.current_axis_mask |= 1U << 3;
        else if (*p == 'B') context->value.current_axis_mask |= 1U << 4;
        else if (*p == 'C') context->value.current_axis_mask |= 1U << 5;
    }
    snprintf(context->value.current_axes, sizeof(context->value.current_axes), "%s", axes ? axes : "");
    snprintf(context->value.direct_reason, sizeof(context->value.direct_reason), "%s", reason ? reason : "");
    if (context->callback) context->callback(&context->value, context->user_data);
}

static int home_cancel_requested(const V5LinuxcncrshConfig *config)
{
    V5NativeSafetyResult safety;
    if (!g_home_progress_context || g_home_progress_context->value.terminal) return 0;
    if (v5_native_safety_read_status(&safety) == V5_NATIVE_SAFETY_SEND_SENT &&
        safety.safety_estop_known && safety.safety_estop_active) {
        (void)v5_linuxcncrsh_send_line(config, "Set Abort");
        progress_emit(g_home_progress_context, V5_NATIVE_HOME_PHASE_CANCELLED,
                      g_home_progress_context->value.current_axes, 0,
                      0.0, 0.0, 0.0, "HOME_CANCELLED_BY_ESTOP", 1, 1);
        return 1;
    }
    return 0;
}

const char *v5_native_home_safety_reject_code(
    int estop_known,
    int estop_active,
    int machine_known,
    int machine_enabled)
{
    if (estop_known && estop_active) {
        return "HOME_PRECONDITION_ESTOP";
    }
    if (machine_known && !machine_enabled) {
        return "HOME_PRECONDITION_DISABLED";
    }
    return 0;
}

static int rotary_axis(char axis)
{
    return axis == 'A' || axis == 'B' || axis == 'C';
}

static double target_error(char axis, double current, double target)
{
    double error = fabs(current - target);
    if (!isfinite(error)) {
        return HUGE_VAL;
    }
    if (rotary_axis(axis)) {
        error = fmod(error, 360.0);
        if (error > 180.0) {
            error = 360.0 - error;
        }
    }
    return error;
}

static double axis_arrival_tolerance(const V5NativeMotionAxisParameters *axis)
{
    if (axis && rotary_axis(axis->axis) && isfinite(axis->bus_counts_per_unit) &&
        fabs(axis->bus_counts_per_unit) > 0.0) {
        return 1.0 / fabs(axis->bus_counts_per_unit);
    }
    return V5_HOME_LINEAR_TARGET_TOLERANCE;
}

double v5_native_home_target_delta(char axis, double current, double target)
{
    double delta = target - current;
    if (!isfinite(delta)) {
        return NAN;
    }
    if (rotary_axis(axis)) {
        delta = remainder(delta, 360.0);
    }
    return delta == 0.0 ? 0.0 : delta;
}

static const V5NativeMotionAxisParameters *axis_for_slot(
    const V5NativeMotionParameters *parameters,
    unsigned int slot)
{
    unsigned int i;
    if (!parameters || !parameters->loaded) {
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (parameters->axes[i].active && parameters->axes[i].status_slot == slot) {
            return &parameters->axes[i];
        }
    }
    return 0;
}

static int read_all_positions(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    double positions[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT])
{
    unsigned int slot;
    if (!parameters || parameters->active_axis_count > V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT) {
        return 0;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        if (!axis || !v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &positions[slot])) {
            return 0;
        }
    }
    return 1;
}

int v5_native_home_positions_still(
    const double *previous,
    const double *current,
    unsigned int axis_count)
{
    unsigned int axis;
    if (!previous || !current || axis_count == 0U ||
        axis_count > V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT) {
        return 0;
    }
    for (axis = 0U; axis < axis_count; ++axis) {
        if (!isfinite(previous[axis]) || !isfinite(current[axis]) ||
            fabs(current[axis] - previous[axis]) > V5_HOME_MOTION_TOLERANCE) {
            return 0;
        }
    }
    return 1;
}

static int active_axes_still(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters)
{
#ifdef _WIN32
    (void)config;
    (void)parameters;
    return -1;
#else
    double previous[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    double current[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    unsigned int sample;
    if (!read_all_positions(config, parameters, previous)) {
        return -1;
    }
    for (sample = 0U; sample < V5_HOME_STILLNESS_SAMPLE_COUNT; ++sample) {
        if (home_cancel_requested(config)) return 0;
        usleep(V5_HOME_STILLNESS_WAIT_US);
        if (!read_all_positions(config, parameters, current)) {
            return -1;
        }
        if (!v5_native_home_positions_still(
                previous, current, parameters->active_axis_count)) {
            return 0;
        }
        memcpy(previous, current, sizeof(previous));
    }
    return 1;
#endif
}

static int wait_machine_enabled(const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return 0;
#else
    unsigned int attempt;
    for (attempt = 0U; attempt < 20U; ++attempt) {
        if (home_cancel_requested(config)) return 0;
        int enabled = 0;
        if (v5_linuxcncrsh_probe_machine_enabled(config, &enabled, 0, 0) && enabled) {
            return 1;
        }
        usleep(V5_HOME_WAIT_US);
    }
    return 0;
#endif
}

static int wait_teleop_enabled(
    const V5LinuxcncrshConfig *config,
    int expected_enabled)
{
#ifdef _WIN32
    (void)config;
    (void)expected_enabled;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < 100U; ++attempt) {
        if (home_cancel_requested(config)) return 0;
        int enabled = 0;
        if (v5_linuxcncrsh_get_teleop_enabled(config, &enabled) &&
            enabled == (expected_enabled ? 1 : 0)) {
            ++stable;
            if (stable >= 2U) {
                return 1;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_HOME_WAIT_US);
    }
    return 0;
#endif
}

static int wait_axis_target(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis,
    double start,
    double target,
    int *moved)
{
#ifdef _WIN32
    (void)config;
    (void)axis;
    (void)start;
    (void)target;
    (void)moved;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    if (!axis) {
        return 0;
    }
    for (attempt = 0U; attempt < V5_HOME_AXIS_WAIT_ATTEMPTS; ++attempt) {
        if (home_cancel_requested(config)) return 0;
        double current;
        if (v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &current)) {
            if (fabs(current - start) > V5_HOME_MOTION_TOLERANCE && moved) {
                *moved = 1;
            }
            if (target_error(axis->axis, current, target) <= axis_arrival_tolerance(axis)) {
                ++stable;
                if (stable >= 3U) {
                    return 1;
                }
            } else {
                stable = 0U;
            }
        }
        usleep(V5_HOME_WAIT_US);
    }
    return 0;
#endif
}

int v5_native_home_format_increment(
    const V5NativeMotionAxisParameters *axis,
    double delta,
    char *line,
    size_t line_size)
{
    double speed;
    int rc;
    if (!axis || !line || line_size == 0U ||
        !isfinite(delta) || fabs(delta) <= V5_HOME_MOTION_TOLERANCE ||
        !isfinite(axis->max_velocity) || axis->max_velocity <= 0.0) {
        return 0;
    }
    speed = (delta < 0.0 ? -axis->max_velocity : axis->max_velocity) * 60.0;
    rc = snprintf(
        line,
        line_size,
        "Set Jog_Incr %u %.6f %.6f",
        axis->status_slot,
        speed,
        fabs(delta));
    return rc > 0 && (size_t)rc < line_size;
}

static int send_increment(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis,
    double delta)
{
    char line[128];
    return v5_native_home_format_increment(axis, delta, line, sizeof(line)) &&
           v5_linuxcncrsh_send_line(config, line) == V5_LINUXCNCRSH_SEND_SENT;
}

static void stop_increment(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis)
{
    char line[64];
    int rc;
    if (!axis) {
        return;
    }
    rc = snprintf(line, sizeof(line), "Set Jog_Stop %u", axis->status_slot);
    if (rc > 0 && (size_t)rc < sizeof(line)) {
        (void)v5_linuxcncrsh_send_line(config, line);
    }
}

static double proof_delta(const V5NativeMotionAxisParameters *axis, double current)
{
    double delta = rotary_axis(axis->axis) ? V5_HOME_ROTARY_PROOF : V5_HOME_LINEAR_PROOF;
    if (current + delta >= axis->max_limit) {
        delta = -delta;
    }
    return current + delta > axis->min_limit && current + delta < axis->max_limit ? delta : 0.0;
}

static int wait_all_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int expected_joint_count,
    unsigned int attempts)
{
#ifdef _WIN32
    (void)config;
    (void)expected_joint_count;
    (void)attempts;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < attempts; ++attempt) {
        if (home_cancel_requested(config)) return 0;
        int all_homed = 0;
        if (v5_linuxcncrsh_get_all_homed(config, expected_joint_count, &all_homed) && all_homed) {
            ++stable;
            if (stable >= 2U) {
                return 1;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_HOME_WAIT_US);
    }
    return 0;
#endif
}

static int wait_joint_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    unsigned int attempts)
{
#ifdef _WIN32
    (void)config;
    (void)joint;
    (void)attempts;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < attempts; ++attempt) {
        if (home_cancel_requested(config)) return 0;
        int homed = 0;
        if (v5_linuxcncrsh_get_joint_homed(config, joint, &homed) && homed) {
            ++stable;
            if (stable >= 2U) {
                return 1;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_HOME_WAIT_US);
    }
    return 0;
#endif
}

int v5_native_home_joint_needs_sync(int homed_status_available, int homed)
{
    if (!homed_status_available) {
        return -1;
    }
    return homed ? 0 : 1;
}

static int sync_homed_in_configured_order(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeProgressContext *progress)
{
    int needs_sync[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT] = {0, 0, 0, 0, 0, 0};
    int previous_sequence = -1;
    unsigned int completed = 0U;
    unsigned int slot;
    if (!parameters || !parameters->loaded) {
        return 0;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        int homed = 0;
        int homed_status_available;
        if (!axis) {
            return 0;
        }
        homed_status_available = v5_linuxcncrsh_get_joint_homed(
            config, axis->status_slot, &homed);
        needs_sync[slot] = v5_native_home_joint_needs_sync(homed_status_available, homed);
        if (needs_sync[slot] < 0) {
            return 0;
        }
    }
    while (completed < parameters->active_axis_count) {
        int next_sequence = INT_MAX;
        for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
            const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
            if (!axis || axis->home_sequence < 0) {
                return 0;
            }
            if (axis->home_sequence > previous_sequence && axis->home_sequence < next_sequence) {
                next_sequence = axis->home_sequence;
            }
        }
        if (next_sequence == INT_MAX) {
            return 0;
        }
        {
            char axes[16] = "";
            size_t used = 0U;
            for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
                const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
                if (axis && axis->home_sequence == next_sequence && used + 2U < sizeof(axes)) {
                    if (used) axes[used++] = '/';
                    axes[used++] = axis->axis;
                    axes[used] = '\0';
                }
            }
            progress_emit(progress, V5_NATIVE_HOME_PHASE_HOMED_SYNC, axes, 0, 0.0, 0.0, 0.0, "HOME_HOMED_SYNC", 0, 0);
        }
        for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
            const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
            char line[64];
            int rc;
            if (!axis || axis->home_sequence != next_sequence || !needs_sync[slot]) {
                continue;
            }
            rc = snprintf(line, sizeof(line), "Set Home %u", axis->status_slot);
            if (rc <= 0 || (size_t)rc >= sizeof(line) ||
                v5_linuxcncrsh_send_line(config, line) != V5_LINUXCNCRSH_SEND_SENT) {
                return 0;
            }
        }
        for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
            const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
            if (axis && axis->home_sequence == next_sequence) {
                if (needs_sync[slot] && !wait_joint_homed(config, axis->status_slot, 100U)) {
                    return 0;
                }
                ++completed;
            }
        }
        previous_sequence = next_sequence;
    }
    return wait_all_homed(config, parameters->active_axis_count, 100U);
}

static V5LinuxcncrshSendStatus send_bus_home(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result,
    V5NativeHomeProgressContext *progress)
{
    double start[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    double current[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    int moved[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT] = {0, 0, 0, 0, 0, 0};
    unsigned int slot;
    if (!parameters->runtime_owner_loaded) {
        result_code(result, "BUS_HOME_RUNTIME_OWNER_NOT_LOADED");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        if (!axis || !axis->bus_zero_evidence_known) {
            result_code(result, "BUS_HOME_ZERO_EVIDENCE_MISSING");
            return V5_LINUXCNCRSH_SEND_INVALID;
        }
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
        v5_linuxcncrsh_send_line(config, "Set Teleop_Enable Off") != V5_LINUXCNCRSH_SEND_SENT ||
        !wait_teleop_enabled(config, 0)) {
        result_code(result, "BUS_HOME_JOINT_MODE_REJECTED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!read_all_positions(config, parameters, start)) {
        result_code(result, "BUS_HOME_NATIVE_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double delta = axis ? proof_delta(axis, start[slot]) : 0.0;
        char axes[2] = {axis ? axis->axis : '\0', '\0'};
        progress_emit(progress, V5_NATIVE_HOME_PHASE_PROOF_MOVE, axes, 1, start[slot], start[slot] + delta, axis_arrival_tolerance(axis), "HOME_PROOF_MOVE", 0, 0);
        if (!axis || delta == 0.0 || !send_increment(config, axis, delta)) {
            result_code(result, "BUS_HOME_PROOF_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!wait_axis_target(config, axis, start[slot], start[slot] + delta, &moved[slot])) {
            stop_increment(config, axis);
            result_code(result, "BUS_HOME_PROOF_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (!read_all_positions(config, parameters, current)) {
        result_code(result, "BUS_HOME_POSITION_UNAVAILABLE_AFTER_PROOF");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        V5NativeWcheckpointSnapshot snapshot;
        V5NativeSafeZeroPlan plan;
        char owner_code[64];
        char axes[2] = {axis ? axis->axis : '\0', '\0'};
        double target = axis ? axis->bus_home_reference : 0.0;
        double delta = axis ? v5_native_home_target_delta(axis->axis, current[slot], target) : NAN;
        double motion_target = current[slot] + delta;
        if (axis && rotary_axis(axis->axis)) {
            if (!v5_native_home_wcheckpoint_read(axis->axis, &snapshot, owner_code, sizeof(owner_code)) ||
                !v5_native_home_safe_zero_plan(axis, &snapshot, &plan, owner_code, sizeof(owner_code))) {
                result_code(result, owner_code);
                return V5_LINUXCNCRSH_SEND_IO_ERROR;
            }
            delta = (double)plan.delta_counts / axis->bus_counts_per_unit;
            motion_target = (double)plan.runtime_target_counts / axis->bus_counts_per_unit;
        }
        progress_emit(progress, V5_NATIVE_HOME_PHASE_ZERO_RETURN, axes, 1, current[slot], motion_target, axis_arrival_tolerance(axis), "HOME_ZERO_RETURN", 0, 0);
        if (!axis || !isfinite(delta) || !send_increment(config, axis, delta)) {
            result_code(result, "BUS_HOME_ZERO_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!wait_axis_target(config, axis, current[slot], motion_target, &moved[slot])) {
            stop_increment(config, axis);
            result_code(result, "BUS_HOME_ZERO_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (rotary_axis(axis->axis)) {
            V5NativeWcheckpointSnapshot after;
            int64_t logical_error = 0;
            if (!v5_native_home_wcheckpoint_read(axis->axis, &after, owner_code, sizeof(owner_code))) {
                result_code(result, owner_code);
                return V5_LINUXCNCRSH_SEND_IO_ERROR;
            }
            if (after.generation != plan.generation) {
                if (!v5_native_home_safe_zero_remap(&plan, &after, owner_code, sizeof(owner_code))) {
                    result_code(result, owner_code);
                    return V5_LINUXCNCRSH_SEND_IO_ERROR;
                }
                motion_target = (double)plan.runtime_target_counts / axis->bus_counts_per_unit;
                delta = motion_target - ((double)after.runtime_counts / axis->bus_counts_per_unit);
                if (fabs(delta) > axis_arrival_tolerance(axis) &&
                    (!send_increment(config, axis, delta) ||
                     !wait_axis_target(config, axis, current[slot], motion_target, &moved[slot]))) {
                    result_code(result, "HOME_SAFE_ZERO_REMAP_FAILED");
                    return V5_LINUXCNCRSH_SEND_IO_ERROR;
                }
                if (!v5_native_home_wcheckpoint_read(axis->axis, &after, owner_code, sizeof(owner_code))) {
                    result_code(result, owner_code);
                    return V5_LINUXCNCRSH_SEND_IO_ERROR;
                }
            }
            if (!v5_native_home_safe_zero_arrived(&plan, &after, &logical_error, owner_code, sizeof(owner_code))) {
                result_code(result, owner_code);
                return V5_LINUXCNCRSH_SEND_IO_ERROR;
            }
        }
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double position;
        if (!axis || !moved[slot] ||
            !v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &position) ||
            (!rotary_axis(axis->axis) &&
             target_error(axis->axis, position, axis->bus_home_reference) > axis_arrival_tolerance(axis))) {
            result_code(result, "BUS_HOME_REAL_MOVE_READBACK_FAILED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
        !sync_homed_in_configured_order(config, parameters, progress)) {
        (void)v5_linuxcncrsh_send_line(config, "Set Teleop_Enable On");
        result_code(result, "BUS_HOME_HOMED_SYNC_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Teleop_Enable On") != V5_LINUXCNCRSH_SEND_SENT ||
        !wait_teleop_enabled(config, 1)) {
        result_code(result, "BUS_HOME_TELEOP_RESTORE_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (result) {
        result->movement_confirmed = 1;
        result->arrival_confirmed = 1;
        result->homed_confirmed = 1;
    }
    result_code(result, "BUS_HOME_REAL_MOVE_CONFIRMED");
    return V5_LINUXCNCRSH_SEND_SENT;
}

static V5LinuxcncrshSendStatus send_pulse_home(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result,
    V5NativeHomeProgressContext *progress)
{
#ifdef _WIN32
    (void)config;
    (void)parameters;
    (void)progress;
    result_code(result, "PULSE_HOME_UNAVAILABLE_ON_WIN32");
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    double start[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    int moved[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT] = {0, 0, 0, 0, 0, 0};
    unsigned int attempt;
    unsigned int slot;
    if (!parameters->runtime_owner_loaded || !parameters->pulse_runtime_selectable) {
        result_code(result, "PULSE_HOME_NOT_RUNTIME_SELECTABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (!read_all_positions(config, parameters, start) ||
        v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
        v5_linuxcncrsh_send_line(config, "Set Teleop_Enable Off") != V5_LINUXCNCRSH_SEND_SENT ||
        v5_linuxcncrsh_send_line(config, "Set Home -1") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "PULSE_HOME_COMMAND_REJECTED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (attempt = 0U; attempt < V5_HOME_PULSE_WAIT_ATTEMPTS; ++attempt) {
        if (home_cancel_requested(config)) {
            result_code(result, "HOME_CANCELLED_BY_ESTOP");
            return V5_LINUXCNCRSH_SEND_INVALID;
        }
        int all_homed = 0;
        for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
            const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
            double position;
            if (axis && v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &position) &&
                fabs(position - start[slot]) > V5_HOME_MOTION_TOLERANCE) {
                moved[slot] = 1;
            }
        }
        if (v5_linuxcncrsh_get_all_homed(
                config, parameters->active_axis_count, &all_homed) && all_homed) {
            int all_moved = 1;
            for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
                if (!moved[slot]) {
                    all_moved = 0;
                }
            }
            if (!all_moved) {
                result_code(result, "PULSE_HOME_REAL_MOVE_NOT_CONFIRMED");
                return V5_LINUXCNCRSH_SEND_IO_ERROR;
            }
            if (v5_linuxcncrsh_send_line(config, "Set Teleop_Enable On") != V5_LINUXCNCRSH_SEND_SENT) {
                result_code(result, "PULSE_HOME_TELEOP_RESTORE_FAILED");
                return V5_LINUXCNCRSH_SEND_IO_ERROR;
            }
            for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
                const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
                double position;
                if (!axis || !v5_linuxcncrsh_get_axis_position(config, axis->axis, 0, &position) ||
                    target_error(axis->axis, position, 0.0) > V5_HOME_TARGET_TOLERANCE) {
                    result_code(result, "PULSE_HOME_AXIS_ZERO_READBACK_FAILED");
                    return V5_LINUXCNCRSH_SEND_IO_ERROR;
                }
            }
            if (result) {
                result->movement_confirmed = 1;
                result->arrival_confirmed = 1;
                result->homed_confirmed = 1;
            }
            result_code(result, "PULSE_HOME_REAL_MOVE_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_SENT;
        }
        usleep(V5_HOME_WAIT_US);
    }
    result_code(result, "PULSE_HOME_COMPLETION_NOT_CONFIRMED");
    return V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}

V5LinuxcncrshSendStatus v5_native_home_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data)
{
    V5NativeSafetyResult safety;
    const char *safety_reject_code;
    int stillness;
    V5LinuxcncrshSendStatus status;
    V5NativeHomeProgressContext progress;
    char rtcp_code[64];
    memset(&progress, 0, sizeof(progress));
    progress.value.run_id = run_id;
    progress.value.generation = generation;
    snprintf(progress.value.mode, sizeof(progress.value.mode), "%s", "all");
    progress.callback = progress_cb;
    progress.user_data = progress_user_data;
    g_home_progress_context = &progress;
    result_init(result);
    progress_emit(&progress, V5_NATIVE_HOME_PHASE_PREPARING, "", 0, 0.0, 0.0, 0.0, "HOME_PREPARING", 0, 0);
    if (!config || !parameters || !parameters->loaded ||
        parameters->active_axis_count == 0U) {
        result_code(result, "HOME_NATIVE_PARAMETERS_OR_MACHINE_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (v5_native_safety_read_status(&safety) == V5_NATIVE_SAFETY_SEND_SENT) {
        safety_reject_code = v5_native_home_safety_reject_code(
            safety.safety_estop_known,
            safety.safety_estop_active,
            safety.machine_enable_known,
            safety.machine_enabled);
        if (safety_reject_code) {
            result_code(result, safety_reject_code);
            return V5_LINUXCNCRSH_SEND_INVALID;
        }
    }
    if (!wait_machine_enabled(config)) {
        result_code(result, "HOME_NATIVE_PARAMETERS_OR_MACHINE_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (result) {
        snprintf(result->mode, sizeof(result->mode), "%s", v5_native_driver_mode_text(parameters->driver_mode));
    }
    stillness = active_axes_still(config, parameters);
    if (stillness < 0) {
        result_code(result, "HOME_STILLNESS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!stillness) {
        result_code(result, "HOME_AXES_MOVING");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    progress_emit(&progress, V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF, "", 0, 0.0, 0.0, 0.0, "HOME_RTCP_FORCE_OFF", 0, 0);
    if (!v5_native_home_force_rtcp_off(rtcp_code, sizeof(rtcp_code))) {
        result_code(result, rtcp_code);
        progress_emit(&progress, V5_NATIVE_HOME_PHASE_FAILED, "", 0, 0.0, 0.0, 0.0, rtcp_code, 1, 0);
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Abort") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "HOME_ABORT_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_BUS) {
        status = send_bus_home(config, parameters, result, &progress);
    } else if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_PULSE) {
        status = send_pulse_home(config, parameters, result, &progress);
    } else {
        result_code(result, "HOME_ACTIVE_MODE_UNKNOWN");
        status = V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (progress.value.cancelled) {
        result_code(result, "HOME_CANCELLED_BY_ESTOP");
        status = V5_LINUXCNCRSH_SEND_INVALID;
    } else if (status == V5_LINUXCNCRSH_SEND_SENT) {
        progress_emit(&progress, V5_NATIVE_HOME_PHASE_COMPLETE, "", 0, 0.0, 0.0, 0.0, result ? result->code : "HOME_COMPLETE", 1, 0);
    } else {
        progress_emit(&progress, V5_NATIVE_HOME_PHASE_FAILED, progress.value.current_axes, progress.value.detail_valid, progress.value.actual, progress.value.target, progress.value.tolerance, result ? result->code : "HOME_FAILED", 1, 0);
    }
    return status;
}
