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
#define V5_MODEL_INI_UPDATE_MAX 48U

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

static const V5MotionModelDescriptor *settings_apply_alternate_model(
    const V5MotionModelDescriptor *target,
    const V5MotionModelDescriptor *current)
{
    size_t i;
    if (!target) {
        return 0;
    }
    if (current && current->first_status_slot == target->first_status_slot &&
        current->first_rotary_axis != target->first_rotary_axis) {
        return current;
    }
    for (i = 0U; i < v5_motion_model_registry_count(); ++i) {
        const V5MotionModelDescriptor *candidate = v5_motion_model_registry_at(i);
        if (candidate && candidate->first_status_slot == target->first_status_slot &&
            candidate->first_rotary_axis != target->first_rotary_axis) {
            return candidate;
        }
    }
    return 0;
}

int v5_settings_apply_commit_motion_model(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    const V5MotionModelDescriptor *target_model;
    const V5MotionModelDescriptor *current_model = 0;
    const V5MotionModelDescriptor *alternate_model;
    V5IniTextUpdate updates[V5_MODEL_INI_UPDATE_MAX];
    size_t update_count = 0U;
    char ini_path[512];
    char current_model_text[64];
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
    char target_axis[2];
    char alternate_axis[2] = "";
    char snapshot_axis[2];
    char target_axis_section[32];
    char snapshot_axis_section[32];
    char joint_section[32];
    char target_slave[64] = "";
    char alternate_slave[64] = "";
    char final_target_slave[64] = "";
    char final_alternate_slave[64] = "";
    char joint_snapshot[V5_MODEL_JOINT_KEY_COUNT][64];
    char target_joint[V5_MODEL_JOINT_KEY_COUNT][64];
    char target_slave_readback[64];
    char alternate_slave_readback[64];
    char *original_ini;
    unsigned int wrapped_rotary_mask = 0U;
    unsigned int i;
    int pair_needed = 0;
    int pair_written = 0;
    int target_is_nat = 0;
    int alternate_is_nat = 1;
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
    if (v5_settings_apply_ini_read_section_text(ini_path, "RTCP", "MODEL", current_model_text, sizeof(current_model_text))) {
        current_model = v5_motion_model_find(current_model_text);
    }
    alternate_model = settings_apply_alternate_model(target_model, current_model);
    target_axis[0] = target_model->first_rotary_axis;
    target_axis[1] = '\0';
    snapshot_axis[0] = target_axis[0];
    snapshot_axis[1] = '\0';
    if (alternate_model) {
        alternate_axis[0] = alternate_model->first_rotary_axis;
        alternate_axis[1] = '\0';
        if (!v5_settings_parameter_store_read_axis(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                target_axis, "slave", target_slave, sizeof(target_slave)) ||
            !v5_settings_parameter_store_read_axis(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                alternate_axis, "slave", alternate_slave, sizeof(alternate_slave))) {
            return 0;
        }
        snprintf(final_target_slave, sizeof(final_target_slave), "%s", target_slave);
        snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", alternate_slave);
        target_is_nat = strcmp(target_slave, "NAT") == 0;
        alternate_is_nat = strcmp(alternate_slave, "NAT") == 0;
        if (target_is_nat && !alternate_is_nat) {
            snprintf(final_target_slave, sizeof(final_target_slave), "%s", alternate_slave);
            snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", "NAT");
            snapshot_axis[0] = alternate_axis[0];
            pair_needed = 1;
        } else if (!target_is_nat && alternate_is_nat) {
            snapshot_axis[0] = target_axis[0];
        } else if (!target_is_nat && !alternate_is_nat) {
            if (strcmp(target_slave, alternate_slave) != 0) {
                return 0;
            }
            snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", "NAT");
            snapshot_axis[0] = current_model &&
                current_model->first_rotary_axis == alternate_axis[0] ? alternate_axis[0] : target_axis[0];
            pair_needed = 1;
        } else if (current_model && current_model->first_status_slot == target_model->first_status_slot) {
            snapshot_axis[0] = current_model->first_rotary_axis;
        }
    }
    snprintf(target_axis_section, sizeof(target_axis_section), "AXIS_%c", target_axis[0]);
    snprintf(snapshot_axis_section, sizeof(snapshot_axis_section), "AXIS_%c", snapshot_axis[0]);
    snprintf(joint_section, sizeof(joint_section), "JOINT_%u", target_model->first_status_slot);
    for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
        if (!v5_settings_apply_ini_read_section_text(
                ini_path, joint_section, k_model_joint_keys[i],
                joint_snapshot[i], sizeof(joint_snapshot[i]))) {
            return 0;
        }
        if (snapshot_axis[0] == target_axis[0]) {
            snprintf(target_joint[i], sizeof(target_joint[i]), "%s", joint_snapshot[i]);
        } else if (!v5_settings_apply_ini_read_section_text(
                       ini_path, target_axis_section, k_model_joint_keys[i],
                       target_joint[i], sizeof(target_joint[i]))) {
            return 0;
        }
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
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "TRAJ", "COORDINATES", traj_coordinates)) {
        return 0;
    }
    for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
        if (!ini_update_add(
                updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                snapshot_axis_section, k_model_joint_keys[i], joint_snapshot[i]) ||
            !ini_update_add(
                updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                joint_section, k_model_joint_keys[i], target_joint[i])) {
            return 0;
        }
    }
    original_ini = v5_settings_apply_read_text_file_limited(ini_path);
    if (!original_ini || !v5_settings_apply_ini_write_text_updates(ini_path, updates, update_count)) {
        free(original_ini);
        return 0;
    }
    if (pair_needed) {
        pair_written = v5_settings_parameter_store_write_axis_pair(
            request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
            target_axis, alternate_axis, "slave", final_target_slave, final_alternate_slave);
        if (!pair_written) {
            (void)v5_settings_apply_write_text_file_atomic(ini_path, original_ini);
            free(original_ini);
            return 0;
        }
    }
    ok = v5_settings_apply_ini_updates_readback_match(ini_path, updates, update_count);
    if (ok && alternate_model) {
        ok = v5_settings_parameter_store_read_axis(
                 request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                 target_axis, "slave", target_slave_readback, sizeof(target_slave_readback)) &&
             v5_settings_parameter_store_read_axis(
                 request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                 alternate_axis, "slave", alternate_slave_readback, sizeof(alternate_slave_readback)) &&
             strcmp(target_slave_readback, final_target_slave) == 0 &&
             strcmp(alternate_slave_readback, final_alternate_slave) == 0;
    }
    if (!ok) {
        (void)v5_settings_apply_write_text_file_atomic(ini_path, original_ini);
        if (pair_written) {
            (void)v5_settings_parameter_store_write_axis_pair(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                target_axis, alternate_axis, "slave", target_slave, alternate_slave);
        }
        free(original_ini);
        return 0;
    }
    free(original_ini);
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", display);
    }
    return 1;
}
