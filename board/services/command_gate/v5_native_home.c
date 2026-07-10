#include "v5_native_home.h"

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
    int coordinated,
    double positions[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT])
{
    unsigned int slot;
    if (!parameters || parameters->active_axis_count > V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT) {
        return 0;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        if (!axis ||
            !(coordinated
                ? v5_linuxcncrsh_get_axis_position(config, axis->axis, 0, &positions[slot])
                : v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &positions[slot]))) {
            return 0;
        }
    }
    return 1;
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

static int wait_axis_target(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis,
    int coordinated,
    double start,
    double target,
    int *moved)
{
#ifdef _WIN32
    (void)config;
    (void)axis;
    (void)coordinated;
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
        int position_ok = coordinated
            ? v5_linuxcncrsh_get_axis_position(config, axis->axis, 0, &current)
            : v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &current);
        if (position_ok) {
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

static int send_increment(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis,
    int coordinated,
    double delta)
{
    char line[128];
    double speed;
    int rc;
    if (!axis || !isfinite(delta) || fabs(delta) <= V5_HOME_MOTION_TOLERANCE) {
        return 0;
    }
    speed = delta < 0.0 ? -axis->max_velocity : axis->max_velocity;
    rc = coordinated
        ? snprintf(line, sizeof(line), "Set Jog_Incr %c %.6f %.6f", axis->axis, speed, fabs(delta))
        : snprintf(line, sizeof(line), "Set Jog_Incr %u %.6f %.6f", axis->status_slot, speed, fabs(delta));
    return rc > 0 && (size_t)rc < sizeof(line) &&
           v5_linuxcncrsh_send_line(config, "Set Mode Manual") == V5_LINUXCNCRSH_SEND_SENT &&
           v5_linuxcncrsh_send_line(config, line) == V5_LINUXCNCRSH_SEND_SENT;
}

static double proof_delta(const V5NativeMotionAxisParameters *axis, double current)
{
    double delta = rotary_axis(axis->axis) ? V5_HOME_ROTARY_PROOF : V5_HOME_LINEAR_PROOF;
    if (current + delta >= axis->max_limit) {
        delta = -delta;
    }
    return current + delta > axis->min_limit && current + delta < axis->max_limit ? delta : 0.0;
}

static double nearest_zero_target(char axis, double current)
{
    return rotary_axis(axis) ? current - remainder(current, 360.0) : 0.0;
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

static V5LinuxcncrshSendStatus send_bus_home(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    V5NativeHomeResult *result)
{
    double start[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    double current[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
    int moved[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT] = {0, 0, 0, 0, 0, 0};
    int all_homed = 0;
    int coordinated = 0;
    unsigned int slot;
    coordinated = v5_linuxcncrsh_get_all_homed(
        config, parameters->active_axis_count, &all_homed) && all_homed;
    if (!read_all_positions(config, parameters, coordinated, start)) {
        result_code(result, "BUS_HOME_NATIVE_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double delta = axis ? proof_delta(axis, start[slot]) : 0.0;
        if (!axis || delta == 0.0 ||
            !send_increment(config, axis, coordinated, delta) ||
            !wait_axis_target(config, axis, coordinated, start[slot], start[slot] + delta, &moved[slot])) {
            result_code(result, "BUS_HOME_PROOF_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (!read_all_positions(config, parameters, coordinated, current)) {
        result_code(result, "BUS_HOME_POSITION_UNAVAILABLE_AFTER_PROOF");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double target = axis ? nearest_zero_target(axis->axis, current[slot]) : 0.0;
        if (!axis ||
            !send_increment(config, axis, coordinated, target - current[slot]) ||
            !wait_axis_target(config, axis, coordinated, current[slot], target, &moved[slot])) {
            result_code(result, "BUS_HOME_ZERO_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    for (slot = 0U; slot < parameters->active_axis_count; ++slot) {
        const V5NativeMotionAxisParameters *axis = axis_for_slot(parameters, slot);
        double position;
        if (!axis || !moved[slot] ||
            !(coordinated
                ? v5_linuxcncrsh_get_axis_position(config, axis->axis, 0, &position)
                : v5_linuxcncrsh_get_joint_position(config, axis->status_slot, &position)) ||
            target_error(axis->axis, position, 0.0) > V5_HOME_TARGET_TOLERANCE) {
            result_code(result, "BUS_HOME_REAL_MOVE_READBACK_FAILED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
        v5_linuxcncrsh_send_line(config, "Set Home -1") != V5_LINUXCNCRSH_SEND_SENT ||
        !wait_all_homed(config, parameters->active_axis_count, 100U)) {
        result_code(result, "BUS_HOME_HOMED_SYNC_FAILED");
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
    if (!read_all_positions(config, parameters, 0, start) ||
        v5_linuxcncrsh_send_line(config, "Set Mode Manual") != V5_LINUXCNCRSH_SEND_SENT ||
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
    result_init(result);
    if (!config || !parameters || !parameters->loaded ||
        parameters->active_axis_count == 0U || !wait_machine_enabled(config)) {
        result_code(result, "HOME_NATIVE_PARAMETERS_OR_MACHINE_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (result) {
        snprintf(result->mode, sizeof(result->mode), "%s", v5_native_driver_mode_text(parameters->driver_mode));
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
