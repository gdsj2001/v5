#ifndef V5_SETTINGS_AXIS_TABLE_INTERNAL_H
#define V5_SETTINGS_AXIS_TABLE_INTERNAL_H

#include "v5_settings_axis_table.h"
#include "v5_settings_parameter_store.h"
#include "v5_ui_first_frame_guard.h"

#include <stddef.h>
#include <stdint.h>

enum {
    V5_AXIS_FIELD_EDIT = 0,
    V5_AXIS_FIELD_SELECT = 1,
    V5_AXIS_FIELD_ACTION = 2,
    V5_AXIS_FIELD_READONLY = 3
};

#define V5_AXIS_TABLE_MAX_ROWS 8U
#define V5_AXIS_TABLE_MAX_COLS 19U
#define V5_AXIS_VALUE_CAP 32U
#define V5_G53_ROW_COUNT 5U
#define V5_SLAVE_OPTION_MAX 32U
#define V5_SLAVE_OPTION_CAP 128U
#define V5_DROPDOWN_OPTIONS_CAP 1024U
#define V5_RESIDENT_PARAMETER_KEY_CAP 96U
#define V5_RESIDENT_PARAMETER_VALUE_CAP 512U
#define V5_AXIS_CELL_REF_MAX 192U

typedef struct V5AxisIniRow {
    int axis_seen;
    int joint_seen;
    char type[16];
    char direction_mode[24];
    char min_limit[24];
    char max_limit[24];
    char max_velocity[24];
    char max_acceleration[24];
    char pitch[24];
    char motor_rev[24];
    char load_rev[24];
    char scale[24];
    char home[24];
    char home_sequence[24];
    char home_search_vel[24];
    char backlash[24];
} V5AxisIniRow;

typedef struct V5SlaveDriveDisplay {
    char slave_id[V5_AXIS_VALUE_CAP];
    char egear_numerator[V5_AXIS_VALUE_CAP];
    char egear_denominator[V5_AXIS_VALUE_CAP];
    char write_status[V5_AXIS_VALUE_CAP];
    unsigned char numerator_real;
    unsigned char denominator_real;
    unsigned char status_real;
} V5SlaveDriveDisplay;

typedef struct V5AxisCellRef {
    unsigned int kind;
    unsigned int row;
    unsigned int col;
    lv_obj_t *obj;
    lv_obj_t *label;
    char field_id[96];
} V5AxisCellRef;

enum {
    V5_CELL_KIND_AXIS = 0U,
    V5_CELL_KIND_G53 = 1U,
    V5_CELL_KIND_AXIS_LABEL = 2U
};

extern const V5SettingsAxisColumnSpec g_v5_axis_table_columns[V5_AXIS_TABLE_MAX_COLS];
extern const V5SettingsAxisRowSpec g_v5_axis_table_rows[V5_AXIS_TABLE_MAX_ROWS];
extern char g_v5_axis_table_values[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS][V5_AXIS_VALUE_CAP];
extern unsigned char g_v5_axis_table_value_real[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS];
extern int g_v5_axis_table_loaded;
extern char g_v5_axis_table_g53_values[V5_G53_ROW_COUNT][3U][V5_AXIS_VALUE_CAP];
extern unsigned char g_v5_axis_table_g53_value_real[V5_G53_ROW_COUNT][3U];
extern char g_v5_axis_table_motion_model_value[V5_AXIS_VALUE_CAP];
extern unsigned char g_v5_axis_table_motion_model_real;
extern char g_v5_axis_table_bus_pulse_value[V5_AXIS_VALUE_CAP];
extern unsigned char g_v5_axis_table_bus_pulse_real;
extern char g_v5_axis_table_slave_options[V5_SLAVE_OPTION_MAX][V5_SLAVE_OPTION_CAP];
extern unsigned int g_v5_axis_table_slave_option_count;
extern char g_v5_axis_table_project_root[256];
extern char g_v5_axis_table_self_parameter_text[V5_BOOT_CLOSURE_TEXT_CAP];
extern char g_v5_axis_table_drive_parameter_text[V5_BOOT_CLOSURE_TEXT_CAP];
extern V5SlaveDriveDisplay g_v5_axis_table_slave_drive_display[V5_SLAVE_OPTION_MAX];
extern unsigned int g_v5_axis_table_slave_drive_display_count;
extern V5AxisCellRef g_v5_axis_table_cell_refs[V5_AXIS_CELL_REF_MAX];
extern unsigned int g_v5_axis_table_cell_ref_count;
extern lv_obj_t *g_v5_axis_table_keyboard_overlay;
extern lv_obj_t *g_v5_axis_table_keyboard_value_label;
extern V5AxisCellRef *g_v5_axis_table_keyboard_ref;
extern char g_v5_axis_table_keyboard_value[64];
extern V5UiFirstFrameGuard g_v5_axis_table_keyboard_frame_guard;
extern lv_obj_t *g_v5_axis_table_axis_scroll;
extern V5SettingsAxisCommitCallback g_v5_axis_table_commit_callback;
extern void *g_v5_axis_table_commit_callback_user_data;
extern V5SettingsAxisZeroCallback g_v5_axis_table_axis_zero_callback;
extern void *g_v5_axis_table_axis_zero_callback_user_data;

void v5_settings_axis_refresh_axis_cell_ref(V5AxisCellRef *ref);

void v5_settings_axis_trim_in_place(char *text);

