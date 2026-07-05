#include "v5_parameter_table.h"

#include <string.h>

static const V5ParameterField kV5ParameterFields[] = {
    {"motion_driver_mode", V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 1, 1, 0},
    {"bus_pulse_active_mode", V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 0, 0, 0},
    {"axis_*_axis_mode", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_precision", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_pitch", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_motor_rev", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_load_rev", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_max_velocity", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_max_acceleration", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_backlash", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_ferror", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_min_ferror", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_home_*", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_soft_minus", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_soft_plus", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"axis_*_zero", V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 1, 1, 0},
    {"axis_*_slave", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"axis_*_encoder_bits", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"axis_*_profile_id", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"drive_profile_id", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"axis_*_egear_*", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"feedback_counts_per_motor_rev", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"rotary_load_counts_per_rev", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"drive_zero_model", V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1, 0, 1},
    {"wcs_offsets", V5_PARAMETER_OWNER_LINUXCNC_PARAMETER_FILE, V5_PARAMETER_READBACK_PARAMETER_FILE, 1, 0, 0},
    {"g92_offset", V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 1, 0, 0},
    {"tool_table", V5_PARAMETER_OWNER_TOOL_TABLE, V5_PARAMETER_READBACK_TOOL_NATIVE, 1, 1, 0},
    {"tool_offset", V5_PARAMETER_OWNER_TOOL_TABLE, V5_PARAMETER_READBACK_TOOL_NATIVE, 0, 0, 0},
    {"display_tool_length_mm", V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 1, 1, 0},
    {"rtcp_ac_geometry", V5_PARAMETER_OWNER_KINEMATICS_NATIVE, V5_PARAMETER_READBACK_NATIVE_API, 1, 1, 0},
    {"g53_geometry", V5_PARAMETER_OWNER_KINEMATICS_NATIVE, V5_PARAMETER_READBACK_NATIVE_API, 1, 1, 0},
};

static int v5_parameter_match_pattern(const char *pattern, const char *field_name)
{
    const char *star;
    size_t prefix_len;
    size_t suffix_len;
    size_t field_len;

    if (!pattern || !field_name) {
        return 0;
    }
    star = strchr(pattern, '*');
    if (!star) {
        return strcmp(pattern, field_name) == 0;
    }

    prefix_len = (size_t)(star - pattern);
    suffix_len = strlen(star + 1);
    field_len = strlen(field_name);
    if (field_len < prefix_len + suffix_len) {
        return 0;
    }
    return strncmp(pattern, field_name, prefix_len) == 0 &&
           strcmp(field_name + field_len - suffix_len, star + 1) == 0;
}

const V5ParameterField *v5_parameter_table_entries(size_t *count)
{
    if (count) {
        *count = v5_parameter_table_count();
    }
    return kV5ParameterFields;
}

size_t v5_parameter_table_count(void)
{
    return sizeof(kV5ParameterFields) / sizeof(kV5ParameterFields[0]);
}

const V5ParameterField *v5_parameter_table_find(const char *field_name)
{
    size_t i;
    for (i = 0U; i < v5_parameter_table_count(); ++i) {
        if (v5_parameter_match_pattern(kV5ParameterFields[i].field_pattern, field_name)) {
            return &kV5ParameterFields[i];
        }
    }
    return 0;
}

int v5_parameter_table_field_is_drive_only(const char *field_name)
{
    const V5ParameterField *field = v5_parameter_table_find(field_name);
    return field ? field->drive_only_allowed : 0;
}

int v5_parameter_table_field_uses_shm(const char *field_name)
{
    (void)field_name;
    return 0;
}
