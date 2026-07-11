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

static void dropdown_changed_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    char selected[64];
    char committed[V5_AXIS_VALUE_CAP];
    const char *commit_value = selected;
    int is_slave;
    int ok;
    if (!ref || !ref->obj || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    if (v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) {
        v5_settings_axis_refresh_axis_cell_ref(ref);
        return;
    }
    selected[0] = '\0';
    lv_dropdown_get_selected_str(ref->obj, selected, sizeof(selected));
    is_slave = ref->col < V5_AXIS_TABLE_MAX_COLS && strcmp(g_v5_axis_table_columns[ref->col].field_key, "slave") == 0;
    if (is_slave) {
        v5_settings_axis_slave_option_extract_id(selected, committed, sizeof(committed));
        if (committed[0]) {
            commit_value = committed;
        }
    }
    ok = v5_settings_axis_table_commit_value(ref->row, ref->col, commit_value);
    if (ok && is_slave) {
        v5_settings_axis_refresh_axis_row_refs(ref->row);
    } else {
        v5_settings_axis_refresh_axis_cell_ref(ref);
    }
}

static void prepare_dropdown_options_for_ref(V5AxisCellRef *ref)
{
    const V5SettingsAxisColumnSpec *col;
    char options[V5_DROPDOWN_OPTIONS_CAP];
    const char *value;
    if (!ref || !ref->obj) {
        return;
    }
    if (ref->col >= v5_settings_axis_table_column_count()) return;
    if (v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) return;
    col = &g_v5_axis_table_columns[ref->col];
    if (strcmp(col->field_key, "slave") != 0) return;
    v5_settings_axis_reload_slave_options_from_resident_self_table();
    value = v5_settings_axis_table_value(ref->row, ref->col);
    if (v5_settings_axis_dropdown_options_for_cell(col, ref->row, value, options, sizeof(options))) {
        lv_dropdown_set_options(ref->obj, options);
        lv_dropdown_set_selected(ref->obj, (uint16_t)v5_settings_axis_dropdown_selected_for_value(options, value));
    }
}

static void dropdown_pressed_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_PRESSED) {
        return;
    }
    prepare_dropdown_options_for_ref(ref);
}

static void dropdown_cell_hit_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ref || !ref->obj) {
        return;
    }
    if (v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) {
        return;
    }
    prepare_dropdown_options_for_ref(ref);
    lv_dropdown_open(ref->obj);
}

void v5_settings_axis_dropdown_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, const V5SettingsAxisColumnSpec *col, lv_color_t text_color, const char *debug_id)
{
    char options[V5_DROPDOWN_OPTIONS_CAP];
    lv_obj_t *dd;
    lv_obj_t *hit;
    V5AxisCellRef *ref;
    (void)debug_id;
    if (!v5_settings_axis_dropdown_options_for_cell(col, row, text, options, sizeof(options))) {
        v5_settings_axis_value_cell(parent, x, y, w, h, text, text_color, 0, debug_id);
        return;
    }
    dd = lv_dropdown_create(parent);
    v5_settings_axis_plain_obj(dd);
    lv_obj_set_pos(dd, x, y);
    lv_obj_set_size(dd, w, h);
    lv_obj_set_style_bg_color(dd, v5_settings_axis_rgb(5, 27, 43), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, v5_settings_axis_rgb(33, 72, 98), 0);
    lv_obj_set_style_text_color(dd, text_color, 0);
    lv_obj_set_style_pad_all(dd, 0, 0);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, (uint16_t)v5_settings_axis_dropdown_selected_for_value(options, text));
    ref = v5_settings_axis_store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, dd, 0, debug_id);
    lv_obj_add_event_cb(dd, dropdown_pressed_cb, LV_EVENT_PRESSED, ref);
    lv_obj_add_event_cb(dd, dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, ref);
    v5_settings_axis_apply_axis_cell_state(dd, 0, row, col_index);

    hit = v5_settings_axis_panel(parent, x, y, w, h, 0, 0, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hit, dropdown_cell_hit_cb, LV_EVENT_CLICKED, ref);
    lv_obj_move_foreground(hit);
}
