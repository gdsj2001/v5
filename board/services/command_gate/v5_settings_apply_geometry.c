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

static const char *settings_apply_g53_rtcp_key(const char *field_name)
{
    if (!field_name) return 0;
    if (strcmp(field_name, "g53_A_center_y") == 0) return "G53_A_Y";
    if (strcmp(field_name, "g53_A_center_z") == 0) return "G53_A_Z";
    if (strcmp(field_name, "g53_B_center_x") == 0) return "G53_B_X";
    if (strcmp(field_name, "g53_B_center_z") == 0) return "G53_B_Z";
    if (strcmp(field_name, "g53_C_center_x") == 0) return "G53_C_X";
    if (strcmp(field_name, "g53_C_center_y") == 0) return "G53_C_Y";
    return 0;
}

int v5_settings_apply_commit_g53_geometry(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    char ini_path[512];
    char raw[64];
    const char *ini_key;
    if (!request || !request->field_name || !request->value_text) {
        return 0;
    }
    if (strcmp(request->field_name, "motion_model") == 0) {
        return v5_settings_apply_commit_motion_model(request, result);
    }
    if (!v5_settings_apply_numeric_text(request->value_text) ||
        !v5_settings_apply_build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    ini_key = settings_apply_g53_rtcp_key(request->field_name);
    if (!ini_key) {
        return 0;
    }
    if (!v5_settings_apply_ini_write_section_text(ini_path, "RTCP", ini_key, request->value_text, 0, raw, sizeof(raw))) {
        return 0;
    }
    if (!v5_settings_apply_ini_read_section_text(ini_path, "RTCP", ini_key, raw, sizeof(raw))) {
        return 0;
    }
    if (!v5_settings_apply_values_match(raw, request->value_text)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", raw);
    }
    return 1;
}

static V5SettingsParameterDiskTable settings_apply_disk_table_for_owner(
    const char *field_key,
    V5ParameterOwnerKind owner)
{
    if (field_key && strcmp(field_key, "slave") == 0) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    if (field_key && strcmp(field_key, "encoder_bits") == 0) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (owner == V5_PARAMETER_OWNER_DRIVE_ONLY) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (owner == V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    return V5_SETTINGS_PARAMETER_DISK_NONE;
}

int v5_settings_apply_commit_parameter_table(
    const V5SettingsApplyAxisCommitRequest *request,
    V5ParameterOwnerKind owner,
    V5SettingsApplyAxisCommitResult *result)
{
    V5SettingsParameterDiskTable table = settings_apply_disk_table_for_owner(request->field_key, owner);
    char readback[64];
    if (table == V5_SETTINGS_PARAMETER_DISK_NONE) {
        return 0;
    }
    if (!v5_settings_parameter_store_write_axis(request->project_root, table,
                                                request->axis, request->field_key, request->value_text)) {
        return 0;
    }
    if (!v5_settings_parameter_store_read_axis(request->project_root, table,
                                               request->axis, request->field_key, readback, sizeof(readback))) {
        return 0;
    }
    if (!v5_settings_apply_values_match(readback, request->value_text)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", readback);
    }
    return 1;
}
