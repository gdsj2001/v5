#include "v5_settings_apply.h"

#include "v5_parameter_owner_map.h"
#include "v5_motion_model_registry.h"
#include "v5_settings_parameter_store.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "v5_settings_apply_internal.h"

static int axis_is_rotary(const char *axis)
{
    return axis && axis[1] == '\0' && (axis[0] == 'A' || axis[0] == 'B' || axis[0] == 'C');
}

static int compute_scale_from_chain(
    const char *ini_path,
    const char *axis_section,
    const char *joint_section,
    const char *axis_obj_start,
    const char *axis_obj_end,
    const char *axis,
    double *scale_out)
{
    double pitch = 0.0;
    double motor_rev = 0.0;
    double load_rev = 0.0;
    double ratio = 0.0;
    double counts = 0.0;
    double rotary_counts = 0.0;
    if (!scale_out) {
        return 0;
    }
    if (v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "motor_revs_per_load_rev", &ratio) ||
        v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "reducer_ratio", &ratio)) {
        /* ratio loaded from drive-only evidence */
    }
    if (v5_settings_apply_ini_read_preferred_number(ini_path, axis_section, joint_section, "MOTOR_REV", &motor_rev) &&
        v5_settings_apply_ini_read_preferred_number(ini_path, axis_section, joint_section, "LOAD_REV", &load_rev) &&
        motor_rev > 0.0 && load_rev > 0.0) {
        ratio = motor_rev / load_rev;
    }
    if (axis_is_rotary(axis)) {
        if (v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "rotary_load_counts_per_rev", &rotary_counts) &&
            rotary_counts > 0.0) {
            *scale_out = rotary_counts / 360.0;
            return isfinite(*scale_out) && *scale_out > 0.0;
        }
    } else {
        if (!v5_settings_apply_ini_read_preferred_number(ini_path, axis_section, joint_section, "PITCH", &pitch) || pitch <= 0.0) {
            return 0;
        }
    }
    if (!(v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "actual_counts_per_motor_rev", &counts) ||
          v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "drive_command_counts_per_motor_rev", &counts) ||
          v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "target_command_counts_per_motor_rev", &counts) ||
          v5_settings_apply_json_number_value(axis_obj_start, axis_obj_end, "feedback_counts_per_motor_rev", &counts)) ||
        counts <= 0.0 || ratio <= 0.0) {
        return 0;
    }
    *scale_out = axis_is_rotary(axis) ? (counts * ratio) / 360.0 : (counts * ratio) / pitch;
    return isfinite(*scale_out) && *scale_out > 0.0;
}

static int commit_wcheckpoint_profile(
    const char *ini_path,
    const char *axis_section,
    const char *axis_obj_start,
    const char *axis_obj_end)
{
    static const char *const json_keys[] = {
        "position_command_raw_bits",
        "position_feedback_raw_bits",
        "position_command_raw_signed",
        "position_feedback_raw_signed",
        "position_command_raw_modulus",
        "position_feedback_raw_modulus",
        "rotary_load_counts_per_rev",
        "drive_wrapped_rotary_support_flag",
    };
    static const char *const ini_keys[] = {
        "WCHECKPOINT_COMMAND_RAW_BITS",
        "WCHECKPOINT_FEEDBACK_RAW_BITS",
        "WCHECKPOINT_COMMAND_RAW_SIGNED",
        "WCHECKPOINT_FEEDBACK_RAW_SIGNED",
        "WCHECKPOINT_COMMAND_MODULUS_HI",
        "WCHECKPOINT_COMMAND_MODULUS_LO",
        "WCHECKPOINT_FEEDBACK_MODULUS_HI",
        "WCHECKPOINT_FEEDBACK_MODULUS_LO",
        "WCHECKPOINT_COUNTS_PER_REV",
        "WCHECKPOINT_DRIVE_PERIODIC",
    };
    V5IniTextUpdate updates[10];
    char values[10][32];
    double mapped[8];
    double command_hi;
    double feedback_hi;
    size_t i;

    for (i = 0U; i < 8U; ++i) {
        if (!v5_settings_apply_json_number_value(
                axis_obj_start, axis_obj_end, json_keys[i], &mapped[i]) ||
            !isfinite(mapped[i]) || mapped[i] < 0.0) {
            return 0;
        }
    }
    if (mapped[0] < 2.0 || mapped[1] < 2.0 ||
        mapped[2] > 1.0 || mapped[3] > 1.0 ||
        mapped[4] <= 0.0 || mapped[5] <= 0.0 || mapped[6] <= 0.0 ||
        mapped[7] > 1.0) {
        return 0;
    }
    command_hi = floor(mapped[4] / 4294967296.0);
    feedback_hi = floor(mapped[5] / 4294967296.0);
    snprintf(values[0], sizeof(values[0]), "%.0f", mapped[0]);
    snprintf(values[1], sizeof(values[1]), "%.0f", mapped[1]);
    snprintf(values[2], sizeof(values[2]), "%.0f", mapped[2]);
    snprintf(values[3], sizeof(values[3]), "%.0f", mapped[3]);
    snprintf(values[4], sizeof(values[4]), "%.0f", command_hi);
    snprintf(values[5], sizeof(values[5]), "%.0f", mapped[4] - command_hi * 4294967296.0);
    snprintf(values[6], sizeof(values[6]), "%.0f", feedback_hi);
    snprintf(values[7], sizeof(values[7]), "%.0f", mapped[5] - feedback_hi * 4294967296.0);
    snprintf(values[8], sizeof(values[8]), "%.0f", mapped[6]);
    snprintf(values[9], sizeof(values[9]), "%.0f", mapped[7]);
    memset(updates, 0, sizeof(updates));
    for (i = 0U; i < 10U; ++i) {
        updates[i].section = axis_section;
        updates[i].key = ini_keys[i];
        updates[i].value = values[i];
    }
    return v5_settings_apply_ini_write_text_updates(ini_path, updates, 10U) &&
        v5_settings_apply_ini_updates_readback_match(ini_path, updates, 10U);
}