int v5_settings_axis_same_key(const char *a, const char *b);

int v5_settings_axis_axis_field_requires_integer_value(const char *field_key);

int v5_settings_axis_axis_integer_text_is_valid(const char *value);

unsigned int v5_settings_axis_axis_letter_index(char axis);

int v5_settings_axis_column_index(const char *field_key);

void v5_settings_axis_set_value(unsigned int row, const char *field_key, const char *value, int real);

void v5_settings_axis_set_value_from_double(unsigned int row, const char *field_key, double value);

void v5_settings_axis_set_max_velocity_from_runtime_ini(unsigned int row, const char *value);

void v5_settings_axis_clear_slave_options(void);

void v5_settings_axis_clear_slave_drive_display(void);

void v5_settings_axis_notify_settings_axis_commit_success(void);

void v5_settings_axis_clear_values(void);

void v5_settings_axis_copy_axis_value(char *dst, size_t cap, const char *value);

int v5_settings_axis_next_text_line(const char **cursor, char *line, size_t cap);

unsigned int v5_settings_axis_settings_axis_owner_generation(const char *field_id);

unsigned int v5_settings_axis_settings_axis_readback_token(const char *field_id, const char *value);

void v5_settings_axis_set_resident_parameter_table(V5SettingsParameterDiskTable table, const V5BootClosureResidentText *blob);

int v5_settings_axis_resident_parameter_table_read_axis(V5SettingsParameterDiskTable table,
                                              const char *axis,
                                              const char *field_key,
                                              char *out,
                                              size_t out_cap);

int v5_settings_axis_resident_parameter_table_set_axis(
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    const char *value);

int v5_settings_axis_row_index_for_axis_name(const char *axis);

void v5_settings_axis_slave_option_extract_id(const char *text, char *out, size_t cap);

int v5_settings_axis_slave_id_is_nat(const char *id);

int v5_settings_axis_slave_option_is_nat(const char *text);

int v5_settings_axis_slave_option_same_id(const char *a, const char *b);

void v5_settings_axis_load_slave_options_from_resident_self_table(void);

void v5_settings_axis_reload_slave_options_from_resident_self_table(void);

void v5_settings_axis_set_g53_value(unsigned int row, unsigned int col, const char *value, int real);

const char *v5_settings_axis_g53_field_key(unsigned int row, unsigned int col);

void v5_settings_axis_format_double_compact(char *out, size_t cap, double value);

int v5_settings_axis_g53_field_id(unsigned int row, unsigned int col, char *out, size_t out_cap);

void v5_settings_axis_load_g53_disk_parameter_tables(void);

void v5_settings_axis_set_motion_model_value(const char *value, int real);

void v5_settings_axis_load_self_setting_parameter_table(void);

void v5_settings_axis_parse_runtime_ini_text(const char *text);

void v5_settings_axis_mark_missing_encoder_bits_unavailable(void);

const char *v5_settings_axis_row_value(unsigned int row, const char *field_key);

int v5_settings_axis_row_slave_matches_option(unsigned int row, const char *option);

int v5_settings_axis_slave_option_used_by_other_row(unsigned int current_row, const char *option);

int v5_settings_axis_axis_row_slave_is_nat(unsigned int row);

void v5_settings_axis_apply_drive_display_for_row(unsigned int row);

int v5_settings_axis_axis_cell_disabled_by_nat(unsigned int row, unsigned int col);

lv_color_t v5_settings_axis_rgb(unsigned char r, unsigned char g, unsigned char b);

void v5_settings_axis_plain_obj(lv_obj_t *obj);

lv_obj_t *v5_settings_axis_panel(lv_obj_t *parent, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);

lv_obj_t *v5_settings_axis_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b);

lv_color_t v5_settings_axis_field_color_for_cell(unsigned int row, unsigned int col);

lv_color_t v5_settings_axis_axis_label_color_for_row(unsigned int row);

const char *v5_settings_axis_initial_value(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col);

lv_obj_t *v5_settings_axis_value_cell(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_color_t text_color, int disabled, const char *debug_id);

V5AxisCellRef *v5_settings_axis_store_cell_ref(unsigned int kind, unsigned int row, unsigned int col, lv_obj_t *obj, lv_obj_t *label_obj, const char *field_id);

void v5_settings_axis_apply_axis_row_label_state(lv_obj_t *obj, lv_obj_t *label_obj, unsigned int row);

void v5_settings_axis_apply_axis_cell_state(lv_obj_t *obj, lv_obj_t *label_obj, unsigned int row, unsigned int col);

void v5_settings_axis_refresh_axis_row_refs(unsigned int row);

int v5_settings_axis_current_driver_mode_is_pulse(void);

int v5_settings_axis_axis_zero_col_index(void);

unsigned int v5_settings_axis_dropdown_selected_for_value(const char *options, const char *value);

int v5_settings_axis_dropdown_options_for_cell(const V5SettingsAxisColumnSpec *col, unsigned int row, const char *value, char *out, size_t cap);

void v5_settings_axis_dropdown_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, const V5SettingsAxisColumnSpec *col, lv_color_t text_color, const char *debug_id);

void v5_settings_axis_close_keyboard_overlay(void);

void v5_settings_axis_create_keyboard_overlay(void);

void v5_settings_axis_open_keyboard_for_ref(V5AxisCellRef *ref);

void v5_settings_axis_action_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id);

void v5_settings_axis_edit_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id);

#endif
