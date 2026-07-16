#include "v5_native_axis_zero_position.h"

#include "v5_native_readback.h"
#include "v5_native_wcs_status.h"
#include "v5_native_home_runtime_owner.h"
#include "v5_native_safety.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_AXIS_ZERO_TARGET_TOLERANCE 0.001
#define V5_AXIS_ZERO_COMPARE_EPSILON 1.0e-9
#define V5_AXIS_ZERO_MOTION_TOLERANCE 0.0001
#define V5_AXIS_ZERO_LINEAR_PROOF 0.1
#define V5_AXIS_ZERO_ROTARY_PROOF 1.0
#define V5_AXIS_ZERO_WAIT_ATTEMPTS 2400U
#define V5_AXIS_ZERO_WAIT_US 50000U

static void result_init(V5NativeAxisZeroPositionResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->code, sizeof(result->code), "%s", "AXIS_ZERO_NOT_ATTEMPTED");
}

static void result_code(V5NativeAxisZeroPositionResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s", code ? code : "AXIS_ZERO_FAILED");
    }
}

static int request_ok(const V5CommandPrepared *prepared, const V5CommandRequest *request)
{
    char axis;
    if (!prepared || !request || !prepared->accepted ||
        request->kind != V5_COMMAND_AXIS_ZERO_POSITION ||
        !prepared->name || strcmp(prepared->name, "axis_zero_position") != 0 ||
        !prepared->owner || strcmp(prepared->owner, "native_axis_zero_position") != 0 ||
        !request->text_value || request->text_value[1] || !request->mode_value) {
        return 0;
    }
    axis = request->text_value[0];
    return (axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'A' || axis == 'B' || axis == 'C') &&
           (strcmp(request->mode_value, "mcs") == 0 || strcmp(request->mode_value, "wcs") == 0);
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

static int send_target(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double target);

static int read_safe_zero_snapshot(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionAxisParameters *axis_parameters,
    V5NativeWcheckpointSnapshot *snapshot,
    char *owner_code,
    size_t owner_code_cap)
{
    double runtime_position;
    if (!config || !axis_parameters || !snapshot ||
        !v5_native_home_wcheckpoint_read(
            axis_parameters->axis, snapshot, owner_code, owner_code_cap)) {
        return 0;
    }
    if (!v5_linuxcncrsh_get_joint_position(
            config, axis_parameters->status_slot, &runtime_position)) {
        if (owner_code && owner_code_cap > 0U) {
            snprintf(owner_code, owner_code_cap, "%s", "HOME_WCHECKPOINT_RUNTIME_ACTUAL_INVALID");
        }
        return 0;
    }
    return v5_native_home_wcheckpoint_bind_runtime_actual(
        axis_parameters, runtime_position, snapshot, owner_code, owner_code_cap);
}

static int wait_position(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double before,
    double target,
    int *moved,
    double *position_out,
    V5NativeSafeZeroPlan *safe_zero_plan,
    const V5NativeMotionAxisParameters *axis_parameters,
    char *owner_code,
    size_t owner_code_cap,
    V5NativeHomeProgress *progress,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data)
{
#ifdef _WIN32
    (void)config;
    (void)axis;
    (void)relative;
    (void)before;
    (void)target;
    (void)moved;
    (void)position_out;
    (void)safe_zero_plan;
    (void)axis_parameters;
    (void)owner_code;
    (void)owner_code_cap;
    (void)progress;
    (void)progress_cb;
    (void)progress_user_data;
    return 0;
#else
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < V5_AXIS_ZERO_WAIT_ATTEMPTS; ++attempt) {
        V5NativeSafetyResult safety;
        double current;
        if (v5_native_safety_read_status(&safety) == V5_NATIVE_SAFETY_SEND_SENT &&
            safety.safety_estop_known && safety.safety_estop_active) {
            (void)v5_linuxcncrsh_send_line(config, "Set Abort");
            if (progress) {
                progress->phase = V5_NATIVE_HOME_PHASE_CANCELLED;
                progress->terminal = 1;
                progress->cancelled = 1;
                snprintf(progress->direct_reason, sizeof(progress->direct_reason), "%s", "HOME_CANCELLED_BY_ESTOP");
                if (progress_cb) progress_cb(progress, progress_user_data);
            }
            return 0;
        }
        if (safe_zero_plan) {
            V5NativeWcheckpointSnapshot snapshot;
            int64_t logical_error = 0;
            if (!axis_parameters ||
                !read_safe_zero_snapshot(
                    config, axis_parameters, &snapshot, owner_code, owner_code_cap)) {
                return 0;
            }
            current = (double)snapshot.runtime_counts / axis_parameters->bus_counts_per_unit;
            if (fabs(current - before) > V5_AXIS_ZERO_MOTION_TOLERANCE && moved) *moved = 1;
            if (snapshot.generation != safe_zero_plan->generation) {
                double remaining_delta;
                (void)v5_linuxcncrsh_send_line(config, "Set Abort");
                if (!v5_native_home_safe_zero_remap(
                        safe_zero_plan, &snapshot, owner_code, owner_code_cap)) return 0;
                target = (double)safe_zero_plan->runtime_target_counts /
                         axis_parameters->bus_counts_per_unit;
                remaining_delta = (double)(safe_zero_plan->logical_target_counts -
                                           snapshot.logical_counts) /
                                  axis_parameters->bus_counts_per_unit;
                if (progress) {
                    progress->actual = current;
                    progress->target = target;
                    snprintf(progress->direct_reason, sizeof(progress->direct_reason), "%s", "HOME_SAFE_ZERO_REMAPPED");
                    if (progress_cb) progress_cb(progress, progress_user_data);
                }
                if (fabs(remaining_delta) > axis_parameters->positioning_resolution_units &&
                    (v5_linuxcncrsh_send_line(config, "Set Mode MDI") != V5_LINUXCNCRSH_SEND_SENT ||
                     !send_target(config, axis, 0, target))) {
                    if (owner_code && owner_code_cap > 0U) {
                        snprintf(owner_code, owner_code_cap, "%s", "HOME_SAFE_ZERO_REMAP_FAILED");
                    }
                    return 0;
                }
                before = current;
                stable = 0U;
            }
            if (v5_native_home_safe_zero_arrived(
                    safe_zero_plan, &snapshot, &logical_error, owner_code, owner_code_cap)) {
                ++stable;
                if (stable >= 3U) {
                    if (position_out) *position_out = current;
                    return 1;
                }
            } else {
                stable = 0U;
            }
            usleep(V5_AXIS_ZERO_WAIT_US);
            continue;
        }
        if (v5_linuxcncrsh_get_axis_position(config, axis, relative, &current)) {
            if (fabs(current - before) > V5_AXIS_ZERO_MOTION_TOLERANCE && moved) {
                *moved = 1;
            }
            if (target_error(axis, current, target) <=
                V5_AXIS_ZERO_TARGET_TOLERANCE + V5_AXIS_ZERO_COMPARE_EPSILON) {
                ++stable;
                if (stable >= 3U) {
                    if (position_out) {
                        *position_out = current;
                    }
                    return 1;
                }
            } else {
                stable = 0U;
            }
        }
        usleep(V5_AXIS_ZERO_WAIT_US);
    }
    return 0;
#endif
}

