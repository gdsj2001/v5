#ifndef V5_SETTINGS_PARAMETER_STORE_H
#define V5_SETTINGS_PARAMETER_STORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5SettingsParameterDiskTable {
    V5_SETTINGS_PARAMETER_DISK_NONE = 0,
    V5_SETTINGS_PARAMETER_DISK_SELF = 1,
    V5_SETTINGS_PARAMETER_DISK_DRIVE,
} V5SettingsParameterDiskTable;

int v5_settings_parameter_store_read_axis(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    char *out,
    size_t out_cap);

int v5_settings_parameter_store_write_axis(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    const char *value);

int v5_settings_parameter_store_write_axis_pair(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *first_axis,
    const char *second_axis,
    const char *field_key,
    const char *first_value,
    const char *second_value);

#ifdef __cplusplus
}
#endif

#endif
