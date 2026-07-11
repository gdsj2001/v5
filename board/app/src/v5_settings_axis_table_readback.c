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

const char *v5_settings_axis_row_value(unsigned int row, const char *field_key)
{
    int c = v5_settings_axis_column_index(field_key);
    if (row >= V5_AXIS_TABLE_MAX_ROWS || c < 0) {
        return 0;
    }
    return g_v5_axis_table_value_real[row][(unsigned int)c] ? g_v5_axis_table_values[row][(unsigned int)c] : 0;
}

int v5_settings_axis_row_slave_matches_option(unsigned int row, const char *option)
{
    char option_id[V5_AXIS_VALUE_CAP];
    char value_id[V5_AXIS_VALUE_CAP];
    if (row >= v5_settings_axis_table_row_count()) return 0;
    v5_settings_axis_slave_option_extract_id(option, option_id, sizeof(option_id));
    if (!option_id[0] || v5_settings_axis_slave_id_is_nat(option_id)) return 0;
    v5_settings_axis_slave_option_extract_id(v5_settings_axis_row_value(row, "slave"), value_id, sizeof(value_id));
    return value_id[0] && !v5_settings_axis_slave_id_is_nat(value_id) && strcmp(option_id, value_id) == 0;
}

int v5_settings_axis_slave_option_used_by_other_row(unsigned int current_row, const char *option)
{
    unsigned int row;
    for (row = 0U; row < v5_settings_axis_table_row_count(); ++row) {
        if (row == current_row) continue;
        if (v5_settings_axis_row_slave_matches_option(row, option)) {
            return 1;
        }
    }
    return 0;
}

static int axis_column_is_slave(unsigned int col)
{
    return col < v5_settings_axis_table_column_count() &&
           g_v5_axis_table_columns[col].field_key &&
           strcmp(g_v5_axis_table_columns[col].field_key, "slave") == 0;
}

int v5_settings_axis_axis_row_slave_is_nat(unsigned int row)
{
    char slave_id[V5_AXIS_VALUE_CAP];
    if (row >= v5_settings_axis_table_row_count()) {
        return 0;
    }
    v5_settings_axis_slave_option_extract_id(v5_settings_axis_row_value(row, "slave"), slave_id, sizeof(slave_id));
    return v5_settings_axis_slave_id_is_nat(slave_id);
}

static int drive_display_field_index(const char *field_key)
{
    if (v5_settings_axis_same_key(field_key, "egear_numerator")) return 0;
    if (v5_settings_axis_same_key(field_key, "egear_denominator")) return 1;
    if (v5_settings_axis_same_key(field_key, "write_status")) return 2;
    return -1;
}

static int drive_display_field_is_slave_owned(const char *field_key)
{
    return drive_display_field_index(field_key) >= 0;
}

static void drive_display_slave_key(const char *slave_id, char *out, size_t cap)
{
    if (!out || cap == 0U) return;
    out[0] = '\0';
    if (!slave_id || !slave_id[0] || v5_settings_axis_slave_id_is_nat(slave_id)) return;
    snprintf(out, cap, "SLAVE_%s", slave_id);
}

static V5SlaveDriveDisplay *slave_drive_display_for_id(const char *slave_id, int create)
{
    unsigned int i;
    if (!slave_id || !slave_id[0] || v5_settings_axis_slave_id_is_nat(slave_id)) {
        return 0;
    }
    for (i = 0U; i < g_v5_axis_table_slave_drive_display_count; ++i) {
        if (strcmp(g_v5_axis_table_slave_drive_display[i].slave_id, slave_id) == 0) {
            return &g_v5_axis_table_slave_drive_display[i];
        }
    }
    if (!create || g_v5_axis_table_slave_drive_display_count >= V5_SLAVE_OPTION_MAX) {
        return 0;
    }
    snprintf(g_v5_axis_table_slave_drive_display[g_v5_axis_table_slave_drive_display_count].slave_id,
             sizeof(g_v5_axis_table_slave_drive_display[g_v5_axis_table_slave_drive_display_count].slave_id),
             "%s",
             slave_id);
    return &g_v5_axis_table_slave_drive_display[g_v5_axis_table_slave_drive_display_count++];
}