static int send_target(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double target)
{
    char line[96];
    int rc = snprintf(
        line,
        sizeof(line),
        relative ? "Set MDI G0 %c%.6f" : "Set MDI G53 G0 %c%.6f",
        axis,
        target);
    return rc > 0 && (size_t)rc < sizeof(line) &&
           v5_linuxcncrsh_send_line(config, line) == V5_LINUXCNCRSH_SEND_SENT;
}

static int read_wcs_snapshot(V5NativeReadback *readback)
{
    v5_native_readback_init(readback);
    return v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, readback) &&
           v5_native_readback_wcs_table_known(readback);
}

static int wcs_offset_same(
    const V5NativeReadback *before,
    const V5NativeReadback *after)
{
    const double *before_offsets;
    const double *after_offsets;
    unsigned int slot;
    if (!before || !after || before->wcs_index != after->wcs_index) {
        return 0;
    }
    before_offsets = v5_native_readback_active_wcs_offsets(before);
    after_offsets = v5_native_readback_active_wcs_offsets(after);
    if (!before_offsets || !after_offsets) {
        return 0;
    }
    for (slot = 0U; slot < V5_NATIVE_READBACK_WCS_AXIS_COUNT; ++slot) {
        if (fabs(before_offsets[slot] - after_offsets[slot]) > 1.0e-9) {
            return 0;
        }
    }
    return 1;
}

