#include "v5_settings_axis_table.h"
#include "v5_boot_closure.h"
#include "v5_button_visuals.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_parameter_store.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_popup_layout.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_axis_table_internal.h"

static const char *g_keyboard_keys[] = {
    "/", "x", "-", "+",
    "7", "8", "9", "BKSP",
    "4", "5", "6", "C",
    "1", "2", "3", "OK",
    "0", ".", "+/-", "ESC",
};

enum {
    V5_KEYBOARD_HIT_X = 352,
    V5_KEYBOARD_HIT_Y = 76,
    V5_KEYBOARD_HIT_W = 320,
    V5_KEYBOARD_HIT_H = 419,
    V5_KEYBOARD_TITLE_X = 10,
    V5_KEYBOARD_TITLE_Y = 6,
    V5_KEYBOARD_PANEL_X = 0,
    V5_KEYBOARD_PANEL_Y = 29,
    V5_KEYBOARD_VALUE_X = 6,
    V5_KEYBOARD_VALUE_Y = 35,
    V5_KEYBOARD_VALUE_W = 308,
    V5_KEYBOARD_VALUE_H = 54,
    V5_KEYBOARD_MATRIX_X = 6,
    V5_KEYBOARD_MATRIX_Y = 95,
    V5_KEYBOARD_KEY_W = 75,
    V5_KEYBOARD_KEY_H = 61,
    V5_KEYBOARD_GAP = 2
};

static void keyboard_absorb_release(void)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) {
        lv_indev_wait_release(indev);
    }
}

static void close_keyboard_overlay_with_refresh(V5AxisCellRef *refresh_ref)
{
    lv_obj_t *refresh_obj = refresh_ref ? refresh_ref->obj : 0;

    if (g_v5_axis_table_keyboard_overlay &&
        !lv_obj_has_flag(g_v5_axis_table_keyboard_overlay, LV_OBJ_FLAG_HIDDEN)) {
        if (!v5_ui_first_frame_guard_dismiss_overlay(
                &g_v5_axis_table_keyboard_frame_guard,
                g_v5_axis_table_keyboard_overlay)) {
            return;
        }
    }
    g_v5_axis_table_keyboard_ref = 0;
    g_v5_axis_table_keyboard_value[0] = '\0';
    if (g_v5_axis_table_keyboard_value_label) {
        lv_label_set_text(g_v5_axis_table_keyboard_value_label, " ");
    }
    if (refresh_obj) {
        lv_obj_invalidate(refresh_obj);
    }
}

void v5_settings_axis_close_keyboard_overlay(void)
{
    close_keyboard_overlay_with_refresh(0);
}

static void keyboard_overlay_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        keyboard_absorb_release();
        v5_settings_axis_close_keyboard_overlay();
    }
}

static void refresh_keyboard_value_label(void)
{
    if (g_v5_axis_table_keyboard_value_label) {
        lv_label_set_text(g_v5_axis_table_keyboard_value_label, g_v5_axis_table_keyboard_value[0] ? g_v5_axis_table_keyboard_value : " ");
    }
}

static void keyboard_append_text(const char *text)
{
    size_t used;
    size_t add;
    if (!text || !text[0]) return;
    used = strlen(g_v5_axis_table_keyboard_value);
    add = strlen(text);
    if (used + add >= sizeof(g_v5_axis_table_keyboard_value)) {
        return;
    }
    memcpy(g_v5_axis_table_keyboard_value + used, text, add + 1U);
    refresh_keyboard_value_label();
}

static void keyboard_backspace(void)
{
    size_t len = strlen(g_v5_axis_table_keyboard_value);
    if (len > 0U) {
        g_v5_axis_table_keyboard_value[len - 1U] = '\0';
        refresh_keyboard_value_label();
    }
}

