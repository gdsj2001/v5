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

static int option_line_matches_value(const char *start, size_t len, const char *value)
{
    char option[64];
    char *endptr_a;
    char *endptr_b;
    long a;
    long b;
    if (!start || !value || !value[0]) return 0;
    if (len >= sizeof(option)) len = sizeof(option) - 1U;
    memcpy(option, start, len);
    option[len] = '\0';
    v5_settings_axis_trim_in_place(option);
    if (strcmp(option, value) == 0) return 1;
    a = strtol(option, &endptr_a, 10);
    b = strtol(value, &endptr_b, 10);
    return endptr_a != option && endptr_b != value && a == b;
}

unsigned int v5_settings_axis_dropdown_selected_for_value(const char *options, const char *value)
{
    const char *start;
    unsigned int index = 0U;
    if (!options || !options[0] || !value || !value[0]) return 0U;
    start = options;
    while (*start) {
        const char *end = strchr(start, '\n');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (option_line_matches_value(start, len, value)) {
            return index;
        }
        if (!end) break;
        start = end + 1;
        ++index;
    }
    return 0U;
}

static int append_dropdown_option(char *out, size_t cap, const char *value)
{
    size_t len;
    if (!out || cap == 0U || !value || !value[0]) return 0;
    len = strlen(out);
    if (len > 0U) {
        if (len + 1U >= cap) return 0;
        out[len++] = '\n';
        out[len] = '\0';
    }
    if (len + strlen(value) + 1U > cap) return 0;
    strcat(out, value);
    return 1;
}

static int dropdown_options_contain_slave_id(const char *options, const char *value)
{
    const char *start;
    if (!options || !value || !value[0]) return 0;
    start = options;
    while (*start) {
        const char *end = strchr(start, '\n');
        char line[V5_SLAVE_OPTION_CAP];
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (len >= sizeof(line)) len = sizeof(line) - 1U;
        memcpy(line, start, len);
        line[len] = '\0';
        if (v5_settings_axis_slave_option_same_id(line, value)) {
            return 1;
        }
        if (!end) break;
        start = end + 1;
    }
    return 0;
}

static int append_unique_slave_dropdown_option(char *out, size_t cap, const char *value)
{
    if (dropdown_options_contain_slave_id(out, value)) {
        return 1;
    }
    return append_dropdown_option(out, cap, value);
}

static int append_slave_dropdown_options_for_row(unsigned int row, char *out, size_t cap)
{
    unsigned int i;
    const char *current_slave;
    char current_id[V5_AXIS_VALUE_CAP];
    int ok = append_dropdown_option(out, cap, "NAT");
    current_id[0] = '\0';
    current_slave = v5_settings_axis_row_value(row, "slave");
    v5_settings_axis_slave_option_extract_id(current_slave, current_id, sizeof(current_id));
    for (i = 0U; i < g_v5_axis_table_slave_option_count; ++i) {
        if (v5_settings_axis_slave_option_is_nat(g_v5_axis_table_slave_options[i])) continue;
        if (!v5_settings_axis_row_slave_matches_option(row, g_v5_axis_table_slave_options[i]) &&
            v5_settings_axis_slave_option_used_by_other_row(row, g_v5_axis_table_slave_options[i])) {
            continue;
        }
        ok = append_unique_slave_dropdown_option(out, cap, g_v5_axis_table_slave_options[i]) && ok;
    }
    if (g_v5_axis_table_slave_option_count > 0U && current_id[0] && !v5_settings_axis_slave_id_is_nat(current_id)) {
        ok = append_unique_slave_dropdown_option(out, cap, current_id) && ok;
    }
    return ok;
}

int v5_settings_axis_dropdown_options_for_cell(const V5SettingsAxisColumnSpec *col, unsigned int row, const char *value, char *out, size_t cap)
{
    if (!col || !out || cap == 0U) return 0;
    out[0] = '\0';
    if (strcmp(col->field_key, "axis_mode") == 0) {
        snprintf(out, cap, "直线\n旋转\n虚拟");
    } else if (strcmp(col->field_key, "direction_mode") == 0) {
        snprintf(out, cap, "cw\nccw");
    } else if (strcmp(col->field_key, "slave") == 0) {
        return append_slave_dropdown_options_for_row(row, out, cap);
    } else if (strcmp(col->field_key, "home_order") == 0) {
        snprintf(out, cap, "禁用\n0\n1\n2\n3\n4\n5\n6\n7");
    } else if (strcmp(col->field_key, "home_direction") == 0) {
        snprintf(out, cap, "0\n+\n-");
    } else if (strcmp(col->field_key, "encoder_bits") == 0) {
        snprintf(out, cap, "24\n23\n22\n21\n20\n19\n18\n17\n16");
    }
    (void)value;
    return out[0] != '\0';
}

