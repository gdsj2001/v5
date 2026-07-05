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
    request.owner_generation = 1U;
    request.readback_token = 1U;
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

int main(void)
{
    size_t count = v5_parameter_table_count();

    if (count == 0U) {
        return 1;
    }
    if (v5_parameter_table_field_uses_shm("axis_X_max_velocity")) {
        return 2;
    }
    if (!check_apply("axis_X_max_velocity", "10000")) {
        return 3;
    }
    if (!check_apply("axis_A_profile_id", "sv630n_private")) {
        return 4;
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