static void slave_drive_display_set_field(V5SlaveDriveDisplay *display,
                                          const char *field_key,
                                          const char *value,
                                          int real)
{
    if (!display || !field_key || !value || !value[0]) return;
    if (v5_settings_axis_same_key(field_key, "egear_numerator")) {
        snprintf(display->egear_numerator, sizeof(display->egear_numerator), "%s", value);
        display->numerator_real = real ? 1U : 0U;
    } else if (v5_settings_axis_same_key(field_key, "egear_denominator")) {
        snprintf(display->egear_denominator, sizeof(display->egear_denominator), "%s", value);
        display->denominator_real = real ? 1U : 0U;
    } else if (v5_settings_axis_same_key(field_key, "write_status")) {
        snprintf(display->write_status, sizeof(display->write_status), "%s", value);
        display->status_real = real ? 1U : 0U;
    }
}

static void cache_drive_display_row_for_slave(const char *slave_id, const char *row_key)
{
    static const char *fields[] = {"egear_numerator", "egear_denominator", "write_status"};
    V5SlaveDriveDisplay *display;
    char value[V5_AXIS_VALUE_CAP];
    unsigned int i;
    if (!slave_id || !slave_id[0] || v5_settings_axis_slave_id_is_nat(slave_id) || !row_key || !row_key[0]) {
        return;
    }
    display = slave_drive_display_for_id(slave_id, 1);
    if (!display) return;
    for (i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (v5_settings_axis_resident_parameter_table_read_axis(V5_SETTINGS_PARAMETER_DISK_DRIVE,
                                               row_key,
                                               fields[i],
                                               value,
                                               sizeof(value))) {
            slave_drive_display_set_field(display, fields[i], value, 1);
        }
    }
}

void v5_settings_axis_apply_drive_display_for_row(unsigned int row)
{
    char slave_id[V5_AXIS_VALUE_CAP];
    V5SlaveDriveDisplay *display;
    if (row >= v5_settings_axis_table_row_count()) {
        return;
    }
    slave_id[0] = '\0';
    v5_settings_axis_slave_option_extract_id(v5_settings_axis_row_value(row, "slave"), slave_id, sizeof(slave_id));
    display = slave_drive_display_for_id(slave_id, 0);
    if (!display || v5_settings_axis_slave_id_is_nat(slave_id)) {
        v5_settings_axis_set_value(row, "egear_numerator", "--", 0);
        v5_settings_axis_set_value(row, "egear_denominator", "--", 0);
        v5_settings_axis_set_value(row, "write_status", "--", 0);
        return;
    }
    v5_settings_axis_set_value(row, "egear_numerator", display->egear_numerator[0] ? display->egear_numerator : "--", display->numerator_real);
    v5_settings_axis_set_value(row, "egear_denominator", display->egear_denominator[0] ? display->egear_denominator : "--", display->denominator_real);
    v5_settings_axis_set_value(row, "write_status", display->write_status[0] ? display->write_status : "--", display->status_real);
}

int v5_settings_axis_axis_cell_disabled_by_nat(unsigned int row, unsigned int col)
{
    return v5_settings_axis_axis_row_slave_is_nat(row) && !axis_column_is_slave(col);
}

int v5_settings_axis_table_cell_is_disabled(unsigned int row, unsigned int col)
{
    if (row >= v5_settings_axis_table_row_count() || col >= v5_settings_axis_table_column_count()) {
        return 1;
    }
    if (!g_v5_axis_table_rows[row].enabled || g_v5_axis_table_columns[col].kind == V5_AXIS_FIELD_READONLY) {
        return 1;
    }
    return v5_settings_axis_axis_cell_disabled_by_nat(row, col);
}

static V5SettingsParameterDiskTable disk_table_for_column(const V5SettingsAxisColumnSpec *col)
{
    if (!col) return V5_SETTINGS_PARAMETER_DISK_NONE;
    if (col->drive_only_allowed || col->owner == V5_PARAMETER_OWNER_DRIVE_ONLY) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (col->owner == V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    return V5_SETTINGS_PARAMETER_DISK_NONE;
}

static int should_read_disk_table_for_column(V5SettingsParameterDiskTable table, const V5SettingsAxisColumnSpec *col)
{
    if (!col || !col->field_key) return 0;
    if (strcmp(col->field_key, "slave") == 0) return table == V5_SETTINGS_PARAMETER_DISK_SELF;
    if (strcmp(col->field_key, "encoder_bits") == 0) return table == V5_SETTINGS_PARAMETER_DISK_DRIVE;
    if (drive_display_field_is_slave_owned(col->field_key)) return 0;
    return disk_table_for_column(col) == table;
}

static void load_disk_parameter_table(V5SettingsParameterDiskTable table)
{
    unsigned int r;
    unsigned int c;
    char value[V5_AXIS_VALUE_CAP];
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        for (c = 0U; c < v5_settings_axis_table_column_count(); ++c) {
            const V5SettingsAxisColumnSpec *col = &g_v5_axis_table_columns[c];
            if (!should_read_disk_table_for_column(table, col)) {
                continue;
            }
            if (v5_settings_axis_resident_parameter_table_read_axis(table, g_v5_axis_table_rows[r].axis, col->field_key, value, sizeof(value))) {
                v5_settings_axis_set_value(r, col->field_key, value, 1);
            }
        }
    }
}

