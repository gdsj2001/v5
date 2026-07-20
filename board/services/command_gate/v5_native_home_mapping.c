#include "v5_native_home_mapping.h"

#include "v5_native_hal_owner_client.h"
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

typedef struct V5BusZeroEvidence {
    double zero_counts;
    double counts_per_unit;
    double home_reference;
} V5BusZeroEvidence;

static int v5_native_home_zero_evidence_for_slave(
    const char *json,
    const V5NativeMotionParameters *parameters,
    unsigned int expected_slave_position,
    V5BusZeroEvidence *evidence)
{
    unsigned int i;
    int found = 0;
    if (!json || !parameters || !evidence) {
        return 0;
    }
    memset(evidence, 0, sizeof(*evidence));
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        const V5NativeMotionAxisParameters *candidate = &parameters->axes[i];
        const char *axis_start;
        const char *axis_end;
        const char *zero_start;
        const char *zero_end;
        char axis_name[2] = {candidate->axis, '\0'};
        double zero_slave_position;
        V5BusZeroEvidence candidate_evidence;
        if (!v5_settings_apply_runtime_axis_object(
                json, axis_name, &axis_start, &axis_end) ||
            !v5_settings_apply_json_object_for_key(
                axis_start, axis_end, "zero_model", &zero_start, &zero_end) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "slave_position", &zero_slave_position) ||
            !isfinite(zero_slave_position) ||
            zero_slave_position < 0.0 ||
            zero_slave_position > (double)UINT_MAX ||
            zero_slave_position != floor(zero_slave_position) ||
            (unsigned int)zero_slave_position != expected_slave_position) {
            continue;
        }
        if (found ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "zero_counts", &candidate_evidence.zero_counts) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "counts_per_unit", &candidate_evidence.counts_per_unit) ||
            !v5_settings_apply_json_number_value(
                zero_start, zero_end, "raw_zero_position", &candidate_evidence.home_reference) ||
            !isfinite(candidate_evidence.zero_counts) ||
            !isfinite(candidate_evidence.counts_per_unit) ||
            candidate_evidence.counts_per_unit == 0.0 ||
            !isfinite(candidate_evidence.home_reference)) {
            return 0;
        }
        *evidence = candidate_evidence;
        found = 1;
    }
    return found;
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
    unsigned int loaded_zero_count = 0U;
    if (!v5_native_home_mapping_load(
            settings_project_root, parameters, code, code_cap)) {
        return 0;
    }
    if (!settings_runtime_json_path || !settings_runtime_json_path[0]) {
        snprintf(code, code_cap, "%s", "BUS_HOME_SETTINGS_RUNTIME_REQUIRED");
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        axis->bus_zero_evidence_known = 0;
        axis->bus_zero_counts = 0.0;
        axis->bus_counts_per_unit = 0.0;
        axis->bus_home_reference = 0.0;
    }
    json = v5_settings_apply_read_text_file_limited(settings_runtime_json_path);
    if (!json) {
        snprintf(code, code_cap, "%s", "BUS_HOME_SETTINGS_RUNTIME_UNAVAILABLE");
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        V5BusZeroEvidence evidence;
        double expected;
        double reference_scale_epsilon;
        if (!axis->active) {
            continue;
        }
        if (!v5_native_home_zero_evidence_for_slave(
                json, parameters, axis->slave_position, &evidence)) {
            continue;
        }
        axis->bus_zero_counts = evidence.zero_counts;
        axis->bus_counts_per_unit = evidence.counts_per_unit;
        axis->bus_home_reference = evidence.home_reference;
        expected = axis->bus_zero_counts / axis->bus_counts_per_unit;
        reference_scale_epsilon = fmax(1.0, fabs(expected)) * 1.0e-9;
        if (fabs(expected - axis->bus_home_reference) > reference_scale_epsilon) {
            axis->bus_zero_counts = 0.0;
            axis->bus_counts_per_unit = 0.0;
            axis->bus_home_reference = 0.0;
            continue;
        }
        axis->bus_zero_evidence_known = 1;
        ++loaded_zero_count;
    }
    free(json);
    snprintf(parameters->pulse_contract_status, sizeof(parameters->pulse_contract_status), "%s", "not_applicable");
    parameters->runtime_owner_loaded = 1;
    snprintf(
        code,
        code_cap,
        "%s",
        loaded_zero_count == parameters->active_axis_count
            ? "BUS_HOME_RUNTIME_OWNER_LOADED"
            : "BUS_HOME_RUNTIME_OWNER_LOADED_PARTIAL");
    return 1;
}

