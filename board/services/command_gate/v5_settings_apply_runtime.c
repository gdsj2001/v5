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

static int settings_apply_active_model_from_ini(
    const char *ini_path,
    const V5MotionModelDescriptor **model_out)
{
    char model_text[64];
    char kins_module[64];
    char kins_coordinates[64];
    char traj_coordinates[64];
    const V5MotionModelDescriptor *model;

    if (model_out) {
        *model_out = 0;
    }
    if (!ini_path || !model_out ||
        !v5_settings_apply_ini_read_section_text(
            ini_path, "RTCP", "MODEL", model_text, sizeof(model_text)) ||
        !v5_settings_apply_ini_read_section_text(
            ini_path, "RTCP", "KINS_MODULE", kins_module, sizeof(kins_module)) ||
        !v5_settings_apply_ini_read_section_text(
            ini_path, "RTCP", "KINS_COORDINATES", kins_coordinates, sizeof(kins_coordinates)) ||
        !v5_settings_apply_ini_read_section_text(
            ini_path, "TRAJ", "COORDINATES", traj_coordinates, sizeof(traj_coordinates))) {
        return 0;
    }
    model = v5_motion_model_find(model_text);
    if (!v5_motion_model_descriptor_valid(model) ||
        strcmp(kins_module, model->kins_module) != 0 ||
        strcmp(kins_coordinates, model->kins_coordinates) != 0 ||
        strcmp(traj_coordinates, model->traj_coordinates) != 0) {
        return 0;
    }
    *model_out = model;
    return 1;
}

static int settings_apply_runtime_target(
    const char *ini_path,
    const char *axis,
    const char *field_key,
    char *primary_section,
    size_t primary_cap,
    char *mirror_section,
    size_t mirror_cap,
    const char **ini_key,
    unsigned int *joint_index_out,
    int *has_joint_out)
{
    const V5MotionModelDescriptor *model = 0;
    char axis_section[32];
    char joint_section[32];
    unsigned int joint_index = 0U;
    int has_joint = 0;
    const char *key = 0;
    int primary_is_joint = 0;
    int mirror_joint = 0;
    if (!ini_path || !axis || !field_key || !primary_section || primary_cap == 0U ||
        !mirror_section || mirror_cap == 0U || !ini_key || !has_joint_out) {
        return 0;
    }
    primary_section[0] = '\0';
    mirror_section[0] = '\0';
    *ini_key = 0;
    *has_joint_out = 0;
    snprintf(axis_section, sizeof(axis_section), "AXIS_%s", axis);
    if (!settings_apply_active_model_from_ini(ini_path, &model)) {
        return 0;
    }
    has_joint = axis[1] == '\0' &&
        v5_motion_model_status_slot_for_axis(model, axis[0], &joint_index);
    if (has_joint) {
        snprintf(joint_section, sizeof(joint_section), "JOINT_%u", joint_index);
    } else {
        joint_section[0] = '\0';
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        key = "TYPE";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "direction_mode") == 0) {
        key = "DIRECTION_MODE";
    } else if (strcmp(field_key, "precision") == 0) {
        key = "SCALE";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "pitch") == 0) {
        key = "PITCH";
    } else if (strcmp(field_key, "motor_rev") == 0) {
        key = "MOTOR_REV";
    } else if (strcmp(field_key, "load_rev") == 0) {
        key = "LOAD_REV";
    } else if (strcmp(field_key, "home_order") == 0) {
        key = "HOME_SEQUENCE";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "home_direction") == 0) {
        key = "HOME_SEARCH_VEL";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "soft_minus") == 0) {
        key = "MIN_LIMIT";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "soft_plus") == 0) {
        key = "MAX_LIMIT";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "max_velocity") == 0) {
        key = "MAX_VELOCITY";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "max_acceleration") == 0) {
        key = "MAX_ACCELERATION";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "backlash") == 0) {
        key = "BACKLASH";
        mirror_joint = has_joint;
    } else {
        return 0;
    }
    snprintf(primary_section, primary_cap, "%s", primary_is_joint ? joint_section : axis_section);
    if (mirror_joint && has_joint) {
        snprintf(mirror_section, mirror_cap, "%s", joint_section);
    }
    *ini_key = key;
    if (joint_index_out) {
        *joint_index_out = joint_index;
    }
    *has_joint_out = has_joint;
    return primary_section[0] != '\0' && *ini_key != 0;
}

int v5_settings_apply_commit_runtime_ini(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    char ini_path[512];
    char primary_section[32];
    char mirror_section[32];
    const char *ini_key;
    char ini_value[64];
    char expected_display[64];
    char raw[64];
    char display[64];
    unsigned int joint_index = 0U;
    int has_joint = 0;
    if (!v5_settings_apply_build_runtime_ini_path(
            ini_path, sizeof(ini_path), request->project_root, request->runtime_ini_path)) {
        return 0;
    }
    if (!settings_apply_runtime_target(ini_path, request->axis, request->field_key,
                                       primary_section, sizeof(primary_section),
                                       mirror_section, sizeof(mirror_section),
                                       &ini_key, &joint_index, &has_joint)) {
        return 0;
    }
    if (!v5_settings_apply_ini_value_for_field(request->field_key, request->value_text,
                                            ini_value, sizeof(ini_value),
                                            expected_display, sizeof(expected_display))) {
        return 0;
    }
    if (!v5_settings_apply_ini_write_section_text(ini_path, primary_section, ini_key, ini_value, 0, raw, sizeof(raw))) {
        return 0;
    }
    if (mirror_section[0] && strcmp(mirror_section, primary_section) != 0) {
        char mirror_readback[64];
        if (!v5_settings_apply_ini_write_section_text(ini_path, mirror_section, ini_key, ini_value, 0,
                                    mirror_readback, sizeof(mirror_readback))) {
            return 0;
        }
    }
    if (!v5_settings_apply_ini_read_section_text(ini_path, primary_section, ini_key, raw, sizeof(raw))) {
        return 0;
    }
    if (!v5_settings_apply_display_from_raw(request->field_key, raw, display, sizeof(display))) {
        return 0;
    }
    if (!v5_settings_apply_display_values_match(request->field_key, display, expected_display)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", display);
    }
    if (result && result->apply.raw_limits_recompute_required) {
        if (!v5_settings_apply_scale_chain_commit(
                request->project_root,
                request->runtime_ini_path,
                0,
                request->axis,
                has_joint, joint_index,
                request->field_name, &result->scale_chain)) {
            return 0;
        }
    }
    return 1;
}