int v5_settings_apply_scale_chain_commit(
    const char *project_root,
    const char *settings_runtime_json_path,
    const char *axis,
    unsigned int axis_index,
    const char *field_name,
    V5SettingsApplyScaleChainResult *result)
{
    char ini_path[512];
    char axis_section[32];
    char joint_section[32];
    const char *runtime_path;
    char *json;
    const char *axis_obj_start = 0;
    const char *axis_obj_end = 0;
    const char *zero_obj_start = 0;
    const char *zero_obj_end = 0;
    double zero_counts = 0.0;
    double old_zero = 0.0;
    double current_scale = 0.0;
    double effective_scale = 0.0;
    double chain_scale = 0.0;
    double raw_min_current = 0.0;
    double raw_max_current = 0.0;
    double min_distance;
    double max_distance;
    double new_zero;
    double new_min;
    double new_max;
    char *original_ini = 0;
    int precision_field;
    int write_scale = 0;
    int min_limit_disabled;
    int max_limit_disabled;

    if (result) {
        memset(result, 0, sizeof(*result));
        v5_settings_apply_scale_chain_result_code(result, "SCALE_CHAIN_NOT_ATTEMPTED");
    }
    if (!axis || !axis[0] || !field_name ||
        !v5_settings_apply_field_is_scale_chain(field_name)) {
        if (result) {
            result->skipped = 1;
            v5_settings_apply_scale_chain_result_code(result, "SCALE_CHAIN_NOT_REQUIRED");
        }
        return 1;
    }
    if (result) {
        result->attempted = 1;
    }
    if (!v5_settings_apply_build_runtime_ini_path(ini_path, sizeof(ini_path), project_root)) {
        v5_settings_apply_scale_chain_result_code(result, "RUNTIME_INI_PATH_INVALID");
        return 0;
    }
    runtime_path = settings_runtime_json_path;
    if (!runtime_path || !runtime_path[0]) {
        runtime_path = getenv("V5_SETTINGS_RUNTIME_JSON");
    }
    if (!runtime_path || !runtime_path[0]) {
        runtime_path = V5_SETTINGS_RUNTIME_JSON_DEFAULT;
    }
    if (!v5_settings_apply_file_exists(runtime_path)) {
        if (result) {
            result->skipped = 1;
            v5_settings_apply_scale_chain_result_code(result, "SETTINGS_RUNTIME_ZERO_MODEL_ABSENT");
        }
        return 1;
    }
    json = v5_settings_apply_read_text_file_limited(runtime_path);
    if (!json) {
        v5_settings_apply_scale_chain_result_code(result, "SETTINGS_RUNTIME_READ_FAILED");
        return 0;
    }
    snprintf(axis_section, sizeof(axis_section), "AXIS_%s", axis);
    snprintf(joint_section, sizeof(joint_section), "JOINT_%u", axis_index);
    if (!v5_settings_apply_runtime_axis_object(json, axis, &axis_obj_start, &axis_obj_end) ||
        !v5_settings_apply_json_object_for_key(
            axis_obj_start, axis_obj_end, "zero_model", &zero_obj_start, &zero_obj_end)) {
        free(json);
        if (result) {
            result->skipped = 1;
            v5_settings_apply_scale_chain_result_code(result, "SETTINGS_RUNTIME_ZERO_MODEL_ABSENT");
        }
        return 1;
    }
    if (result) {
        result->zero_model_present = 1;
    }
    if (!(v5_settings_apply_json_number_value(zero_obj_start, zero_obj_end, "zero_anchor_counts", &zero_counts) ||
          v5_settings_apply_json_number_value(zero_obj_start, zero_obj_end, "actual_counts", &zero_counts) ||
          v5_settings_apply_json_number_value(zero_obj_start, zero_obj_end, "zero_counts", &zero_counts) ||
          v5_settings_apply_json_number_value(zero_obj_start, zero_obj_end, "actual_position_counts", &zero_counts))) {
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "ZERO_MODEL_COUNTS_MISSING");
        return 0;
    }
    if (!v5_settings_apply_ini_read_preferred_number(ini_path, joint_section, axis_section, "SCALE", &current_scale) ||
        current_scale <= 0.0 || !isfinite(current_scale)) {
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "RUNTIME_SCALE_MISSING");
        return 0;
    }
    effective_scale = current_scale;
    precision_field = strstr(field_name, "_precision") != 0;
    if (!precision_field &&
        compute_scale_from_chain(ini_path, axis_section, joint_section, axis_obj_start, axis_obj_end, axis, &chain_scale)) {
        effective_scale = chain_scale;
        write_scale = fabs(chain_scale - current_scale) > 1.0e-9;
    }
    if (!v5_settings_apply_json_number_value(zero_obj_start, zero_obj_end, "raw_zero_position", &old_zero)) {
        old_zero = zero_counts / current_scale;
    }
    if (!(v5_settings_apply_ini_read_section_number(
              ini_path, joint_section, "MIN_LIMIT", &raw_min_current) &&
          v5_settings_apply_ini_read_section_number(
              ini_path, joint_section, "MAX_LIMIT", &raw_max_current)) &&
        !(v5_settings_apply_ini_read_section_number(
              ini_path, axis_section, "MIN_LIMIT", &raw_min_current) &&
          v5_settings_apply_ini_read_section_number(
              ini_path, axis_section, "MAX_LIMIT", &raw_max_current))) {
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "RAW_LIMIT_CURRENT_MISSING");
        return 0;
    }
    min_limit_disabled = raw_min_current == 0.0;
    max_limit_disabled = raw_max_current == 0.0;
    if ((!min_limit_disabled && old_zero < raw_min_current) ||
        (!max_limit_disabled && old_zero > raw_max_current)) {
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "RAW_ZERO_OUTSIDE_LIMITS");
        return 0;
    }
    min_distance = min_limit_disabled ? 0.0 :
        v5_settings_apply_nearest_integer(raw_min_current - old_zero);
    max_distance = max_limit_disabled ? 0.0 :
        v5_settings_apply_nearest_integer(raw_max_current - old_zero);
    new_zero = zero_counts / effective_scale;
    new_min = min_limit_disabled ? 0.0 :
        v5_settings_apply_nearest_integer(new_zero + min_distance);
    new_max = max_limit_disabled ? 0.0 :
        v5_settings_apply_nearest_integer(new_zero + max_distance);
    if (!isfinite(new_zero) || !isfinite(new_min) || !isfinite(new_max) ||
        (!min_limit_disabled && !max_limit_disabled && new_min >= new_max)) {
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "RAW_LIMIT_RECOMPUTE_INVALID");
        return 0;
    }
    original_ini = v5_settings_apply_read_text_file_limited(ini_path);
    if (!original_ini ||
        !v5_settings_apply_ini_write_scale_and_limits(ini_path, axis_section, joint_section, write_scale, effective_scale, new_min, new_max)) {
        free(original_ini);
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "RAW_LIMIT_WRITE_FAILED");
        return 0;
    }
    if (axis_is_rotary(axis) &&
        !commit_wcheckpoint_profile(ini_path, axis_section, axis_obj_start, axis_obj_end)) {
        (void)v5_settings_apply_write_text_file_atomic(ini_path, original_ini);
        free(original_ini);
        free(json);
        v5_settings_apply_scale_chain_result_code(result, "WCHECKPOINT_PROFILE_WRITE_FAILED");
        return 0;
    }
    free(original_ini);
    free(json);
    if (result) {
        result->scale_recomputed = write_scale ? 1 : 0;
        result->raw_limits_recomputed = 1;
        result->effective_scale = effective_scale;
        result->raw_zero_position = new_zero;
        result->raw_min_limit = new_min;
        result->raw_max_limit = new_max;
        v5_settings_apply_scale_chain_result_code(result, "SCALE_CHAIN_RAW_LIMITS_RECOMPUTED");
    }
    return 1;
}
