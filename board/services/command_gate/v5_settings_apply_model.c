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

static int settings_apply_motion_model_values(
    const char *value_text,
    char *canonical,
    size_t canonical_cap,
    char *display,
    size_t display_cap,
    char *kins_module,
    size_t kins_module_cap,
    char *kins_coordinates,
    size_t kins_coordinates_cap,
    char *traj_coordinates,
    size_t traj_coordinates_cap,
    unsigned int *wrapped_rotary_mask)
{
    const V5MotionModelDescriptor *model;
    if (!value_text || !value_text[0] || !canonical || canonical_cap == 0U ||
        !display || display_cap == 0U ||
        !kins_module || kins_module_cap == 0U ||
        !kins_coordinates || kins_coordinates_cap == 0U ||
        !traj_coordinates || traj_coordinates_cap == 0U ||
        !wrapped_rotary_mask) {
        return 0;
    }
    model = v5_motion_model_find(value_text);
    if (!model) {
        return 0;
    }
    snprintf(canonical, canonical_cap, "%s", model->canonical);
    snprintf(display, display_cap, "%s", model->display);
    snprintf(kins_module, kins_module_cap, "%s", model->kins_module);
    snprintf(kins_coordinates, kins_coordinates_cap, "%s", model->kins_coordinates);
    snprintf(traj_coordinates, traj_coordinates_cap, "%s", model->traj_coordinates);
    *wrapped_rotary_mask = model->wrapped_rotary_mask;
    return 1;
}

#define V5_MODEL_JOINT_KEY_COUNT 10U
#define V5_MODEL_INI_UPDATE_MAX 192U
#define V5_MODEL_MIGRATION_MAX V5_MOTION_MODEL_MAX_TRANSITION_AXES

static const char *const k_model_joint_keys[V5_MODEL_JOINT_KEY_COUNT] = {
    "TYPE",
    "HOME",
    "MIN_LIMIT",
    "MAX_LIMIT",
    "MAX_VELOCITY",
    "MAX_ACCELERATION",
    "SCALE",
    "HOME_SEARCH_VEL",
    "HOME_SEQUENCE",
    "BACKLASH",
};

typedef struct V5ModelAxisMigration {
    V5MotionModelAxisTransition transition;
    char axis[2];
    char axis_section[32];
    char current_joint_section[32];
    char target_joint_section[32];
    char original_slave[64];
    char final_slave[64];
    char current_joint[V5_MODEL_JOINT_KEY_COUNT][64];
    char target_joint[V5_MODEL_JOINT_KEY_COUNT][64];
} V5ModelAxisMigration;

static int ini_update_add(
    V5IniTextUpdate *updates,
    size_t *count,
    size_t cap,
    const char *section,
    const char *key,
    const char *value)
{
    if (!updates || !count || *count >= cap || !section || !key || !value) {
        return 0;
    }
    updates[*count].section = section;
    updates[*count].key = key;
    updates[*count].value = value;
    updates[*count].section_seen = 0;
    updates[*count].written = 0;
    ++*count;
    return 1;
}

static int settings_apply_read_axis_binding(
    const char *project_root,
    char axis,
    char *out,
    size_t out_cap)
{
    char axis_name[2];
    axis_name[0] = axis;
    axis_name[1] = '\0';
    if (!v5_settings_parameter_store_read_axis(
            project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
            axis_name, "slave", out, out_cap) ||
        !out[0]) {
        return 0;
    }
    return 1;
}

