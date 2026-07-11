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

static int settings_apply_joint_index_from_ini(const char *ini_path, const char *axis, unsigned int *joint_out)
{
    FILE *fp;
    char raw[512];
    char wanted;
    int in_traj = 0;
    if (!ini_path || !axis || strlen(axis) != 1U || !joint_out) {
        return 0;
    }
    wanted = (char)toupper((unsigned char)axis[0]);
    fp = fopen(ini_path, "rb");
    if (!fp) {
        return 0;
    }
    while (fgets(raw, sizeof(raw), fp)) {
        char probe[512];
        char section[64];
        char *eq;
        snprintf(probe, sizeof(probe), "%s", raw);
        v5_settings_apply_trim(probe);
        if (!probe[0] || probe[0] == '#' || probe[0] == ';') {
            continue;
        }
        if (v5_settings_apply_ini_section_line(probe, section, sizeof(section))) {
            in_traj = strcmp(section, "TRAJ") == 0;
            continue;
        }
        eq = strchr(probe, '=');
        if (!in_traj || !eq) {
            continue;
        }
        *eq = '\0';
        v5_settings_apply_trim(probe);
        v5_settings_apply_trim(eq + 1);
        if (strcmp(probe, "COORDINATES") == 0) {
            unsigned int joint = 0U;
            const char *p = eq + 1;
            while (*p) {
                if (isalpha((unsigned char)*p)) {
                    if ((char)toupper((unsigned char)*p) == wanted) {
                        *joint_out = joint;
                        fclose(fp);
                        return 1;
                    }
                    ++joint;
                }
                ++p;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int settings_apply_runtime_target(
    const char *ini_path,
    const char *axis,
    const char *field_key,
    unsigned int fallback_axis_index,
    char *primary_section,
    size_t primary_cap,
    char *mirror_section,
    size_t mirror_cap,
    const char **ini_key,
    unsigned int *joint_index_out)
{
    char axis_section[32];
    char joint_section[32];
    unsigned int joint_index = fallback_axis_index;
    int has_joint;
    const char *key = 0;
    int primary_is_joint = 0;
    int mirror_joint = 0;
    if (!ini_path || !axis || !field_key || !primary_section || primary_cap == 0U ||
        !mirror_section || mirror_cap == 0U || !ini_key) {
        return 0;
    }
    primary_section[0] = '\0';
    mirror_section[0] = '\0';
    *ini_key = 0;
    snprintf(axis_section, sizeof(axis_section), "AXIS_%s", axis);
    has_joint = settings_apply_joint_index_from_ini(ini_path, axis, &joint_index);
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
    unsigned int joint_index = request->axis_index;
    if (!v5_settings_apply_build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    if (!settings_apply_runtime_target(ini_path, request->axis, request->field_key, request->axis_index,
                                       primary_section, sizeof(primary_section),
                                       mirror_section, sizeof(mirror_section),
                                       &ini_key, &joint_index)) {
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
        if (!v5_settings_apply_scale_chain_commit(request->project_root, 0, request->axis, joint_index,
                                                  request->field_name, &result->scale_chain)) {
            return 0;
        }
    }
    return 1;
}
