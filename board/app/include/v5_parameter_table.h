#ifndef V5_PARAMETER_TABLE_H
#define V5_PARAMETER_TABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5ParameterOwnerKind {
    V5_PARAMETER_OWNER_NATIVE_RUNTIME = 1,
    V5_PARAMETER_OWNER_RUNTIME_INI,
    V5_PARAMETER_OWNER_LINUXCNC_PARAMETER_FILE,
    V5_PARAMETER_OWNER_TOOL_TABLE,
    V5_PARAMETER_OWNER_DRIVE_ONLY,
    V5_PARAMETER_OWNER_KINEMATICS_NATIVE,
    V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE,
} V5ParameterOwnerKind;

typedef enum V5ParameterReadbackKind {
    V5_PARAMETER_READBACK_NATIVE_API = 1,
    V5_PARAMETER_READBACK_RUNTIME_INI,
    V5_PARAMETER_READBACK_PARAMETER_FILE,
    V5_PARAMETER_READBACK_TOOL_NATIVE,
    V5_PARAMETER_READBACK_DRIVE_PROVIDER,
    V5_PARAMETER_READBACK_SELF_PARAMETER_TABLE,
} V5ParameterReadbackKind;

typedef struct V5ParameterField {
    const char *field_pattern;
    V5ParameterOwnerKind owner;
    V5ParameterReadbackKind readback;
    int writable;
    int restart_required;
    int drive_only_allowed;
} V5ParameterField;

const V5ParameterField *v5_parameter_table_entries(size_t *count);
size_t v5_parameter_table_count(void);
const V5ParameterField *v5_parameter_table_find(const char *field_name);
int v5_parameter_table_field_is_drive_only(const char *field_name);
int v5_parameter_table_field_uses_shm(const char *field_name);
const char *v5_parameter_table_self_axis_value(const char *axis, const char *field_key);
const char *v5_parameter_table_self_g53_value(const char *target, const char *axis);
const char *v5_parameter_table_self_setting_value(const char *field_key);

#ifdef __cplusplus
}
#endif

#endif