static void keyboard_toggle_sign(void)
{
    size_t len = strlen(g_v5_axis_table_keyboard_value);
    if (g_v5_axis_table_keyboard_value[0] == '-') {
        memmove(g_v5_axis_table_keyboard_value, g_v5_axis_table_keyboard_value + 1, len);
    } else if (len + 1U < sizeof(g_v5_axis_table_keyboard_value)) {
        memmove(g_v5_axis_table_keyboard_value + 1, g_v5_axis_table_keyboard_value, len + 1U);
        g_v5_axis_table_keyboard_value[0] = '-';
    }
    refresh_keyboard_value_label();
}

static int keyboard_ref_uses_integer_axis_value(void)
{
    if (!g_v5_axis_table_keyboard_ref || g_v5_axis_table_keyboard_ref->kind != V5_CELL_KIND_AXIS ||
        g_v5_axis_table_keyboard_ref->col >= v5_settings_axis_table_column_count()) {
        return 0;
    }
    return v5_settings_axis_axis_field_requires_integer_value(g_v5_axis_table_columns[g_v5_axis_table_keyboard_ref->col].field_key);
}

static void keyboard_commit_value(void)
{
    int ok;
    const char *readback;
    if (!g_v5_axis_table_keyboard_ref || !g_v5_axis_table_keyboard_value[0]) {
        return;
    }
    if (g_v5_axis_table_keyboard_ref->kind == V5_CELL_KIND_AXIS &&
        v5_settings_axis_table_cell_is_disabled(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col)) {
        return;
    }
    if (g_v5_axis_table_keyboard_ref->kind == V5_CELL_KIND_G53) {
        ok = v5_settings_axis_table_commit_g53_value(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col, g_v5_axis_table_keyboard_value);
        readback = v5_settings_axis_table_g53_value(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col);
    } else if ((int)g_v5_axis_table_keyboard_ref->col == v5_settings_axis_axis_zero_col_index()) {
        ok = v5_settings_axis_table_start_axis_zero(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_value);
        readback = ok ? g_v5_axis_table_keyboard_value : v5_settings_axis_table_value(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col);
    } else {
        ok = v5_settings_axis_table_commit_value(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col, g_v5_axis_table_keyboard_value);
        readback = v5_settings_axis_table_value(g_v5_axis_table_keyboard_ref->row, g_v5_axis_table_keyboard_ref->col);
    }
    if (g_v5_axis_table_keyboard_ref->label) {
        lv_label_set_text(g_v5_axis_table_keyboard_ref->label, readback);
    }
    if (ok) {
        close_keyboard_overlay_with_refresh(g_v5_axis_table_keyboard_ref);
    }
}

static void keyboard_key_event_cb(lv_event_t *event)
{
    const char *key = (const char *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_RELEASED || !key) {
        return;
    }
    keyboard_absorb_release();
    if (strcmp(key, "OK") == 0) {
        keyboard_commit_value();
    } else if (strcmp(key, "ESC") == 0) {
        v5_settings_axis_close_keyboard_overlay();
    } else if (strcmp(key, "BKSP") == 0) {
        keyboard_backspace();
    } else if (strcmp(key, "C") == 0) {
        g_v5_axis_table_keyboard_value[0] = '\0';
        refresh_keyboard_value_label();
    } else if (strcmp(key, "+/-") == 0) {
        keyboard_toggle_sign();
    } else if (keyboard_ref_uses_integer_axis_value() &&
               (strcmp(key, ".") == 0 || strcmp(key, "/") == 0 || strcmp(key, "x") == 0)) {
        return;
    } else {
        keyboard_append_text(key);
    }
}

