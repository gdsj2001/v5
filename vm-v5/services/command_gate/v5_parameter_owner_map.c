#include "v5_parameter_owner_map.h"

#include <string.h>

static const char *v5_parameter_owner_name(V5ParameterOwnerKind owner)
{
    switch (owner) {
    case V5_PARAMETER_OWNER_NATIVE_RUNTIME:
        return "native_runtime";
    case V5_PARAMETER_OWNER_RUNTIME_INI:
        return "runtime_ini";
    case V5_PARAMETER_OWNER_LINUXCNC_PARAMETER_FILE:
        return "linuxcnc_parameter_file";
    case V5_PARAMETER_OWNER_TOOL_TABLE:
        return "tool_table";
    case V5_PARAMETER_OWNER_DRIVE_ONLY:
        return "drive_only";
    case V5_PARAMETER_OWNER_KINEMATICS_NATIVE:
        return "kinematics_native";
    default:
        return "unknown";
    }
}

static const char *v5_parameter_readback_name(V5ParameterReadbackKind readback)
{
    switch (readback) {
    case V5_PARAMETER_READBACK_NATIVE_API:
        return "native_api_readback";
    case V5_PARAMETER_READBACK_RUNTIME_INI:
        return "runtime_ini_readback";
    case V5_PARAMETER_READBACK_PARAMETER_FILE:
        return "parameter_file_readback";
    case V5_PARAMETER_READBACK_TOOL_NATIVE:
        return "tool_native_readback";
    case V5_PARAMETER_READBACK_DRIVE_PROVIDER:
        return "drive_provider_readback";
    default:
        return "unknown_readback";
    }
}

int v5_parameter_owner_lookup(const char *field_name, V5ParameterOwnerRecord *record)
{
    const V5ParameterField *field;

    if (!field_name || !field_name[0]) {
        return 0;
    }
    field = v5_parameter_table_find(field_name);
    if (!field) {
        return 0;
    }
    if (record) {
        record->field = field;
        record->write_owner = v5_parameter_owner_name(field->owner);
        record->readback_owner = v5_parameter_readback_name(field->readback);
    }
    return 1;
}

int v5_parameter_owner_can_write(const char *field_name)
{
    V5ParameterOwnerRecord record;
    if (!v5_parameter_owner_lookup(field_name, &record)) {
        return 0;
    }
    return record.field->writable;
}

int v5_parameter_owner_requires_restart(const char *field_name)
{
    V5ParameterOwnerRecord record;
    if (!v5_parameter_owner_lookup(field_name, &record)) {
        return 0;
    }
    return record.field->restart_required;
}