int v5_native_home_mapping_project(
    const V5NativeMotionParameters *parameters,
    unsigned int *commit_seq_out,
    char *code,
    size_t code_cap)
{
    unsigned int index;
    unsigned int expected_active_mask = 0U;
    unsigned int home_ready_mask = 0U;
    unsigned int commit_seq;
    const unsigned int full_mask = (1U << V5_NATIVE_HOME_JOINT_COUNT) - 1U;
    V5NativeHomeConfigRecord records[V5_NATIVE_HOME_JOINT_COUNT];
    V5NativeHalOwnerResponse response;
    if (commit_seq_out) *commit_seq_out = 0U;
    if (!parameters || parameters->driver_mode != V5_NATIVE_DRIVER_MODE_BUS ||
        !parameters->mapping_generation) {
        snprintf(code, code_cap, "%s", "NATIVE_BUS_MAPPING_NOT_RESIDENT");
        return 0;
    }
    memset(records, 0, sizeof(records));
    for (index = 0U; index < V5_NATIVE_HOME_JOINT_COUNT; ++index) {
        records[index].joint = index;
        records[index].status_slot = index;
        records[index].slave_position = UINT32_MAX;
    }
    for (index = 0U; index < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++index) {
        const V5NativeMotionAxisParameters *axis = &parameters->axes[index];
        V5NativeHomeConfigRecord *record;
        if (!axis->active) continue;
        if (axis->status_slot >= V5_NATIVE_HOME_JOINT_COUNT ||
            !axis->slave_mapping_known ||
            axis->slave_position >= V5_NATIVE_HOME_JOINT_COUNT ||
            !isfinite(axis->positioning_resolution_units) ||
            axis->positioning_resolution_units <= 0.0 ||
            (expected_active_mask & (1U << axis->status_slot))) {
            snprintf(code, code_cap, "NATIVE_BUS_AXIS_%c_MAPPING_FAILED", axis->axis ? axis->axis : '?');
            return 0;
        }
        record = &records[axis->status_slot];
        record->active = 1U;
        record->axis_code = (unsigned int)(unsigned char)axis->axis;
        record->slave_position = axis->slave_position;
        record->mapping_generation = parameters->mapping_generation;
        record->home_ready = axis->bus_zero_evidence_known ? 1 : 0;
        record->zero_counts = axis->bus_zero_evidence_known ? axis->bus_zero_counts : 0.0;
        record->counts_per_unit = axis->bus_zero_evidence_known
            ? axis->bus_counts_per_unit
            : 1.0 / axis->positioning_resolution_units;
        if (!isfinite(record->zero_counts) ||
            !isfinite(record->counts_per_unit) || record->counts_per_unit <= 0.0) {
            snprintf(code, code_cap, "NATIVE_BUS_AXIS_%c_SCALE_FAILED", axis->axis ? axis->axis : '?');
            return 0;
        }
        expected_active_mask |= 1U << axis->status_slot;
        if (record->home_ready) home_ready_mask |= 1U << axis->status_slot;
    }
    if (expected_active_mask != full_mask ||
        v5_native_hal_owner_exchange(
            V5_NATIVE_HAL_OWNER_OP_HOME_STATUS, 0U, 100U,
            &response) != V5_NATIVE_HAL_OWNER_CLIENT_OK) {
        snprintf(code, code_cap, "%s", "NATIVE_HOME_CONFIG_TABLE_INCOMPLETE");
        return 0;
    }
    commit_seq = response.home_config_commit_seq + 1U;
    if (!commit_seq) commit_seq = 1U;
    for (index = 0U; index < V5_NATIVE_HOME_JOINT_COUNT; ++index) {
        records[index].expected_active_mask = expected_active_mask;
        records[index].commit_seq = commit_seq;
        if (v5_native_hal_owner_stage_home_joint(
                &records[index], index + 1U == V5_NATIVE_HOME_JOINT_COUNT,
                100U, &response) != V5_NATIVE_HAL_OWNER_CLIENT_OK) {
            snprintf(code, code_cap, "NATIVE_HOME_JOINT_%u_CONFIG_FAILED", index);
            return 0;
        }
    }
    if (!response.home_config_readback_valid ||
        response.home_config_mask != home_ready_mask ||
        response.home_config_active_mask != expected_active_mask ||
        response.home_mapping_generation != parameters->mapping_generation ||
        response.home_config_commit_seq != commit_seq ||
        !response.status_home_router_mapping_valid ||
        response.status_home_router_mapping_generation != parameters->mapping_generation ||
        response.status_home_router_active_mask != expected_active_mask ||
        response.status_home_router_commit_seq != commit_seq ||
        response.status_home_router_rejected_commit_seq == commit_seq) {
        snprintf(code, code_cap, "%s", "NATIVE_BUS_MAPPING_READBACK_MISMATCH");
        return 0;
    }
    if (commit_seq_out) *commit_seq_out = commit_seq;
    snprintf(code, code_cap, "%s", "NATIVE_BUS_MAPPING_PROJECTED");
    return 1;
}
