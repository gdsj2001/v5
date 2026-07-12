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

void v5_settings_axis_table_begin_page(void)
{
    v5_settings_axis_close_keyboard_overlay();
    g_v5_axis_table_cell_ref_count = 0U;
    memset(g_v5_axis_table_cell_refs, 0, sizeof(g_v5_axis_table_cell_refs));
}

static void g53_edit_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    v5_settings_axis_open_keyboard_for_ref(ref);
}

void v5_settings_axis_table_create_g53_cell(lv_obj_t *root, unsigned int row, unsigned int col, int x, int y, int w, int h)
{
    const char *value;
    char id[96];
    lv_obj_t *cell;
    lv_obj_t *text_label;
    V5AxisCellRef *ref;
    int editable;
    if (!root || row >= V5_G53_ROW_COUNT || col >= 3U) return;
    value = v5_settings_axis_table_g53_value(row, col);
    editable = v5_settings_axis_table_g53_value_is_editable(row, col);
    if (!editable) {
        v5_settings_axis_value_cell(root, x, y, w, h, value, v5_settings_axis_rgb(226, 238, 246), !v5_settings_axis_table_g53_value_is_real(row, col), "g53_readonly");
        return;
    }
    if (!v5_settings_axis_g53_field_id(row, col, id, sizeof(id))) {
        snprintf(id, sizeof(id), "g53_%u_%u", row, col);
    }
    cell = v5_settings_axis_panel(root, x, y, w, h, 5, 27, 43);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, v5_settings_axis_rgb(15, 43, 61), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    text_label = v5_settings_axis_label(cell, value, 0, 4, w, h - 5, 226, 238, 246);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    ref = v5_settings_axis_store_cell_ref(V5_CELL_KIND_G53, row, col, cell, text_label, id);
    lv_obj_add_event_cb(cell, g53_edit_cell_clicked_cb, LV_EVENT_CLICKED, ref);
}

static lv_coord_t axis_table_last_content_x(void)
{
    unsigned int i;
    lv_coord_t last = 0;
    for (i = 0U; i < v5_settings_axis_table_column_count(); ++i) {
        lv_coord_t right = (lv_coord_t)(g_v5_axis_table_columns[i].x + g_v5_axis_table_columns[i].width);
        if (right > last) {
            last = right;
        }
    }
    return last;
}

static void axis_scroll_page(int direction)
{
    lv_coord_t current;
    lv_coord_t page;
    lv_coord_t max_x;
    lv_coord_t target;
    if (!g_v5_axis_table_axis_scroll || direction == 0) {
        return;
    }
    current = lv_obj_get_scroll_x(g_v5_axis_table_axis_scroll);
    page = lv_obj_get_width(g_v5_axis_table_axis_scroll) - 96;
    if (page < 160) {
        page = 160;
    }
    max_x = axis_table_last_content_x() - lv_obj_get_width(g_v5_axis_table_axis_scroll);
    if (max_x < 0) {
        max_x = 0;
    }
    target = current + (direction > 0 ? page : -page);
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    lv_obj_scroll_to_x(g_v5_axis_table_axis_scroll, target, LV_ANIM_ON);
}

static void axis_page_button_cb(lv_event_t *event)
{
    intptr_t direction;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    v5_button_visual_release_now(lv_event_get_target(event));
    direction = (intptr_t)lv_event_get_user_data(event);
    axis_scroll_page(direction > 0 ? 1 : -1);
}

