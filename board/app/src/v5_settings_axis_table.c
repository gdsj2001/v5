#include "v5_settings_axis_table.h"
#include "v5_boot_closure.h"
#include "v5_button_visuals.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_parameter_store.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_axis_table_internal.h"

const V5SettingsAxisRowSpec g_v5_axis_table_rows[] = {
    {"X", "X 直线", "直线", 1},
    {"Y", "Y 直线", "直线", 1},
    {"Z", "Z 直线", "直线", 1},
    {"A", "A 旋转", "旋转", 1},
    {"B", "B 旋转", "旋转", 1},
    {"C", "C 旋转", "旋转", 1},
    {"GANTRY", "龙门", "从轴", 1},
    {"TOOLMAG", "刀库", "刀库", 1},
};

char g_v5_axis_table_values[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS][V5_AXIS_VALUE_CAP];
unsigned char g_v5_axis_table_value_real[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS];
int g_v5_axis_table_loaded;
char g_v5_axis_table_g53_values[V5_G53_ROW_COUNT][3U][V5_AXIS_VALUE_CAP];
unsigned char g_v5_axis_table_g53_value_real[V5_G53_ROW_COUNT][3U];
char g_v5_axis_table_motion_model_value[V5_AXIS_VALUE_CAP];
unsigned char g_v5_axis_table_motion_model_real;
char g_v5_axis_table_bus_pulse_value[V5_AXIS_VALUE_CAP];
unsigned char g_v5_axis_table_bus_pulse_real;
char g_v5_axis_table_slave_options[V5_SLAVE_OPTION_MAX][V5_SLAVE_OPTION_CAP];
unsigned int g_v5_axis_table_slave_option_count;
char g_v5_axis_table_project_root[256] = ".";
char g_v5_axis_table_self_parameter_text[V5_BOOT_CLOSURE_TEXT_CAP];
char g_v5_axis_table_drive_parameter_text[V5_BOOT_CLOSURE_TEXT_CAP];

V5SlaveDriveDisplay g_v5_axis_table_slave_drive_display[V5_SLAVE_OPTION_MAX];
unsigned int g_v5_axis_table_slave_drive_display_count;

V5AxisCellRef g_v5_axis_table_cell_refs[V5_AXIS_CELL_REF_MAX];
unsigned int g_v5_axis_table_cell_ref_count;
lv_obj_t *g_v5_axis_table_keyboard_overlay;
lv_obj_t *g_v5_axis_table_keyboard_value_label;
V5AxisCellRef *g_v5_axis_table_keyboard_ref;
char g_v5_axis_table_keyboard_value[64];
V5UiFirstFrameGuard g_v5_axis_table_keyboard_frame_guard;
lv_obj_t *g_v5_axis_table_axis_scroll;

void v5_settings_axis_trim_in_place(char *text)
{
    char *start = text;
    char *end;
    if (!text) {
        return;
    }
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    *end = '\0';
}

