#include "v5_parameter_table.h"
#include "v5_parameter_owner_map.h"
#include "v5_settings_apply.h"

#include <stdio.h>

static int check_apply(const char *field, const char *value)
{
    V5SettingsApplyRequest request;
    V5SettingsApplyResult result;

    request.field_name = field;
    request.value_text = value;
    request.owner_generation = 0x13579BDFU;
    request.readback_token = 0x2468ACE1U;
    if (!v5_settings_apply_prepare(&request, &result)) {
        return 0;
    }
    printf(
        "%s owner=%s readback=%s restart=%d drive_only=%d\n",
        field,
        result.write_owner,
        result.readback_owner,
        result.restart_required,
        result.drive_only_allowed);
    return 1;
}

static int check_native_g53_field(const char *field)
{
    const V5ParameterField *entry = v5_parameter_table_find(field);
    if (!entry) {
        fprintf(stderr, "missing G53 field owner: %s\n", field);
        return 0;
    }
    if (entry->owner != V5_PARAMETER_OWNER_KINEMATICS_NATIVE ||
        entry->readback != V5_PARAMETER_READBACK_NATIVE_API ||
        !entry->restart_required ||
        entry->drive_only_allowed) {
        fprintf(stderr, "wrong G53 field owner: %s\n", field);
        return 0;
    }
    return 1;
}

int main(void)
{
    size_t count = v5_parameter_table_count();

    if (count == 0U) {
        return 1;
    }
    if (v5_parameter_table_field_uses_shm("axis_X_max_velocity")) {
        return 2;
    }
    {
        const V5ParameterField *wcs_field = v5_parameter_table_find("wcs_offsets");
        if (!wcs_field || wcs_field->owner != V5_PARAMETER_OWNER_NATIVE_RUNTIME ||
            wcs_field->readback != V5_PARAMETER_READBACK_NATIVE_API) {
            return 12;
        }
    }
    if (!check_native_g53_field("G53_A_Y") ||
        !check_native_g53_field("G53_A_Z") ||
        !check_native_g53_field("G53_B_X") ||
        !check_native_g53_field("G53_B_Z") ||
        !check_native_g53_field("G53_C_X") ||
        !check_native_g53_field("G53_C_Y") ||
        !check_native_g53_field("motion_model")) {
        return 15;
    }
    if (!check_apply("motion_model", "XYZBC_TRT")) {
        return 16;
    }
    if (!check_apply("axis_X_max_velocity", "10000")) {
        return 3;
    }
    if (!check_apply("axis_A_profile_id", "sv630n_private")) {
        return 4;
    }
    if (!check_apply("axis_X_pitch", "5")) {
        return 7;
    }
    if (check_apply("axis_X_pitch", "0") || check_apply("axis_X_precision", "-1") || check_apply("axis_X_motor_rev", "nan")) {
        return 12;
    }
    if (!check_apply("axis_X_encoder_bits", "18")) {
        return 8;
    }
    {
        V5SettingsApplyRequest request;
        V5SettingsApplyResult result;
        request.field_name = "axis_X_slave";
        request.value_text = "1";
        request.owner_generation = 0x13579BDFU;
        request.readback_token = 0x2468ACE1U;
        if (!v5_settings_apply_prepare(&request, &result) ||
            result.restart_required != 1 ||
            result.drive_only_allowed != 0) {
            return 13;
        }
        request.field_name = "axis_X_encoder_bits";
        request.value_text = "18";
        if (!v5_settings_apply_prepare(&request, &result) ||
            result.restart_required != 1 ||
            result.drive_only_allowed != 1) {
            return 14;
        }
    }
    if (!check_apply("axis_X_direction_mode", "cw")) {
        return 11;
    }
    if (v5_parameter_table_self_axis_value("A", "slave")) {
        return 9;
    }
    if (v5_parameter_table_self_axis_value("A", "encoder_bits")) {
        return 10;
    }
    if (v5_settings_apply_prepare(0, 0)) {
        return 5;
    }
    if (v5_parameter_owner_can_write("tool_offset")) {
        return 6;
    }

    printf("v5 parameter table: fields=%lu\n", (unsigned long)count);
    return 0;
}
