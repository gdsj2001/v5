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

const V5SettingsAxisColumnSpec g_v5_axis_table_columns[] = {
    {"axis_mode", "轴模式", 0, 94, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"direction_mode", "方向", 100, 62, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"slave", "从站", 168, 98, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE, V5_PARAMETER_READBACK_SELF_PARAMETER_TABLE, 0},
    {"precision", "目标精度\n单位/count", 272, 112, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"pitch", "螺距", 390, 76, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"motor_rev", "电机", 472, 74, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"load_rev", "负载", 552, 74, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"home_order", "回零次序", 632, 90, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"home_direction", "回零方向", 728, 90, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"soft_minus", "-软限位", 824, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"zero", "零位", 930, 80, V5_AXIS_FIELD_ACTION, V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 0},
    {"soft_plus", "+软限位", 1016, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"max_velocity", "速度", 1122, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"max_acceleration", "加速", 1228, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"backlash", "反向间隙", 1334, 96, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"encoder_bits", "bit", 1440, 70, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"egear_numerator", "分子", 1516, 92, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"egear_denominator", "分母", 1614, 92, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"write_status", "写入状态", 1712, 132, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
};

lv_color_t v5_settings_axis_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return lv_color_make(r, g, b);
}

void v5_settings_axis_plain_obj(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *v5_settings_axis_panel(lv_obj_t *parent, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    lv_obj_t *obj = lv_obj_create(parent);
    v5_settings_axis_plain_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, v5_settings_axis_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t *v5_settings_axis_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text ? text : "");
    lv_obj_set_style_text_color(obj, v5_settings_axis_rgb(r, g, b), 0);
    return obj;
}

static lv_color_t axis_color(const V5SettingsAxisRowSpec *row)
{
    if (!row || !row->axis) {
        return v5_settings_axis_rgb(226, 238, 246);
    }
    if (strcmp(row->axis, "X") == 0 || strcmp(row->axis, "A") == 0) {
        return v5_settings_axis_rgb(255, 100, 106);
    }
    if (strcmp(row->axis, "Y") == 0) {
        return v5_settings_axis_rgb(0, 232, 150);
    }
    if (strcmp(row->axis, "Z") == 0) {
        return v5_settings_axis_rgb(82, 178, 255);
    }
    if (strcmp(row->axis, "C") == 0) {
        return v5_settings_axis_rgb(0, 225, 220);
    }
    return v5_settings_axis_rgb(226, 238, 246);
}

static lv_color_t field_color(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    if (!row || !row->enabled || !col) {
        return v5_settings_axis_rgb(150, 170, 190);
    }
    if (col->kind == V5_AXIS_FIELD_ACTION) {
        return v5_settings_axis_rgb(0, 230, 200);
    }
    if (col->kind == V5_AXIS_FIELD_SELECT) {
        return v5_settings_axis_rgb(255, 100, 106);
    }
    if (col->kind == V5_AXIS_FIELD_READONLY) {
        return v5_settings_axis_rgb(155, 177, 198);
    }
    return v5_settings_axis_rgb(226, 238, 246);
}

lv_color_t v5_settings_axis_field_color_for_cell(unsigned int row, unsigned int col)
{
    if (v5_settings_axis_table_cell_is_disabled(row, col)) {
        return v5_settings_axis_rgb(150, 170, 190);
    }
    return field_color(&g_v5_axis_table_rows[row], &g_v5_axis_table_columns[col]);
}

lv_color_t v5_settings_axis_axis_label_color_for_row(unsigned int row)
{
    if (row >= v5_settings_axis_table_row_count() ||
        !g_v5_axis_table_rows[row].enabled ||
        v5_settings_axis_axis_row_slave_is_nat(row)) {
        return v5_settings_axis_rgb(150, 170, 190);
    }
    return axis_color(&g_v5_axis_table_rows[row]);
}

const char *v5_settings_axis_initial_value(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    unsigned int r;
    unsigned int c;
    if (!row || !col) {
        return "--";
    }
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        if (&g_v5_axis_table_rows[r] == row) {
            for (c = 0U; c < v5_settings_axis_table_column_count(); ++c) {
                if (&g_v5_axis_table_columns[c] == col) {
                    return v5_settings_axis_table_value(r, c);
                }
            }
        }
    }
    return "--";
}

int v5_settings_axis_table_field_id(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col, char *out, size_t out_cap)
{
    if (!out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!row || !row->axis || !col || !col->field_key) {
        return 0;
    }
    snprintf(out, out_cap, "axis_%s_%s", row->axis, col->field_key);
    return out[0] != '\0';
}