int v5_settings_axis_same_key(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

int v5_settings_axis_axis_field_requires_integer_value(const char *field_key)
{
    return v5_settings_axis_same_key(field_key, "pitch") ||
           v5_settings_axis_same_key(field_key, "motor_rev") ||
           v5_settings_axis_same_key(field_key, "load_rev") ||
           v5_settings_axis_same_key(field_key, "soft_minus") ||
           v5_settings_axis_same_key(field_key, "soft_plus") ||
           v5_settings_axis_same_key(field_key, "max_velocity") ||
           v5_settings_axis_same_key(field_key, "max_acceleration") ||
           v5_settings_axis_same_key(field_key, "backlash");
}

int v5_settings_axis_axis_integer_text_is_valid(const char *value)
{
    char buf[64];
    const char *p;
    if (!value || !value[0] || strlen(value) >= sizeof(buf)) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    v5_settings_axis_trim_in_place(buf);
    p = buf;
    if (*p == '+' || *p == '-') {
        ++p;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    while (*p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int axis_number_from_text(const char *value, double *out)
{
    char buf[64];
    char *end;
    double numeric;
    if (!value || !value[0] || strlen(value) >= sizeof(buf)) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    v5_settings_axis_trim_in_place(buf);
    numeric = strtod(buf, &end);
    if (end == buf || *end != '\0' || !isfinite(numeric)) {
        return 0;
    }
    if (out) {
        *out = numeric;
    }
    return 1;
}

static void axis_format_integer(char *out, size_t cap, double value)
{
    double rounded;
    if (!out || cap == 0U || !isfinite(value)) {
        return;
    }
    rounded = (double)((long long)(value >= 0.0 ? value + 0.5 : value - 0.5));
    if (rounded == 0.0) {
        rounded = 0.0;
    }
    snprintf(out, cap, "%.0f", rounded);
}

static const char *axis_display_value_for_field(const char *field_key, const char *value, char *scratch, size_t scratch_cap)
{
    double numeric;
    if (v5_settings_axis_axis_field_requires_integer_value(field_key) &&
        axis_number_from_text(value, &numeric) &&
        scratch && scratch_cap > 0U) {
        axis_format_integer(scratch, scratch_cap, numeric);
        return scratch;
    }
    return value;
}

unsigned int v5_settings_axis_axis_letter_index(char axis)
{
    switch (axis) {
    case 'X': return 0U;
    case 'Y': return 1U;
    case 'Z': return 2U;
    case 'A': return 3U;
    case 'B': return 4U;
    case 'C': return 5U;
    default: return V5_AXIS_TABLE_MAX_ROWS;
    }
}

int v5_settings_axis_column_index(const char *field_key)
{
    unsigned int i;
    for (i = 0U; i < v5_settings_axis_table_column_count(); ++i) {
        if (v5_settings_axis_same_key(g_v5_axis_table_columns[i].field_key, field_key)) {
            return (int)i;
        }
    }
    return -1;
}

void v5_settings_axis_set_value(unsigned int row, const char *field_key, const char *value, int real)
{
    int c = v5_settings_axis_column_index(field_key);
    char display[V5_AXIS_VALUE_CAP];
    const char *stored;
    if (row >= V5_AXIS_TABLE_MAX_ROWS || c < 0 || !value || !value[0]) {
        return;
    }
    stored = axis_display_value_for_field(field_key, value, display, sizeof(display));
    snprintf(g_v5_axis_table_values[row][(unsigned int)c], V5_AXIS_VALUE_CAP, "%s", stored);
    g_v5_axis_table_value_real[row][(unsigned int)c] = real ? 1U : 0U;
}

void v5_settings_axis_set_value_from_double(unsigned int row, const char *field_key, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", value);
    for (char *p = buf + strlen(buf); p > buf && p[-1] == '0'; --p) {
        p[-1] = '\0';
    }
    if (buf[strlen(buf) - 1U] == '.') {
        buf[strlen(buf) - 1U] = '\0';
    }
    v5_settings_axis_set_value(row, field_key, buf, 1);
}

void v5_settings_axis_set_max_velocity_from_runtime_ini(unsigned int row, const char *value)
{
    char *end;
    double units_per_s;
    double display_value;
    double rounded;
    double delta;
    if (!value || !value[0]) {
        return;
    }
    units_per_s = strtod(value, &end);
    if (end == value || *end != '\0' || !isfinite(units_per_s)) {
        return;
    }
    display_value = units_per_s * 60.0;
    rounded = (double)((long long)(display_value >= 0.0 ? display_value + 0.5 : display_value - 0.5));
    delta = display_value - rounded;
    if (delta < 0.0) {
        delta = -delta;
    }
    if (delta < 0.0001) {
        display_value = rounded;
    }
    v5_settings_axis_set_value_from_double(row, "max_velocity", display_value);
}

void v5_settings_axis_clear_slave_options(void)
{
    unsigned int r;
    g_v5_axis_table_slave_option_count = 0U;
    for (r = 0U; r < V5_SLAVE_OPTION_MAX; ++r) {
        g_v5_axis_table_slave_options[r][0] = '\0';
    }
}

void v5_settings_axis_clear_slave_drive_display(void)
{
    unsigned int i;
    g_v5_axis_table_slave_drive_display_count = 0U;
    for (i = 0U; i < V5_SLAVE_OPTION_MAX; ++i) {
        memset(&g_v5_axis_table_slave_drive_display[i], 0, sizeof(g_v5_axis_table_slave_drive_display[i]));
    }
}


V5SettingsAxisCommitCallback g_v5_axis_table_commit_callback;
void *g_v5_axis_table_commit_callback_user_data;
V5SettingsAxisZeroCallback g_v5_axis_table_axis_zero_callback;
void *g_v5_axis_table_axis_zero_callback_user_data;

void v5_settings_axis_table_set_commit_callback(V5SettingsAxisCommitCallback cb, void *user_data)
{
    g_v5_axis_table_commit_callback = cb;
    g_v5_axis_table_commit_callback_user_data = user_data;
}

void v5_settings_axis_table_set_axis_zero_callback(V5SettingsAxisZeroCallback cb, void *user_data)
{
    g_v5_axis_table_axis_zero_callback = cb;
    g_v5_axis_table_axis_zero_callback_user_data = user_data;
}

void v5_settings_axis_notify_settings_axis_commit_success(void)
{
    if (g_v5_axis_table_commit_callback) {
        g_v5_axis_table_commit_callback(g_v5_axis_table_commit_callback_user_data);
    }
}