int v5_settings_axis_table_dropdown_options(unsigned int row, unsigned int col, char *out, size_t cap)
{
    if (!out || cap == 0U) return 0;
    out[0] = '\0';
    if (row >= v5_settings_axis_table_row_count() || col >= v5_settings_axis_table_column_count()) {
        return 0;
    }
    return v5_settings_axis_dropdown_options_for_cell(&g_v5_axis_table_columns[col], row, v5_settings_axis_table_value(row, col), out, cap);
}

void v5_settings_axis_refresh_axis_cell_ref(V5AxisCellRef *ref)
{
    const V5SettingsAxisColumnSpec *col;
    const char *value;
    if (!ref) return;
    if (ref->kind == V5_CELL_KIND_G53) {
        value = v5_settings_axis_table_g53_value(ref->row, ref->col);
        if (ref->label) {
            lv_label_set_text(ref->label, value);
        }
        if (ref->obj) {
            lv_obj_invalidate(ref->obj);
        }
        return;
    }
    if (ref->kind == V5_CELL_KIND_AXIS_LABEL) {
        if (ref->row >= v5_settings_axis_table_row_count()) {
            return;
        }
        if (ref->label) {
            lv_label_set_text(ref->label, g_v5_axis_table_rows[ref->row].label);
        }
        v5_settings_axis_apply_axis_row_label_state(ref->obj, ref->label, ref->row);
        if (ref->obj) {
            lv_obj_invalidate(ref->obj);
        }
        return;
    }
    if (ref->row >= v5_settings_axis_table_row_count() ||
        ref->col >= v5_settings_axis_table_column_count()) {
        return;
    }
    col = &g_v5_axis_table_columns[ref->col];
    value = v5_settings_axis_table_value(ref->row, ref->col);
    if (col->kind == V5_AXIS_FIELD_SELECT && ref->obj) {
        char options[V5_DROPDOWN_OPTIONS_CAP];
        if (v5_settings_axis_dropdown_options_for_cell(col, ref->row, value, options, sizeof(options))) {
            lv_dropdown_set_options(ref->obj, options);
            lv_dropdown_set_selected(ref->obj, (uint16_t)v5_settings_axis_dropdown_selected_for_value(options, value));
        }
    } else if (col->kind == V5_AXIS_FIELD_ACTION && ref->label) {
        lv_label_set_text(ref->label, v5_settings_axis_current_driver_mode_is_pulse() ? "设零" : "设0");
    } else if (ref->label) {
        lv_label_set_text(ref->label, value);
    }
    if (ref->obj) {
        v5_settings_axis_apply_axis_cell_state(ref->obj, ref->label, ref->row, ref->col);
        lv_obj_invalidate(ref->obj);
    }
}

void v5_settings_axis_table_reload_current_readback(void)
{
    char project_root[sizeof(g_v5_axis_table_project_root)];
    lv_coord_t scroll_x = 0;
    unsigned int i;
    snprintf(project_root, sizeof(project_root), "%s", g_v5_axis_table_project_root[0] ? g_v5_axis_table_project_root : ".");
    if (g_v5_axis_table_axis_scroll) {
        scroll_x = lv_obj_get_scroll_x(g_v5_axis_table_axis_scroll);
    }
    v5_settings_axis_table_load_readback(project_root);
    for (i = 0U; i < g_v5_axis_table_cell_ref_count; ++i) {
        v5_settings_axis_refresh_axis_cell_ref(&g_v5_axis_table_cell_refs[i]);
    }
    if (g_v5_axis_table_axis_scroll) {
        lv_obj_scroll_to_x(g_v5_axis_table_axis_scroll, scroll_x, LV_ANIM_OFF);
        lv_obj_invalidate(g_v5_axis_table_axis_scroll);
    }
}
