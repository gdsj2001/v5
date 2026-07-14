#include "v5_native_home.h"
#include "v5_native_safety.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_HOME_TARGET_TOLERANCE 0.01
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
        double current;
        if (v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &current)) {
            if (fabs(current - start) > V5_HOME_MOTION_TOLERANCE && moved) {
                *moved = 1;
            }
            if (target_error(axis->axis, current, target) <= V5_HOME_TARGET_TOLERANCE) {
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
    const V5NativeMotionParameters *parameters)
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
    V5NativeHomeResult *result)
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
        double target = axis ? axis->bus_home_reference : 0.0;
        if (!axis || !send_increment(config, axis, target - current[slot])) {
            result_code(result, "BUS_HOME_ZERO_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!wait_axis_target(config, axis, current[slot], target, &moved[slot])) {
            stop_increment(config, axis);
            result_code(result, "BUS_HOME_ZERO_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double position;
        if (!axis || !moved[slot] ||
            !v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &position) ||
            fabs(position - axis->bus_home_reference) > V5_HOME_TARGET_TOLERANCE) {
            result_code(result, "BUS_HOME_REAL_MOVE_READBACK_FAILED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
        !sync_homed_in_configured_order(config, parameters)) {
        (void)v5_linuxcncrsh_send_line(config, "Set Teleop_Enable On");
        result_code(result, "BUS_HOME_HOMED_SYNC_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Teleop_Enable On") != V5_LINUXCNCRSH_SEND_SENT ||
        !wait_teleop_enabled(config, 1)) {
        result_code(result, "BUS_HOME_TELEOP_RESTORE_FAILED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double position;
        if (!axis || !v5_linuxcncrsh_get_axis_position(config, axis->axis, 0, &position) ||
            target_error(axis->axis, position, 0.0) > V5_HOME_TARGET_TOLERANCE) {
            result_code(result, "BUS_HOME_AXIS_ZERO_READBACK_FAILED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
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
    V5NativeHomeResult *result)
{
#ifdef _WIN32
    (void)config;
    (void)parameters;
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
    V5NativeHomeResult *result)
{
    V5NativeSafetyResult safety;
    const char *safety_reject_code;
    int stillness;
    result_init(result);
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
    if (v5_linuxcncrsh_send_line(config, "Set Abort") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "HOME_ABORT_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_BUS) {
        return send_bus_home(config, parameters, result);
    }
    if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_PULSE) {
        return send_pulse_home(config, parameters, result);
    }
    result_code(result, "HOME_ACTIVE_MODE_UNKNOWN");
    return V5_LINUXCNCRSH_SEND_INVALID;
}
