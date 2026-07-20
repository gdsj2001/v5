#include "v5_native_axis_zero_live.h"

#include "v5_native_home_mapping.h"
#include "v5_native_safety.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define V5_AXIS_ZERO_LIVE_ATTEMPTS 20U
#define V5_AXIS_ZERO_LIVE_DELAY_US 10000U
#define V5_AXIS_ZERO_SCALE_EPSILON 1.0e-12

static void set_code(V5NativeAxisZeroLiveResult *result, const char *code)
{
    if (result) {
        snprintf(result->code, sizeof(result->code), "%s",
                 code ? code : "SETTINGS_AXIS_ZERO_LIVE_FAILED");
    }
}

void v5_native_axis_zero_live_result_init(V5NativeAxisZeroLiveResult *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    set_code(result, "SETTINGS_AXIS_ZERO_LIVE_NOT_ATTEMPTED");
}

static int parameters_compatible(
    const V5NativeMotionParameters *resident,
    const V5NativeMotionParameters *candidate,
    char axis,
    unsigned int expected_slave,
    const V5NativeMotionAxisParameters **selected_out,
    V5NativeAxisZeroLiveResult *result)
{
    unsigned int index;
    const V5NativeMotionAxisParameters *selected = 0;
    if (!resident || !candidate ||
        resident->driver_mode != V5_NATIVE_DRIVER_MODE_BUS ||
        candidate->driver_mode != V5_NATIVE_DRIVER_MODE_BUS ||
        !resident->mapping_generation ||
        candidate->mapping_generation != resident->mapping_generation) {
        set_code(result, "SETTINGS_AXIS_ZERO_MAPPING_CHANGED_RESTART_REQUIRED");
        return 0;
    }
    for (index = 0U; index < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++index) {
        const V5NativeMotionAxisParameters *before = &resident->axes[index];
        const V5NativeMotionAxisParameters *after = &candidate->axes[index];
        if (before->active != after->active || before->status_slot != after->status_slot ||
            before->slave_mapping_known != after->slave_mapping_known ||
            before->slave_position != after->slave_position ||
            fabs(before->positioning_resolution_units - after->positioning_resolution_units) >
                V5_AXIS_ZERO_SCALE_EPSILON) {
            set_code(result, "SETTINGS_AXIS_ZERO_SCALE_OR_MAPPING_CHANGED_RESTART_REQUIRED");
            return 0;
        }
        if (after->active && after->axis == axis) selected = after;
    }
    if (!selected || !selected->slave_mapping_known ||
        selected->slave_position != expected_slave ||
        !selected->bus_zero_evidence_known ||
        !isfinite(selected->positioning_resolution_units) ||
        selected->positioning_resolution_units <= 0.0) {
        set_code(result, "SETTINGS_AXIS_ZERO_LIVE_AXIS_READBACK_MISMATCH");
        return 0;
    }
    if (selected_out) *selected_out = selected;
    return 1;
}

static void update_resident_zero_fields(
    V5NativeMotionParameters *resident,
    const V5NativeMotionParameters *candidate)
{
    unsigned int index;
    if (!resident || !candidate) return;
    for (index = 0U; index < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++index) {
        resident->axes[index].bus_zero_counts = candidate->axes[index].bus_zero_counts;
        resident->axes[index].bus_counts_per_unit = candidate->axes[index].bus_counts_per_unit;
        resident->axes[index].bus_home_reference = candidate->axes[index].bus_home_reference;
        resident->axes[index].bus_zero_evidence_known =
            candidate->axes[index].bus_zero_evidence_known;
    }
    resident->runtime_owner_loaded = candidate->runtime_owner_loaded;
}

int v5_native_axis_zero_live_apply(
    const V5LinuxcncrshConfig *linuxcncrsh,
    const char *ini_path,
    const char *settings_project_root,
    const char *settings_runtime_path,
    const char *pulse_contract_path,
    V5NativeMotionParameters *resident_parameters,
    char axis,
    unsigned int expected_slave_position,
    double expected_mcs_position,
    V5NativeAxisZeroLiveResult *result)
{
    V5NativeMotionParameters candidate;
    V5NativeSafetyResult safety;
    const V5NativeMotionAxisParameters *selected = 0;
    char code[64];
    unsigned int attempt;
    unsigned int stable = 0U;
    double mcs = 0.0;
    v5_native_axis_zero_live_result_init(result);
    if (!result || !linuxcncrsh || !resident_parameters ||
        !isfinite(expected_mcs_position) ||
        expected_slave_position >= V5_NATIVE_MOTION_HOME_JOINT_COUNT) {
        set_code(result, "SETTINGS_AXIS_ZERO_LIVE_REQUEST_INVALID");
        return 0;
    }
    if (v5_native_safety_read_status(&safety) != V5_NATIVE_SAFETY_SEND_SENT ||
        !safety.machine_enable_known || safety.machine_enabled) {
        set_code(result, "SETTINGS_AXIS_ZERO_MACHINE_OFF_REQUIRED");
        return 0;
    }
    if (!v5_native_motion_parameters_load(
            ini_path, &candidate, code, sizeof(code)) ||
        !v5_native_motion_parameters_load_runtime_owner(
            settings_project_root, settings_runtime_path, pulse_contract_path,
            &candidate, code, sizeof(code))) {
        set_code(result, code[0] ? code : "SETTINGS_AXIS_ZERO_RUNTIME_OWNER_RELOAD_FAILED");
        return 0;
    }
    if (!parameters_compatible(
            resident_parameters, &candidate, axis, expected_slave_position,
            &selected, result)) {
        return 0;
    }
    if (!v5_linuxcncrsh_get_joint_position(
            linuxcncrsh, selected->status_slot, &result->previous_mcs_position) ||
        !isfinite(result->previous_mcs_position)) {
        set_code(result, "SETTINGS_AXIS_ZERO_PREVIOUS_MCS_UNAVAILABLE");
        return 0;
    }
    if (!v5_native_home_mapping_project(
            &candidate, &result->commit_seq, code, sizeof(code))) {
        set_code(result, code[0] ? code : "SETTINGS_AXIS_ZERO_LIVE_COMMIT_FAILED");
        return 0;
    }
    result->applied = 1;
    result->tolerance_units = selected->positioning_resolution_units;
#ifdef _WIN32
    set_code(result, "SETTINGS_AXIS_ZERO_MCS_READBACK_UNAVAILABLE");
    return 0;
#else
    for (attempt = 0U; attempt < V5_AXIS_ZERO_LIVE_ATTEMPTS; ++attempt) {
        if (v5_linuxcncrsh_get_joint_position(
                linuxcncrsh, selected->status_slot, &mcs) && isfinite(mcs) &&
            fabs(mcs - expected_mcs_position) <= result->tolerance_units) {
            if (++stable >= 2U) {
                result->mcs_position = mcs;
                result->display_verified = 1;
                update_resident_zero_fields(resident_parameters, &candidate);
                set_code(result, "SETTINGS_COUNT_DOMAIN_ZERO_LIVE_APPLIED_RESTART_REQUIRED");
                return 1;
            }
        } else {
            stable = 0U;
        }
        usleep(V5_AXIS_ZERO_LIVE_DELAY_US);
    }
    result->mcs_position = mcs;
    set_code(result, "SETTINGS_AXIS_ZERO_FRESH_MCS_NOT_EXPECTED");
    return 0;
#endif
}
