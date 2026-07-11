#ifndef V5_SETTINGS_APPLY_INTERNAL_H
#define V5_SETTINGS_APPLY_INTERNAL_H

#include "v5_settings_apply.h"

#include <stddef.h>

#define V5_SETTINGS_APPLY_MAX_FILE_BYTES (512U * 1024U)
#define V5_SETTINGS_RUNTIME_JSON_DEFAULT "/opt/8ax/phase0_bus5/settings_runtime.json"

typedef struct V5IniTextUpdate {
    const char *section;
    const char *key;
    const char *value;
    int section_seen;
    int written;
} V5IniTextUpdate;

int v5_settings_apply_file_exists(const char *path);
char *v5_settings_apply_read_text_file_limited(const char *path);
int v5_settings_apply_json_number_value(
    const char *start, const char *end, const char *key, double *out);
int v5_settings_apply_runtime_axis_object(
    const char *json, const char *axis, const char **axis_start, const char **axis_end);
int v5_settings_apply_build_runtime_ini_path(
    char *out, size_t out_cap, const char *project_root);
int v5_settings_apply_ini_section_line(
    const char *raw, char *section, size_t section_cap);
int v5_settings_apply_ini_key_line(
    const char *raw, const char *key, const char **value_start);
int v5_settings_apply_ini_read_preferred_number(
    const char *path, const char *primary_section, const char *fallback_section,
    const char *key, double *out);
int v5_settings_apply_ini_write_scale_and_limits(
    const char *path, const char *axis_section, const char *joint_section,
    int write_scale, double scale, double raw_min, double raw_max);
void v5_settings_apply_scale_chain_result_code(
    V5SettingsApplyScaleChainResult *result, const char *code);
double v5_settings_apply_nearest_integer(double value);
void v5_settings_apply_trim(char *text);
int v5_settings_apply_numeric_text(const char *value);
int v5_settings_apply_safe_ini_value(const char *value);
int v5_settings_apply_values_match(const char *actual, const char *expected);
int v5_settings_apply_ini_value_for_field(
    const char *field_key, const char *value,
    char *ini_value, size_t ini_cap,
    char *expected_display, size_t display_cap);
int v5_settings_apply_display_from_raw(
    const char *field_key, const char *raw, char *out, size_t out_cap);
int v5_settings_apply_display_values_match(
    const char *field_key, const char *actual, const char *expected);
int v5_settings_apply_ini_read_section_text(
    const char *path, const char *section_name, const char *key,
    char *out, size_t out_cap);
int v5_settings_apply_ini_write_section_text(
    const char *path, const char *section_name, const char *key,
    const char *value, int numeric_required,
    char *readback, size_t readback_cap);
int v5_settings_apply_ini_write_text_updates(
    const char *path, V5IniTextUpdate *updates, size_t update_count);
int v5_settings_apply_write_text_file_atomic(const char *path, const char *text);
int v5_settings_apply_ini_updates_readback_match(
    const char *path, const V5IniTextUpdate *updates, size_t update_count);
int v5_settings_apply_commit_runtime_ini(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result);
int v5_settings_apply_commit_motion_model(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result);
int v5_settings_apply_commit_g53_geometry(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result);
int v5_settings_apply_commit_parameter_table(
    const V5SettingsApplyAxisCommitRequest *request,
    V5ParameterOwnerKind owner,
    V5SettingsApplyAxisCommitResult *result);

#endif