int v5_settings_axis_table_field_matches_owner(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    char id[96];
    const V5ParameterField *field;

    if (!v5_settings_axis_table_field_id(row, col, id, sizeof(id))) {
        return 0;
    }
    field = v5_parameter_table_find(id);
    if (!field) {
        return 0;
    }
    return field->owner == col->owner &&
           field->readback == col->readback &&
           field->drive_only_allowed == col->drive_only_allowed;
}

lv_obj_t *v5_settings_axis_value_cell(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_color_t text_color, int disabled, const char *debug_id)
{
    lv_obj_t *cell = v5_settings_axis_panel(parent, x, y, w, h, disabled ? 34 : 5, disabled ? 47 : 27, disabled ? 58 : 43);
    lv_obj_t *text_label = v5_settings_axis_label(cell, text, 0, 5, w, h - 6, 226, 238, 246);
    (void)debug_id;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, v5_settings_axis_rgb(disabled ? 54 : 45, disabled ? 66 : 86, disabled ? 76 : 112), 0);
    return cell;
}

V5AxisCellRef *v5_settings_axis_store_cell_ref(unsigned int kind, unsigned int row, unsigned int col, lv_obj_t *obj, lv_obj_t *label_obj, const char *field_id)
{
    V5AxisCellRef *ref;
    if (g_v5_axis_table_cell_ref_count >= V5_AXIS_CELL_REF_MAX) {
        return 0;
    }
    ref = &g_v5_axis_table_cell_refs[g_v5_axis_table_cell_ref_count++];
    ref->kind = kind;
    ref->row = row;
    ref->col = col;
    ref->obj = obj;
    ref->label = label_obj;
    snprintf(ref->field_id, sizeof(ref->field_id), "%s", field_id ? field_id : "axis_param");
    return ref;
}

void v5_settings_axis_apply_axis_row_label_state(lv_obj_t *obj, lv_obj_t *label_obj, unsigned int row)
{
    int disabled = row >= v5_settings_axis_table_row_count() ||
                   !g_v5_axis_table_rows[row].enabled ||
                   v5_settings_axis_axis_row_slave_is_nat(row);
    lv_color_t text_color = v5_settings_axis_axis_label_color_for_row(row);
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, v5_settings_axis_rgb(disabled ? 34 : 5, disabled ? 47 : 27, disabled ? 58 : 43), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, v5_settings_axis_rgb(disabled ? 54 : 45, disabled ? 66 : 86, disabled ? 76 : 112), 0);
    if (label_obj) {
        lv_obj_set_style_text_color(label_obj, text_color, 0);
    }
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
}

void v5_settings_axis_apply_axis_cell_state(lv_obj_t *obj, lv_obj_t *label_obj, unsigned int row, unsigned int col)
{
    const V5SettingsAxisColumnSpec *col_spec;
    int disabled;
    lv_color_t text_color;
    if (!obj || row >= v5_settings_axis_table_row_count() || col >= v5_settings_axis_table_column_count()) {
        return;
    }
    col_spec = &g_v5_axis_table_columns[col];
    disabled = v5_settings_axis_table_cell_is_disabled(row, col);
    text_color = v5_settings_axis_field_color_for_cell(row, col);
    lv_obj_set_style_bg_color(obj, v5_settings_axis_rgb(disabled ? 34 : 5, disabled ? 47 : 27, disabled ? 58 : 43), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, v5_settings_axis_rgb(disabled ? 54 : 45, disabled ? 66 : 86, disabled ? 76 : 112), 0);
    lv_obj_set_style_text_color(obj, text_color, 0);
    if (label_obj) {
        lv_obj_set_style_text_color(label_obj, text_color, 0);
    }
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
        if (col_spec->kind == V5_AXIS_FIELD_EDIT ||
            col_spec->kind == V5_AXIS_FIELD_SELECT ||
            col_spec->kind == V5_AXIS_FIELD_ACTION) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

void v5_settings_axis_refresh_axis_row_refs(unsigned int row)
{
    unsigned int i;
    for (i = 0U; i < g_v5_axis_table_cell_ref_count; ++i) {
        if (g_v5_axis_table_cell_refs[i].row == row &&
            (g_v5_axis_table_cell_refs[i].kind == V5_CELL_KIND_AXIS ||
             g_v5_axis_table_cell_refs[i].kind == V5_CELL_KIND_AXIS_LABEL)) {
            v5_settings_axis_refresh_axis_cell_ref(&g_v5_axis_table_cell_refs[i]);
        }
    }
}