static void make_axis_page_button(lv_obj_t *parent, int x, int y, int w, int h, int direction)
{
    static const lv_point_t left_points[] = {{25, 9}, {13, 20}, {25, 31}};
    static const lv_point_t right_points[] = {{13, 9}, {25, 20}, {13, 31}};
    static const lv_point_t left_points_narrow[] = {{17, 13}, {8, 27}, {17, 41}};
    static const lv_point_t right_points_narrow[] = {{8, 13}, {17, 27}, {8, 41}};
    lv_obj_t *button;
    lv_obj_t *line;
    int narrow = w < 32;
    button = v5_settings_axis_panel(parent, x, y, w, h, 8, 34, 52);
    lv_obj_set_style_bg_color(button, v5_settings_axis_rgb(20, 62, 91), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, v5_settings_axis_rgb(76, 119, 146), 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    v5_button_visual_bind(button);
    lv_obj_add_event_cb(button, axis_page_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)direction);

    line = lv_line_create(button);
    if (narrow) {
        lv_line_set_points(line, direction < 0 ? left_points_narrow : right_points_narrow, 3);
        lv_obj_set_pos(line, 0, 0);
    } else {
        lv_line_set_points(line, direction < 0 ? left_points : right_points, 3);
        lv_obj_set_pos(line, (w - 38) / 2, (h - 40) / 2);
    }
    lv_obj_set_style_line_color(line, v5_settings_axis_rgb(226, 238, 246), 0);
    lv_obj_set_style_line_width(line, narrow ? 3 : 4, 0);
    lv_obj_set_style_line_rounded(line, 1, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(button);
}

static lv_obj_t *axis_scroll(lv_obj_t *parent)
{
    lv_obj_t *scroll = lv_obj_create(parent);
    v5_settings_axis_plain_obj(scroll);
    lv_obj_set_pos(scroll, 95, 274);
    lv_obj_set_size(scroll, 908, 300);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ON);
    g_v5_axis_table_axis_scroll = scroll;
    return scroll;
}

void v5_settings_axis_table_create(lv_obj_t *root)
{
    enum { row_h = 28, row_step = 31 };
    lv_obj_t *scroll;
    if (!root) {
        return;
    }

    /* Values come from owner readback: native/runtime INI, self parameter table, then drive/profile providers. */
    v5_settings_axis_panel(root, 21, 225, 982, 343, 7, 31, 48);
    v5_settings_axis_label(root, "轴参数(总线)", 30, 230, 120, 24, 226, 238, 246);
    v5_settings_axis_label(root, "单位:mm/deg", 198, 230, 160, 24, 155, 177, 198);
    v5_settings_axis_label(root, "轴", 54, 285, 32, 20, 150, 170, 190);

    for (unsigned int r = 0; r < v5_settings_axis_table_row_count(); ++r) {
        const V5SettingsAxisRowSpec *row = &g_v5_axis_table_rows[r];
        lv_obj_t *cell = v5_settings_axis_value_cell(root,
                                    30,
                                    318 + (int)r * row_step,
                                    63,
                                    row_h,
                                    row->label,
                                    v5_settings_axis_axis_label_color_for_row(r),
                                    !row->enabled || v5_settings_axis_axis_row_slave_is_nat(r),
                                    "axis_label");
        lv_obj_t *text_label = lv_obj_get_child(cell, 0);
        (void)v5_settings_axis_store_cell_ref(V5_CELL_KIND_AXIS_LABEL, r, 0, cell, text_label, "axis_label");
        v5_settings_axis_apply_axis_row_label_state(cell, text_label, r);
    }

    scroll = axis_scroll(root);
    make_axis_page_button(root, 24, 244, 44, 46, -1);
    make_axis_page_button(root, 998, 236, 24, 54, 1);
    for (unsigned int c = 0; c < v5_settings_axis_table_column_count(); ++c) {
        const V5SettingsAxisColumnSpec *col = &g_v5_axis_table_columns[c];
        const char *unit = strchr(col->label, '\n');
        if (unit) {
            char title[64];
            size_t len = (size_t)(unit - col->label);
            if (len >= sizeof(title)) {
                len = sizeof(title) - 1U;
            }
            memcpy(title, col->label, len);
            title[len] = '\0';
            v5_settings_axis_label(scroll, title, col->x, 8, col->width, 20, 150, 170, 190);
            v5_settings_axis_label(scroll, unit + 1, col->x, 27, col->width, 20, 150, 170, 190);
        } else {
            v5_settings_axis_label(scroll, col->label, col->x, 11, col->width, 20, 150, 170, 190);
        }
    }

    for (unsigned int r = 0; r < v5_settings_axis_table_row_count(); ++r) {
        const V5SettingsAxisRowSpec *row = &g_v5_axis_table_rows[r];
        int y = 44 + (int)r * row_step;
        for (unsigned int c = 0; c < v5_settings_axis_table_column_count(); ++c) {
            const V5SettingsAxisColumnSpec *col = &g_v5_axis_table_columns[c];
            char id[96];
            int structural_disabled = !row->enabled || col->kind == V5_AXIS_FIELD_READONLY;
            int visual_disabled = structural_disabled || v5_settings_axis_axis_cell_disabled_by_nat(r, c);
            v5_settings_axis_table_field_id(row, col, id, sizeof(id));
            if (!structural_disabled && col->kind == V5_AXIS_FIELD_SELECT) {
                v5_settings_axis_dropdown_cell(scroll, r, c, col->x, y, col->width, row_h, v5_settings_axis_initial_value(row, col), col, v5_settings_axis_field_color_for_cell(r, c), id);
            } else if (!structural_disabled && col->kind == V5_AXIS_FIELD_ACTION) {
                v5_settings_axis_action_cell(scroll, r, c, col->x, y, col->width, row_h, v5_settings_axis_current_driver_mode_is_pulse() ? "设零" : "设0", v5_settings_axis_field_color_for_cell(r, c), id);
            } else if (!structural_disabled && col->kind != V5_AXIS_FIELD_READONLY) {
                v5_settings_axis_edit_cell(scroll, r, c, col->x, y, col->width, row_h, v5_settings_axis_initial_value(row, col), v5_settings_axis_field_color_for_cell(r, c), id);
            } else {
                lv_obj_t *cell = v5_settings_axis_value_cell(scroll, col->x, y, col->width, row_h, v5_settings_axis_initial_value(row, col), v5_settings_axis_field_color_for_cell(r, c), visual_disabled, id);
                lv_obj_t *text_label = lv_obj_get_child(cell, 0);
                (void)v5_settings_axis_store_cell_ref(V5_CELL_KIND_AXIS, r, c, cell, text_label, id);
                v5_settings_axis_apply_axis_cell_state(cell, text_label, r, c);
            }
        }
    }
    lv_obj_scroll_to_x(scroll, 0, LV_ANIM_OFF);
    v5_settings_axis_create_keyboard_overlay();
}

