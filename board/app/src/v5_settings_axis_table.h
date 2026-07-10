#ifndef V5_SETTINGS_AXIS_TABLE_H
#define V5_SETTINGS_AXIS_TABLE_H

#include "lvgl.h"
#include "v5_boot_closure.h"
#include "v5_parameter_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5SettingsAxisRowSpec {
    const char *axis;
    const char *label;
    const char *mode_text;
    int enabled;
} V5SettingsAxisRowSpec;

typedef void (*V5SettingsAxisCommitCallback)(void *user_data);
typedef void (*V5SettingsAxisZeroCallback)(const char *axis,
                                           const char *driver_mode,
                                           const char *target_scope,
                                           const char *apply_mode,
                                           const char *slave_index,
                                           const char *home_offset,
                                           void *user_data);

typedef struct V5SettingsAxisColumnSpec {
    const char *field_key;
    const char *label;
    int x;
    int width;
    int kind;
    V5ParameterOwnerKind owner;
    V5ParameterReadbackKind readback;
    int drive_only_allowed;
} V5SettingsAxisColumnSpec;

void v5_settings_axis_table_load_boot_closure(const V5BootClosure *closure);
void v5_settings_axis_table_load_readback(const char *project_root);
void v5_settings_axis_table_reload_current_readback(void);
void v5_settings_axis_table_begin_page(void);
void v5_settings_axis_table_create(lv_obj_t *root);
void v5_settings_axis_table_set_commit_callback(V5SettingsAxisCommitCallback cb, void *user_data);
void v5_settings_axis_table_set_axis_zero_callback(V5SettingsAxisZeroCallback cb, void *user_data);
int v5_settings_axis_table_start_axis_zero(unsigned int row, const char *home_offset);
void v5_settings_axis_table_create_g53_cell(lv_obj_t *root, unsigned int row, unsigned int col, int x, int y, int w, int h);
unsigned int v5_settings_axis_table_row_count(void);
unsigned int v5_settings_axis_table_column_count(void);
const V5SettingsAxisRowSpec *v5_settings_axis_table_rows(void);
const V5SettingsAxisColumnSpec *v5_settings_axis_table_columns(void);
int v5_settings_axis_table_field_id(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col, char *out, size_t out_cap);
int v5_settings_axis_table_field_matches_owner(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col);
const char *v5_settings_axis_table_value(unsigned int row, unsigned int col);
int v5_settings_axis_table_value_is_real(unsigned int row, unsigned int col);
const char *v5_settings_axis_table_g53_value(unsigned int row, unsigned int col);
int v5_settings_axis_table_g53_value_is_real(unsigned int row, unsigned int col);
int v5_settings_axis_table_g53_value_is_editable(unsigned int row, unsigned int col);
int v5_settings_axis_table_commit_g53_value(unsigned int row, unsigned int col, const char *value);
int v5_settings_axis_table_cell_is_disabled(unsigned int row, unsigned int col);
const char *v5_settings_axis_table_motion_model_value(void);
int v5_settings_axis_table_motion_model_value_is_real(void);
int v5_settings_axis_table_commit_motion_model(const char *value);
const char *v5_settings_axis_table_bus_pulse_value(void);
int v5_settings_axis_table_bus_pulse_value_is_real(void);
unsigned int v5_settings_axis_table_slave_option_count(void);
const char *v5_settings_axis_table_slave_option(unsigned int index);
int v5_settings_axis_table_dropdown_options(unsigned int row, unsigned int col, char *out, size_t cap);
int v5_settings_axis_table_commit_value(unsigned int row, unsigned int col, const char *value);

#ifdef __cplusplus
}
#endif

#endif