int v5_native_axis_zero_position_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    int rc;
    if (!out || out_size == 0U || !request_ok(prepared, request)) {
        return 0;
    }
    rc = snprintf(out, out_size, "native_axis_zero_position %s %s0",
                  request->mode_value, request->text_value);
    return rc > 0 && (size_t)rc < out_size;
}

V5LinuxcncrshSendStatus v5_native_axis_zero_position_send(
    const V5LinuxcncrshConfig *config,
    const V5NativeMotionParameters *parameters,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5NativeAxisZeroPositionResult *result,
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeProgressCallback progress_cb,
    void *progress_user_data)
{
    const V5NativeMotionAxisParameters *axis_parameters;
    V5NativeReadback before_wcs;
    V5NativeReadback after_wcs;
    char axis;
    int relative;
    int moved = 0;
    double before_abs;
    double before_position;
    double initial_position;
    double after_position = 0.0;
    V5NativeHomeProgress progress;
    V5NativeWcheckpointSnapshot wcheckpoint;
    V5NativeSafeZeroPlan safe_plan;
    char owner_code[64] = "";
    double final_target = 0.0;

    memset(&progress, 0, sizeof(progress));
    progress.run_id = run_id;
    progress.generation = generation;
    progress.phase = V5_NATIVE_HOME_PHASE_PREPARING;
    if (progress_cb) progress_cb(&progress, progress_user_data);

    result_init(result);
    if (!request_ok(prepared, request)) {
        result_code(result, "AXIS_ZERO_REQUEST_INVALID");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    axis = request->text_value[0];
    relative = strcmp(request->mode_value, "wcs") == 0;
    snprintf(progress.mode, sizeof(progress.mode), "%s", relative ? "wcs" : "mcs");
    progress.current_axes[0] = axis;
    progress.current_axes[1] = '\0';
    progress.current_axis_mask = 1U << (axis == 'X' ? 0 : axis == 'Y' ? 1 : axis == 'Z' ? 2 : axis == 'A' ? 3 : axis == 'B' ? 4 : 5);
    axis_parameters = v5_native_motion_parameters_axis(parameters, axis);
    if (!axis_parameters) {
        result_code(result, "AXIS_ZERO_PARAMETERS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    progress.phase = V5_NATIVE_HOME_PHASE_RTCP_FORCE_OFF;
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", "HOME_RTCP_FORCE_OFF");
    if (progress_cb) progress_cb(&progress, progress_user_data);
    if (!v5_native_home_force_rtcp_off(owner_code, sizeof(owner_code))) {
        result_code(result, owner_code);
        progress.phase = V5_NATIVE_HOME_PHASE_FAILED;
        progress.terminal = 1;
        snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", owner_code);
        if (progress_cb) progress_cb(&progress, progress_user_data);
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    progress.phase = V5_NATIVE_HOME_PHASE_PREPARING;
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", "HOME_PREPARING");
    if (progress_cb) progress_cb(&progress, progress_user_data);
    if (!v5_linuxcncrsh_get_axis_position(config, axis, 0, &before_abs) ||
        !v5_linuxcncrsh_get_axis_position(config, axis, relative, &before_position)) {
        result_code(result, "AXIS_ZERO_START_POSITION_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    initial_position = before_position;
    if (relative && !read_wcs_snapshot(&before_wcs)) {
        result_code(result, "AXIS_ZERO_WCS_READBACK_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!relative && rotary_axis(axis)) {
        if (!read_safe_zero_snapshot(
                config, axis_parameters, &wcheckpoint, owner_code, sizeof(owner_code)) ||
            !v5_native_home_safe_zero_plan(axis_parameters, &wcheckpoint, &safe_plan, owner_code, sizeof(owner_code))) {
            result_code(result, owner_code);
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        final_target = (double)safe_plan.runtime_target_counts / axis_parameters->bus_counts_per_unit;
    }
    if (v5_linuxcncrsh_send_line(config, "Set Mode MDI") != V5_LINUXCNCRSH_SEND_SENT) {
        result_code(result, "AXIS_ZERO_MDI_MODE_REJECTED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (target_error(axis, before_position, final_target) <=
        V5_AXIS_ZERO_TARGET_TOLERANCE + V5_AXIS_ZERO_COMPARE_EPSILON) {
        double proof_delta = rotary_axis(axis) ? V5_AXIS_ZERO_ROTARY_PROOF : V5_AXIS_ZERO_LINEAR_PROOF;
        double proof_target;
        if (before_abs + proof_delta >= axis_parameters->max_limit) {
            proof_delta = -proof_delta;
        }
        proof_target = before_position + proof_delta;
        if (before_abs + proof_delta <= axis_parameters->min_limit ||
            before_abs + proof_delta >= axis_parameters->max_limit ||
            !send_target(config, axis, relative, proof_target) ||
            !wait_position(config, axis, relative, before_position, proof_target, &moved, 0,
                           0, 0, 0, 0U,
                           &progress, progress_cb, progress_user_data)) {
            result_code(result, "AXIS_ZERO_PROOF_MOVE_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        before_position = proof_target;
    }
    progress.phase = V5_NATIVE_HOME_PHASE_ZERO_RETURN;
    progress.detail_valid = 1;
    progress.actual = before_position;
    progress.target = final_target;
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", "HOME_ZERO_RETURN");
    if (progress_cb) progress_cb(&progress, progress_user_data);
    owner_code[0] = '\0';
    if (!send_target(config, axis, relative, final_target) ||
        !wait_position(config, axis, relative, before_position, final_target, &moved, &after_position,
                       (!relative && rotary_axis(axis)) ? &safe_plan : 0,
                       (!relative && rotary_axis(axis)) ? axis_parameters : 0,
                       owner_code, sizeof(owner_code),
                       &progress, progress_cb, progress_user_data)) {
        result_code(result,
                    (!relative && rotary_axis(axis) && owner_code[0])
                        ? owner_code
                        : "AXIS_ZERO_ARRIVAL_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!moved) {
        result_code(result, "AXIS_ZERO_REAL_MOVE_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (relative) {
        if (!read_wcs_snapshot(&after_wcs) ||
            !wcs_offset_same(&before_wcs, &after_wcs)) {
            result_code(result, "AXIS_ZERO_WCS_OFFSET_CHANGED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (result) {
            result->wcs_offset_unchanged = 1;
        }
    }
    if (result) {
        result->movement_confirmed = 1;
        result->arrival_confirmed = 1;
        result->before_position = relative ? initial_position : before_abs;
        result->after_position = after_position;
    }
    result_code(result, relative ? "WCS_AXIS_ZERO_MOVE_CONFIRMED" : "MCS_AXIS_ZERO_MOVE_CONFIRMED");
    progress.phase = V5_NATIVE_HOME_PHASE_COMPLETE;
    progress.terminal = 1;
    progress.detail_valid = 0;
    snprintf(progress.direct_reason, sizeof(progress.direct_reason), "%s", result ? result->code : "HOME_COMPLETE");
    if (progress_cb) progress_cb(&progress, progress_user_data);
    return V5_LINUXCNCRSH_SEND_SENT;
}