static void make_keyboard_key(lv_obj_t *parent, const char *text, int x, int y, int w, int h)
{
    unsigned char fill_r = 5U, fill_g = 27U, fill_b = 43U;
    unsigned char border_r = 33U, border_g = 72U, border_b = 98U;
    unsigned char text_r = 226U, text_g = 238U, text_b = 246U;
    lv_obj_t *key;
    lv_obj_t *text_label;

    if (strcmp(text, "C") == 0) {
        fill_r = 185U; fill_g = 75U; fill_b = 69U;
        border_r = 212U; border_g = 106U; border_b = 99U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    } else if (strcmp(text, "OK") == 0) {
        fill_r = 125U; fill_g = 169U; fill_b = 93U;
        border_r = 158U; border_g = 196U; border_b = 125U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    } else if (strcmp(text, "/") == 0 || strcmp(text, "x") == 0 ||
               strcmp(text, "-") == 0 || strcmp(text, "+") == 0 ||
               strcmp(text, "BKSP") == 0) {
        fill_r = 75U; fill_g = 86U; fill_b = 97U;
        border_r = 107U; border_g = 120U; border_b = 134U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    }

    key = v5_settings_axis_panel(parent, x, y, w, h, fill_r, fill_g, fill_b);
    lv_obj_set_style_border_width(key, 1, 0);
    lv_obj_set_style_border_color(key, v5_settings_axis_rgb(border_r, border_g, border_b), 0);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    text_label = v5_settings_axis_label(key, text, 0, (h - 22) / 2, w, 24, text_r, text_g, text_b);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(key, keyboard_key_event_cb, LV_EVENT_RELEASED, (void *)text);
}

void v5_settings_axis_create_keyboard_overlay(void)
{
    lv_obj_t *keyboard_hit_area;
    lv_obj_t *keyboard_panel;
    lv_obj_t *value_box;
    lv_obj_t *title;
    unsigned int i;

    if (g_v5_axis_table_keyboard_overlay) {
        return;
    }
    v5_ui_first_frame_guard_clear(&g_v5_axis_table_keyboard_frame_guard);
    g_v5_axis_table_keyboard_overlay = v5_popup_layout_create_overlay(lv_scr_act());
    lv_obj_set_style_bg_opa(g_v5_axis_table_keyboard_overlay, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(
        g_v5_axis_table_keyboard_overlay,
        keyboard_overlay_clicked_cb,
        LV_EVENT_RELEASED,
        0);

    keyboard_hit_area = v5_settings_axis_panel(
        g_v5_axis_table_keyboard_overlay,
        V5_KEYBOARD_HIT_X, V5_KEYBOARD_HIT_Y,
        V5_KEYBOARD_HIT_W, V5_KEYBOARD_HIT_H,
        3, 12, 20);
    lv_obj_set_style_bg_opa(keyboard_hit_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboard_hit_area, 0, 0);
    lv_obj_add_flag(keyboard_hit_area, LV_OBJ_FLAG_CLICKABLE);

    title = v5_settings_axis_label(
        keyboard_hit_area, "设置参数",
        V5_KEYBOARD_TITLE_X, V5_KEYBOARD_TITLE_Y, 300, 24, 226, 238, 246);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    keyboard_panel = v5_settings_axis_panel(
        keyboard_hit_area,
        V5_KEYBOARD_PANEL_X, V5_KEYBOARD_PANEL_Y, 320, 390, 12, 39, 58);
    lv_obj_set_style_border_width(keyboard_panel, 1, 0);
    lv_obj_set_style_border_color(keyboard_panel, v5_settings_axis_rgb(33, 72, 98), 0);
    lv_obj_add_flag(keyboard_panel, LV_OBJ_FLAG_CLICKABLE);

    value_box = v5_settings_axis_panel(
        keyboard_hit_area,
        V5_KEYBOARD_VALUE_X, V5_KEYBOARD_VALUE_Y,
        V5_KEYBOARD_VALUE_W, V5_KEYBOARD_VALUE_H,
        5, 27, 43);
    lv_obj_set_style_border_width(value_box, 1, 0);
    lv_obj_set_style_border_color(value_box, v5_settings_axis_rgb(54, 86, 113), 0);
    lv_obj_add_flag(value_box, LV_OBJ_FLAG_CLICKABLE);
    g_v5_axis_table_keyboard_value_label = v5_settings_axis_label(
        value_box, " ", 10, 12, V5_KEYBOARD_VALUE_W - 20, 30, 226, 238, 246);
    lv_obj_set_style_text_align(g_v5_axis_table_keyboard_value_label, LV_TEXT_ALIGN_CENTER, 0);

    for (i = 0U; i < sizeof(g_keyboard_keys) / sizeof(g_keyboard_keys[0]); ++i) {
        int col = (int)(i % 4U);
        int row = (int)(i / 4U);
        make_keyboard_key(keyboard_hit_area,
                          g_keyboard_keys[i],
                          V5_KEYBOARD_MATRIX_X + col * (V5_KEYBOARD_KEY_W + V5_KEYBOARD_GAP),
                          V5_KEYBOARD_MATRIX_Y + row * (V5_KEYBOARD_KEY_H + V5_KEYBOARD_GAP),
                          V5_KEYBOARD_KEY_W,
                          V5_KEYBOARD_KEY_H);
    }
    lv_obj_add_flag(g_v5_axis_table_keyboard_overlay, LV_OBJ_FLAG_HIDDEN);
}

void v5_settings_axis_open_keyboard_for_ref(V5AxisCellRef *ref)
{
    if (!ref || !g_v5_axis_table_keyboard_overlay) {
        return;
    }
    if (ref->kind == V5_CELL_KIND_AXIS &&
        v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) {
        return;
    }
    v5_settings_axis_close_keyboard_overlay();
    if (!v5_ui_first_frame_guard_begin_overlay(&g_v5_axis_table_keyboard_frame_guard)) {
        return;
    }
    g_v5_axis_table_keyboard_ref = ref;
    snprintf(
        g_v5_axis_table_keyboard_value,
        sizeof(g_v5_axis_table_keyboard_value),
        "%s",
        ref->kind == V5_CELL_KIND_G53 ?
            v5_settings_axis_table_g53_value(ref->row, ref->col) :
            v5_settings_axis_table_value(ref->row, ref->col));
    refresh_keyboard_value_label();
    if (!v5_ui_first_frame_guard_present_overlay(
            &g_v5_axis_table_keyboard_frame_guard,
            g_v5_axis_table_keyboard_overlay)) {
        (void)v5_ui_first_frame_guard_dismiss_overlay(
            &g_v5_axis_table_keyboard_frame_guard,
            g_v5_axis_table_keyboard_overlay);
        g_v5_axis_table_keyboard_ref = 0;
    }
}

static void edit_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (ref && v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) return;
    v5_settings_axis_open_keyboard_for_ref(ref);
}