const char *v5_settings_axis_table_g53_value(unsigned int row, unsigned int col)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U) {
        return "--";
    }
    return g_v5_axis_table_g53_values[row][col][0] ? g_v5_axis_table_g53_values[row][col] : "--";
}

int v5_settings_axis_table_g53_value_is_real(unsigned int row, unsigned int col)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U) {
        return 0;
    }
    return g_v5_axis_table_g53_value_real[row][col] ? 1 : 0;
}

const char *v5_settings_axis_table_motion_model_value(void)
{
    return g_v5_axis_table_motion_model_value[0] ? g_v5_axis_table_motion_model_value : "--";
}

int v5_settings_axis_table_motion_model_value_is_real(void)
{
    return g_v5_axis_table_motion_model_real ? 1 : 0;
}

const char *v5_settings_axis_table_bus_pulse_value(void)
{
    return g_v5_axis_table_bus_pulse_value[0] ? g_v5_axis_table_bus_pulse_value : "--";
}

int v5_settings_axis_table_bus_pulse_value_is_real(void)
{
    return g_v5_axis_table_bus_pulse_real ? 1 : 0;
}

unsigned int v5_settings_axis_table_row_count(void)
{
    return (unsigned int)(sizeof(g_v5_axis_table_rows) / sizeof(g_v5_axis_table_rows[0]));
}

unsigned int v5_settings_axis_table_column_count(void)
{
    return (unsigned int)(sizeof(g_v5_axis_table_columns) / sizeof(g_v5_axis_table_columns[0]));
}

const V5SettingsAxisRowSpec *v5_settings_axis_table_rows(void)
{
    return g_v5_axis_table_rows;
}

const V5SettingsAxisColumnSpec *v5_settings_axis_table_columns(void)
{
    return g_v5_axis_table_columns;
}
