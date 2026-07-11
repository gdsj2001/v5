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

int v5_settings_apply_field_is_scale_chain(const char *field_name)
{
    return field_name &&
           (strstr(field_name, "_precision") != 0 ||
            strstr(field_name, "_pitch") != 0 ||
            strstr(field_name, "_motor_rev") != 0 ||
            strstr(field_name, "_load_rev") != 0);
}

static int parse_positive_finite(const char *text)
{
    char *endp;
    double value;
    if (!text || !text[0]) {
        return 0;
    }
    value = strtod(text, &endp);
    return endp != text && *endp == '\0' && isfinite(value) && value > 0.0;
}

void v5_settings_apply_scale_chain_result_code(V5SettingsApplyScaleChainResult *result, const char *code)
{
    if (!result || !code) {
        return;
    }
    snprintf(result->code, sizeof(result->code), "%s", code);
}

double v5_settings_apply_nearest_integer(double value);


int v5_settings_apply_prepare(
    const V5SettingsApplyRequest *request,
    V5SettingsApplyResult *result)
{
    V5ParameterOwnerRecord record;

    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!request || !request->field_name || !request->field_name[0] ||
        !request->value_text || !request->value_text[0] ||
        request->owner_generation == 0U || request->readback_token == 0U) {
        return 0;
    }
    if (!v5_parameter_owner_lookup(request->field_name, &record) || !record.field->writable) {
        return 0;
    }
    if (v5_parameter_table_field_uses_shm(request->field_name)) {
        return 0;
    }
    if (v5_settings_apply_field_is_scale_chain(request->field_name) &&
        !parse_positive_finite(request->value_text)) {
        return 0;
    }
    if (result) {
        result->status = V5_SETTINGS_APPLY_ACCEPTED;
        result->write_owner = record.write_owner;
        result->readback_owner = record.readback_owner;
        result->restart_required = record.field->restart_required;
        result->drive_only_allowed = record.field->drive_only_allowed;
        result->scale_chain_transaction_required =
            v5_settings_apply_field_is_scale_chain(request->field_name);
        result->raw_limits_recompute_required = result->scale_chain_transaction_required;
    }
    return 1;
}


int v5_settings_apply_commit_axis_value(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    V5SettingsApplyRequest prepare_request;
    V5SettingsApplyResult apply_result;
    V5ParameterOwnerRecord owner_record;
    int ok = 0;
    if (result) {
        memset(result, 0, sizeof(*result));
        v5_settings_apply_scale_chain_result_code(&result->scale_chain, "SCALE_CHAIN_NOT_ATTEMPTED");
    }
    if (!request || !request->project_root || !request->axis || !request->axis[0] ||
        !request->field_key || !request->field_key[0] || !request->field_name || !request->field_name[0] ||
        !request->value_text || !request->value_text[0]) {
        return 0;
    }
    memset(&prepare_request, 0, sizeof(prepare_request));
    prepare_request.field_name = request->field_name;
    prepare_request.value_text = request->value_text;
    prepare_request.owner_generation = request->owner_generation;
    prepare_request.readback_token = request->readback_token;
    if (!v5_settings_apply_prepare(&prepare_request, &apply_result)) {
        return 0;
    }
    if (!v5_parameter_owner_lookup(request->field_name, &owner_record) || !owner_record.field) {
        return 0;
    }
    if (result) {
        result->apply = apply_result;
    }
    switch (owner_record.field->owner) {
    case V5_PARAMETER_OWNER_RUNTIME_INI:
        ok = v5_settings_apply_commit_runtime_ini(request, result);
        break;
    case V5_PARAMETER_OWNER_KINEMATICS_NATIVE:
        ok = v5_settings_apply_commit_g53_geometry(request, result);
        break;
    case V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE:
    case V5_PARAMETER_OWNER_DRIVE_ONLY:
        ok = v5_settings_apply_commit_parameter_table(request, owner_record.field->owner, result);
        break;
    default:
        ok = 0;
        break;
    }
    if (!ok || !result) {
        return ok;
    }
    result->owner_written = 1;
    result->source_readback_confirmed = result->readback_value[0] ? 1 : 0;
    result->restart_pending = apply_result.restart_required ? 1 : 0;
    return result->source_readback_confirmed;
}
