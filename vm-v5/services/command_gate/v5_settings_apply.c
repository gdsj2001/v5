#include "v5_settings_apply.h"

#include "v5_parameter_owner_map.h"

#include <string.h>

int v5_settings_apply_prepare(
    const V5SettingsApplyRequest *request,
    V5SettingsApplyResult *result)
{
    V5ParameterOwnerRecord record;

    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!request || !request->field_name || !request->field_name[0] ||
        !request->value_text || request->owner_generation == 0U || request->readback_token == 0U) {
        return 0;
    }
    if (!v5_parameter_owner_lookup(request->field_name, &record) || !record.field->writable) {
        return 0;
    }
    if (v5_parameter_table_field_uses_shm(request->field_name)) {
        return 0;
    }
    if (result) {
        result->status = V5_SETTINGS_APPLY_ACCEPTED;
        result->write_owner = record.write_owner;
        result->readback_owner = record.readback_owner;
        result->restart_required = record.field->restart_required;
        result->drive_only_allowed = record.field->drive_only_allowed;
    }
    return 1;
}