static void axis_zero_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (!ref || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (v5_settings_axis_table_cell_is_disabled(ref->row, ref->col)) return;
    if (v5_settings_axis_current_driver_mode_is_pulse()) {
        v5_settings_axis_open_keyboard_for_ref(ref);
        return;
    }
    (void)v5_settings_axis_table_start_axis_zero(ref->row, 0);
}

void v5_settings_axis_action_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id)
{
    lv_obj_t *cell = v5_settings_axis_panel(parent, x, y, w, h, 5, 27, 43);
    lv_obj_t *text_label = v5_settings_axis_label(cell, text && text[0] ? text : "设0", 0, 5, w, h - 6, 0, 230, 200);
    V5AxisCellRef *ref;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, v5_settings_axis_rgb(45, 86, 112), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    ref = v5_settings_axis_store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, cell, text_label, debug_id);
    lv_obj_add_event_cb(cell, axis_zero_cell_clicked_cb, LV_EVENT_CLICKED, ref);
    v5_settings_axis_apply_axis_cell_state(cell, text_label, row, col_index);
}

void v5_settings_axis_edit_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id)
{
    lv_obj_t *cell = v5_settings_axis_panel(parent, x, y, w, h, 5, 27, 43);
    lv_obj_t *text_label = v5_settings_axis_label(cell, text, 0, 5, w, h - 6, 226, 238, 246);
    V5AxisCellRef *ref;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, v5_settings_axis_rgb(45, 86, 112), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    ref = v5_settings_axis_store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, cell, text_label, debug_id);
    lv_obj_add_event_cb(cell, edit_cell_clicked_cb, LV_EVENT_CLICKED, ref);
    v5_settings_axis_apply_axis_cell_state(cell, text_label, row, col_index);
}
