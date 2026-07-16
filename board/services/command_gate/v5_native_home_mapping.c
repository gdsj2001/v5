#include "v5_native_home_mapping.h"

#include "v5_parameter_owner_map.h"
#include "v5_settings_apply_internal.h"
#include "v5_settings_parameter_store.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a_u32(uint32_t value, uint32_t hash)
{
    unsigned int byte;
    for (byte = 0U; byte < 4U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= 16777619U;
    }
    return hash;
}

int v5_native_home_mapping_load(
    const char *settings_project_root,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    unsigned int i;
    unsigned int active_mask = 0U;
    unsigned int slave_mask = 0U;
    unsigned int axis_by_slot[V5_NATIVE_MOTION_HOME_JOINT_COUNT] = {0U};
    unsigned int slave_by_slot[V5_NATIVE_MOTION_HOME_JOINT_COUNT] = {0U};
    uint32_t mapping_generation = 2166136261U;
    if (!settings_project_root || !settings_project_root[0] || !parameters ||
        !code || code_cap == 0U) {
        if (code && code_cap) {
            snprintf(code, code_cap, "%s", "BUS_HOME_AXIS_SLAVE_OWNER_REQUIRED");
        }
        return 0;
    }
    parameters->mapping_generation = 0U;
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        parameters->axes[i].slave_mapping_known = 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        char axis_name[2] = {axis->axis, '\0'};
        char binding[64];
        char *end = 0;
        unsigned long position;
        if (!axis->active) {
            continue;
        }
        if (axis->status_slot >= V5_NATIVE_MOTION_HOME_JOINT_COUNT ||
            (active_mask & (1U << axis->status_slot))) {
            snprintf(code, code_cap, "BUS_HOME_AXIS_%c_JOINT_INVALID", axis->axis);
            return 0;
        }
        if (!v5_settings_parameter_store_read_axis(
                settings_project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                axis_name, "slave", binding, sizeof(binding)) ||
            strcmp(binding, "NAT") == 0) {
            snprintf(code, code_cap, "BUS_HOME_AXIS_%c_SLAVE_UNBOUND", axis->axis);
            return 0;
        }
        position = strtoul(binding, &end, 10);
        if (end == binding || *end || position >= V5_NATIVE_MOTION_HOME_JOINT_COUNT) {
            snprintf(code, code_cap, "BUS_HOME_AXIS_%c_SLAVE_INVALID", axis->axis);
            return 0;
        }
        if (slave_mask & (1U << (unsigned int)position)) {
            snprintf(code, code_cap, "BUS_HOME_SLAVE_%lu_DUPLICATE", position);
            return 0;
        }
        axis->slave_position = (unsigned int)position;
        axis->slave_mapping_known = 1;
        active_mask |= 1U << axis->status_slot;
        slave_mask |= 1U << axis->slave_position;
        axis_by_slot[axis->status_slot] = (unsigned int)(unsigned char)axis->axis;
        slave_by_slot[axis->status_slot] = axis->slave_position;
    }
    if (parameters->active_axis_count != V5_NATIVE_MOTION_HOME_JOINT_COUNT ||
        active_mask != ((1U << V5_NATIVE_MOTION_HOME_JOINT_COUNT) - 1U) ||
        slave_mask != ((1U << V5_NATIVE_MOTION_HOME_JOINT_COUNT) - 1U)) {
        snprintf(code, code_cap, "%s", "BUS_HOME_MAPPING_INCOMPLETE");
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_HOME_JOINT_COUNT; ++i) {
        mapping_generation = fnv1a_u32(i, mapping_generation);
        mapping_generation = fnv1a_u32(axis_by_slot[i], mapping_generation);
        mapping_generation = fnv1a_u32(slave_by_slot[i], mapping_generation);
    }
    parameters->mapping_generation = mapping_generation ? mapping_generation : 1U;
    return 1;
}

int v5_native_home_runtime_owner_load_bus(
    const char *settings_project_root,
    const char *settings_runtime_json_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    char *json;
    unsigned int i;
    if (!v5_native_home_mapping_load(
            settings_project_root, parameters, code, code_cap)) {
        return 0;
    }
    if (!settings_runtime_json_path || !settings_runtime_json_path[0]) {
        snprintf(code, code_cap, "%s", "BUS_HOME_SETTINGS_RUNTIME_REQUIRED");
        return 0;
    }
    json = v5_settings_apply_read_text_file_limited(settings_runtime_json_path);
    if (!json) {
        snprintf(code, code_cap, "%s", "BUS_HOME_SETTINGS_RUNTIME_UNAVAILABLE");
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        const char *axis_start;
        const char *axis_end;
        const char *zero_start;
        const char *zero_end;
        char axis_name[2] = {axis->axis, '\0'};
        double expected;
        double reference_scale_epsilon;
        double zero_slave_position;
        if (!axis->active) {
            continue;
        }
        axis->bus_zero_evidence_known = 0;
        if (!v5_settings_apply_runtime_axis_object(json, axis_name, &axis_start, &axis_end) ||
            !v5_settings_apply_json_object_for_key(
                axis_start, axis_end, "zero_model", &zero_start, &zero_end) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "zero_counts", &axis->bus_zero_counts) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "counts_per_unit", &axis->bus_counts_per_unit) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "raw_zero_position", &axis->bus_home_reference) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "slave_position", &zero_slave_position) ||
            !isfinite(axis->bus_zero_counts) ||
            !isfinite(axis->bus_counts_per_unit) || axis->bus_counts_per_unit == 0.0 ||
            !isfinite(axis->bus_home_reference) || !isfinite(zero_slave_position)) {
            free(json);
            snprintf(code, code_cap, "BUS_HOME_AXIS_%c_ZERO_EVIDENCE_MISSING", axis->axis);
            return 0;
        }
        if (zero_slave_position < 0.0 || zero_slave_position > (double)UINT_MAX ||
            zero_slave_position != floor(zero_slave_position) ||
            (unsigned int)zero_slave_position != axis->slave_position) {
            free(json);
            snprintf(code, code_cap, "BUS_HOME_AXIS_%c_ZERO_SLAVE_MISMATCH", axis->axis);
            return 0;
        }
        expected = axis->bus_zero_counts / axis->bus_counts_per_unit;
        reference_scale_epsilon = fmax(1.0, fabs(expected)) * 1.0e-9;
        if (fabs(expected - axis->bus_home_reference) > reference_scale_epsilon) {
            free(json);
            snprintf(code, code_cap, "%s", "BUS_HOME_ZERO_EVIDENCE_MISMATCH");
            return 0;
        }
        axis->bus_zero_evidence_known = 1;
    }
    free(json);
    snprintf(parameters->pulse_contract_status, sizeof(parameters->pulse_contract_status), "%s", "not_applicable");
    parameters->runtime_owner_loaded = 1;
    snprintf(code, code_cap, "%s", "BUS_HOME_RUNTIME_OWNER_LOADED");
    return 1;
}