static int settings_apply_read_current_model(
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

static int settings_apply_prepare_axis_migrations(
    const V5SettingsApplyAxisCommitRequest *request,
    const char *ini_path,
    const V5MotionModelDescriptor *current,
    const V5MotionModelDescriptor *target,
    V5ModelAxisMigration *migrations,
    size_t *migration_count_out)
{
    V5MotionModelAxisTransition transitions[V5_MODEL_MIGRATION_MAX];
    size_t migration_count = 0U;
    size_t migration_i;

    if (migration_count_out) {
        *migration_count_out = 0U;
    }
    if (!request || !ini_path || !migrations || !migration_count_out ||
        !v5_motion_model_build_axis_transition(
            current, target, transitions,
            sizeof(transitions) / sizeof(transitions[0]), &migration_count)) {
        return 0;
    }
    for (migration_i = 0U; migration_i < migration_count; ++migration_i) {
        V5ModelAxisMigration *migration = &migrations[migration_i];
        unsigned int key_i;
        memset(migration, 0, sizeof(*migration));
        migration->transition = transitions[migration_i];
        migration->axis[0] = migration->transition.axis;
        migration->axis[1] = '\0';
        snprintf(migration->axis_section, sizeof(migration->axis_section),
                 "AXIS_%c", migration->transition.axis);
        if (!settings_apply_read_axis_binding(
                request->project_root, migration->transition.axis,
                migration->original_slave, sizeof(migration->original_slave))) {
            return 0;
        }
        if (migration->transition.current_active) {
            if (strcmp(migration->original_slave, "NAT") == 0) {
                return 0;
            }
            snprintf(
                migration->current_joint_section,
                sizeof(migration->current_joint_section),
                "JOINT_%u", migration->transition.current_status_slot);
            for (key_i = 0U; key_i < V5_MODEL_JOINT_KEY_COUNT; ++key_i) {
                if (!v5_settings_apply_ini_read_section_text(
                        ini_path, migration->current_joint_section,
                        k_model_joint_keys[key_i], migration->current_joint[key_i],
                        sizeof(migration->current_joint[key_i]))) {
                    return 0;
                }
            }
        }
        snprintf(migration->final_slave, sizeof(migration->final_slave),
                 "%s", migration->transition.target_active
                     ? migration->original_slave : "NAT");
    }
    for (migration_i = 0U; migration_i < migration_count; ++migration_i) {
        V5ModelAxisMigration *migration = &migrations[migration_i];
        unsigned int key_i;
        if (!migration->transition.target_active) {
            continue;
        }
        snprintf(
            migration->target_joint_section,
            sizeof(migration->target_joint_section),
            "JOINT_%u", migration->transition.target_status_slot);
        if (migration->transition.current_active) {
            for (key_i = 0U; key_i < V5_MODEL_JOINT_KEY_COUNT; ++key_i) {
                snprintf(migration->target_joint[key_i],
                         sizeof(migration->target_joint[key_i]), "%s",
                         migration->current_joint[key_i]);
            }
        } else {
            for (key_i = 0U; key_i < V5_MODEL_JOINT_KEY_COUNT; ++key_i) {
                if (!v5_settings_apply_ini_read_section_text(
                        ini_path, migration->axis_section, k_model_joint_keys[key_i],
                        migration->target_joint[key_i],
                        sizeof(migration->target_joint[key_i]))) {
                    return 0;
                }
            }
        }
        if (strcmp(migration->final_slave, "NAT") == 0) {
            size_t donor_i;
            for (donor_i = 0U; donor_i < migration_count; ++donor_i) {
                const V5ModelAxisMigration *donor = &migrations[donor_i];
                if (donor->transition.current_active &&
                    !donor->transition.target_active &&
                    donor->transition.current_status_slot ==
                        migration->transition.target_status_slot) {
                    snprintf(migration->final_slave, sizeof(migration->final_slave),
                             "%s", donor->original_slave);
                    break;
                }
            }
        }
        if (strcmp(migration->final_slave, "NAT") == 0) {
            return 0;
        }
    }
    for (migration_i = 0U; migration_i < migration_count; ++migration_i) {
        size_t other_i;
        if (!migrations[migration_i].transition.target_active) {
            continue;
        }
        for (other_i = 0U; other_i < migration_i; ++other_i) {
            if (migrations[other_i].transition.target_active &&
                strcmp(migrations[migration_i].final_slave,
                       migrations[other_i].final_slave) == 0) {
                return 0;
            }
        }
    }
    *migration_count_out = migration_count;
    return 1;
}

int v5_settings_apply_commit_motion_model(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    const V5MotionModelDescriptor *target_model;
    const V5MotionModelDescriptor *current_model = 0;
    V5ModelAxisMigration migrations[V5_MODEL_MIGRATION_MAX];
    V5SettingsParameterAxisValue final_bindings[V5_MODEL_MIGRATION_MAX];
    V5SettingsParameterAxisValue original_bindings[V5_MODEL_MIGRATION_MAX];
    size_t migration_count = 0U;
    V5IniTextUpdate updates[V5_MODEL_INI_UPDATE_MAX];
    size_t update_count = 0U;
    char ini_path[512];
    char canonical[32];
    char display[32];
    char kins_module[32];
    char kins_coordinates[16];
    char kins_prefix[32];
    char kins_tool_offset_pin[64];
    char kins_x_rot_point_pin[64];
    char kins_y_rot_point_pin[64];
    char kins_z_rot_point_pin[64];
    char kins_x_offset_pin[64];
    char kins_y_offset_pin[64];
    char kins_z_offset_pin[64];
    char traj_coordinates[32];
    char kinematics[96];
    char wrapped_mask[16];
    char joint_count[16];
    char *original_ini;
    unsigned int wrapped_rotary_mask = 0U;
    unsigned int i;
    size_t migration_i;
    int bindings_written = 0;
    int ok = 0;
    if (!request || !request->value_text ||
        !settings_apply_motion_model_values(
            request->value_text,
            canonical,
            sizeof(canonical),
            display,
            sizeof(display),
            kins_module,
            sizeof(kins_module),
            kins_coordinates,
            sizeof(kins_coordinates),
            traj_coordinates,
            sizeof(traj_coordinates),
            &wrapped_rotary_mask) ||
        !v5_settings_apply_build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    target_model = v5_motion_model_find(canonical);
    if (!target_model) {
        return 0;
    }
    if (!settings_apply_read_current_model(ini_path, &current_model) ||
        !settings_apply_prepare_axis_migrations(
            request, ini_path, current_model, target_model,
            migrations, &migration_count)) {
        return 0;
    }
    snprintf(kins_prefix, sizeof(kins_prefix), "%s", kins_module);
    snprintf(kins_tool_offset_pin, sizeof(kins_tool_offset_pin), "%s.tool-offset", kins_prefix);
    snprintf(kins_x_rot_point_pin, sizeof(kins_x_rot_point_pin), "%s.x-rot-point", kins_prefix);
    snprintf(kins_y_rot_point_pin, sizeof(kins_y_rot_point_pin), "%s.y-rot-point", kins_prefix);
    snprintf(kins_z_rot_point_pin, sizeof(kins_z_rot_point_pin), "%s.z-rot-point", kins_prefix);
    snprintf(kins_x_offset_pin, sizeof(kins_x_offset_pin), "%s.x-offset", kins_prefix);
    snprintf(kins_y_offset_pin, sizeof(kins_y_offset_pin), "%s.y-offset", kins_prefix);
    snprintf(kins_z_offset_pin, sizeof(kins_z_offset_pin), "%s.z-offset", kins_prefix);
    snprintf(kinematics, sizeof(kinematics), "%s coordinates=%s sparm=identityfirst", kins_module, kins_coordinates);
    snprintf(wrapped_mask, sizeof(wrapped_mask), "%u", wrapped_rotary_mask);
    snprintf(joint_count, sizeof(joint_count), "%u", target_model->active_axis_count);
    if (!ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "MODEL", canonical) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "MOTION_MODEL", display) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_MODULE", kins_module) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_COORDINATES", kins_coordinates) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_PREFIX", kins_prefix) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_TOOL_OFFSET_PIN", kins_tool_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_X_ROT_POINT_PIN", kins_x_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Y_ROT_POINT_PIN", kins_y_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Z_ROT_POINT_PIN", kins_z_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_X_OFFSET_PIN", kins_x_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Y_OFFSET_PIN", kins_y_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Z_OFFSET_PIN", kins_z_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "WRAPPED_ROTARY_MASK", wrapped_mask) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "KINS", "KINEMATICS", kinematics) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "KINS", "JOINTS", joint_count) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "TRAJ", "COORDINATES", traj_coordinates)) {
        return 0;
    }
    for (migration_i = 0U; migration_i < migration_count; ++migration_i) {
        V5ModelAxisMigration *migration = &migrations[migration_i];
        final_bindings[migration_i].axis = migration->axis;
        final_bindings[migration_i].value = migration->final_slave;
        original_bindings[migration_i].axis = migration->axis;
        original_bindings[migration_i].value = migration->original_slave;
        if (migration->transition.current_active) {
            for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
                if (!ini_update_add(
                        updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                        migration->axis_section, k_model_joint_keys[i],
                        migration->current_joint[i])) {
                    return 0;
                }
            }
        }
        if (migration->transition.target_active) {
            for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
                if (!ini_update_add(
                        updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                        migration->target_joint_section, k_model_joint_keys[i],
                        migration->target_joint[i])) {
                    return 0;
                }
            }
        }
    }
    original_ini = v5_settings_apply_read_text_file_limited(ini_path);
    if (!original_ini || !v5_settings_apply_ini_write_text_updates(ini_path, updates, update_count)) {
        free(original_ini);
        return 0;
    }
    bindings_written = v5_settings_parameter_store_write_axis_values(
        request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF, "slave",
        final_bindings, migration_count);
    if (!bindings_written) {
        (void)v5_settings_apply_write_text_file_atomic(ini_path, original_ini);
        free(original_ini);
        return 0;
    }
    ok = v5_settings_apply_ini_updates_readback_match(ini_path, updates, update_count);
    for (migration_i = 0U; ok && migration_i < migration_count; ++migration_i) {
        V5ModelAxisMigration *migration = &migrations[migration_i];
        char readback[64];
        ok = settings_apply_read_axis_binding(
                 request->project_root, migration->transition.axis,
                 readback, sizeof(readback)) &&
             strcmp(readback, migration->final_slave) == 0;
    }
    if (!ok) {
        (void)v5_settings_apply_write_text_file_atomic(ini_path, original_ini);
        (void)v5_settings_parameter_store_write_axis_values(
            request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF, "slave",
            original_bindings, migration_count);
        free(original_ini);
        return 0;
    }
    free(original_ini);
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", display);
    }
    return 1;
}