static void load_slave_drive_display_cache(void)
{
    unsigned int r;
    v5_settings_axis_clear_slave_drive_display();
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        char slave_id[V5_AXIS_VALUE_CAP];
        char slave_key[48];
        const char *slave_value = v5_settings_axis_row_value(r, "slave");
        slave_id[0] = '\0';
        slave_key[0] = '\0';
        v5_settings_axis_slave_option_extract_id(slave_value, slave_id, sizeof(slave_id));
        if (!slave_id[0] || v5_settings_axis_slave_id_is_nat(slave_id)) {
            continue;
        }
        drive_display_slave_key(slave_id, slave_key, sizeof(slave_key));
        cache_drive_display_row_for_slave(slave_id, g_v5_axis_table_rows[r].axis);
        cache_drive_display_row_for_slave(slave_id, slave_key);
    }
}

static void apply_drive_display_for_all_rows(void)
{
    unsigned int r;
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        v5_settings_axis_apply_drive_display_for_row(r);
    }
}

void v5_settings_axis_table_load_boot_closure(const V5BootClosure *closure)
{
    if (closure && closure->project_root[0]) {
        snprintf(g_v5_axis_table_project_root, sizeof(g_v5_axis_table_project_root), "%s", closure->project_root);
    } else {
        snprintf(g_v5_axis_table_project_root, sizeof(g_v5_axis_table_project_root), ".");
    }
    v5_settings_axis_clear_values();
    v5_settings_axis_set_resident_parameter_table(V5_SETTINGS_PARAMETER_DISK_SELF, closure ? &closure->self_parameter_table : 0);
    v5_settings_axis_set_resident_parameter_table(V5_SETTINGS_PARAMETER_DISK_DRIVE, closure ? &closure->drive_parameter_table : 0);
    if (closure && closure->runtime_ini.loaded) {
        v5_settings_axis_parse_runtime_ini_text(closure->runtime_ini.text);
    }
    v5_settings_axis_load_self_setting_parameter_table();
    load_disk_parameter_table(V5_SETTINGS_PARAMETER_DISK_SELF);
    load_disk_parameter_table(V5_SETTINGS_PARAMETER_DISK_DRIVE);
    load_slave_drive_display_cache();
    apply_drive_display_for_all_rows();
    v5_settings_axis_load_g53_disk_parameter_tables();
    v5_settings_axis_mark_missing_encoder_bits_unavailable();
    g_v5_axis_table_loaded = 1;
}

void v5_settings_axis_table_load_readback(const char *project_root)
{
    static V5BootClosure closure;
    v5_boot_closure_load(&closure, project_root);
    v5_settings_axis_table_load_boot_closure(&closure);
}

const char *v5_settings_axis_table_value(unsigned int row, unsigned int col)
{
    if (!g_v5_axis_table_loaded) {
        v5_settings_axis_clear_values();
        g_v5_axis_table_loaded = 1;
    }
    if (row >= V5_AXIS_TABLE_MAX_ROWS || col >= V5_AXIS_TABLE_MAX_COLS) {
        return "--";
    }
    return g_v5_axis_table_values[row][col][0] ? g_v5_axis_table_values[row][col] : "--";
}

int v5_settings_axis_table_value_is_real(unsigned int row, unsigned int col)
{
    if (row >= V5_AXIS_TABLE_MAX_ROWS || col >= V5_AXIS_TABLE_MAX_COLS) {
        return 0;
    }
    return g_v5_axis_table_value_real[row][col] ? 1 : 0;
}


unsigned int v5_settings_axis_table_slave_option_count(void)
{
    return g_v5_axis_table_slave_option_count;
}

const char *v5_settings_axis_table_slave_option(unsigned int index)
{
    if (index >= g_v5_axis_table_slave_option_count) {
        return "";
    }
    return g_v5_axis_table_slave_options[index];
}
