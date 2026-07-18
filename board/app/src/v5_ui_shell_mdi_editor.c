#include "v5_app.h"

#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#include "v5_ui_shell_internal.h"

static lv_obj_t *g_v5_mdi_editor;
static lv_obj_t *g_v5_mdi_line_numbers[V5_MDI_VISIBLE_ROWS];
static unsigned int g_v5_mdi_first_visible_row = 1U;

static lv_coord_t shell_mdi_editor_line_height(void)
{
    const lv_font_t *font;
    lv_coord_t line_height;
    if (!g_v5_mdi_editor) {
        return 1;
    }
    font = lv_obj_get_style_text_font(g_v5_mdi_editor, LV_PART_MAIN);
    line_height = lv_font_get_line_height(font) +
        lv_obj_get_style_text_line_space(g_v5_mdi_editor, LV_PART_MAIN);
    return line_height > 0 ? line_height : 1;
}

static unsigned int shell_mdi_editor_cursor_visual_row(void)
{
    lv_obj_t *label;
    lv_point_t point;
    lv_coord_t line_height;
    uint32_t cursor;
    if (!g_v5_mdi_editor) {
        return 1U;
    }
    lv_obj_update_layout(g_v5_mdi_editor);
    label = lv_textarea_get_label(g_v5_mdi_editor);
    cursor = lv_textarea_get_cursor_pos(g_v5_mdi_editor);
    lv_label_get_letter_pos(label, cursor, &point);
    line_height = shell_mdi_editor_line_height();
    return point.y > 0 ? (unsigned int)(point.y / line_height) + 1U : 1U;
}

static void shell_mdi_editor_update_line_numbers(unsigned int first_row)
{
    unsigned int i;
    if (first_row == 0U) {
        first_row = 1U;
    }
    if (g_v5_mdi_first_visible_row == first_row &&
        g_v5_mdi_line_numbers[0]) {
        return;
    }
    g_v5_mdi_first_visible_row = first_row;
    for (i = 0U; i < V5_MDI_VISIBLE_ROWS; ++i) {
        char text[12];
        if (!g_v5_mdi_line_numbers[i]) {
            continue;
        }
        snprintf(text, sizeof(text), "%03u", first_row + i);
        lv_label_set_text(g_v5_mdi_line_numbers[i], text);
    }
}

static void shell_mdi_editor_follow_cursor(void)
{
    unsigned int cursor_row;
    unsigned int first_row;
    lv_coord_t line_height;
    if (!g_v5_mdi_editor) {
        return;
    }
    cursor_row = shell_mdi_editor_cursor_visual_row();
    first_row = ((cursor_row - 1U) / V5_MDI_VISIBLE_ROWS) *
        V5_MDI_VISIBLE_ROWS + 1U;
    line_height = shell_mdi_editor_line_height();
    lv_obj_scroll_to_y(
        g_v5_mdi_editor,
        (lv_coord_t)((first_row - 1U) * (unsigned int)line_height),
        LV_ANIM_OFF);
    shell_mdi_editor_update_line_numbers(first_row);
}

static int shell_mdi_editor_copy_to_buffer(void)
{
    const char *text;
    size_t size;
    if (!g_v5_mdi_editor) {
        return 0;
    }
    text = lv_textarea_get_text(g_v5_mdi_editor);
    size = strlen(text);
    if (size >= sizeof(g_v5_shell_mdi_line)) {
        return 0;
    }
    memcpy(g_v5_shell_mdi_line, text, size + 1U);
    shell_mark_page_cache_dirty(V5_SHELL_PAGE_MDI);
    return 1;
}

void shell_mdi_editor_bind(
    lv_obj_t *editor,
    lv_obj_t *const *line_number_labels,
    unsigned int line_number_count)
{
    unsigned int i;
    g_v5_mdi_editor = editor;
    for (i = 0U; i < V5_MDI_VISIBLE_ROWS; ++i) {
        g_v5_mdi_line_numbers[i] =
            line_number_labels && i < line_number_count ?
            line_number_labels[i] : 0;
    }
    g_v5_mdi_first_visible_row = 0U;
    shell_mdi_editor_sync_from_buffer();
    shell_mdi_editor_set_active(0);
}

void shell_mdi_editor_sync_from_buffer(void)
{
    const char *current;
    uint32_t cursor = 0U;
    int unchanged;
    if (!g_v5_mdi_editor) {
        return;
    }
    current = lv_textarea_get_text(g_v5_mdi_editor);
    unchanged = strcmp(current, g_v5_shell_mdi_line) == 0;
    if (unchanged) {
        cursor = lv_textarea_get_cursor_pos(g_v5_mdi_editor);
    } else {
        lv_textarea_set_text(g_v5_mdi_editor, g_v5_shell_mdi_line);
    }
    lv_textarea_set_cursor_pos(g_v5_mdi_editor, (int32_t)cursor);
    shell_mdi_editor_follow_cursor();
}

int shell_mdi_editor_handle_key(const char *key)
{
    unsigned int i;
    if (!g_v5_mdi_editor || !key) {
        return 0;
    }
    if (strcmp(key, "BKSP") == 0) {
        lv_textarea_del_char(g_v5_mdi_editor);
    } else if (strcmp(key, "SP") == 0) {
        lv_textarea_add_text(g_v5_mdi_editor, " ");
    } else if (strcmp(key, "EOB") == 0) {
        lv_textarea_add_text(g_v5_mdi_editor, ";");
    } else if (strcmp(key, "CAN") == 0) {
        lv_textarea_set_text(g_v5_mdi_editor, "");
        lv_textarea_set_cursor_pos(g_v5_mdi_editor, 0);
    } else if (strcmp(key, "<") == 0) {
        lv_textarea_cursor_left(g_v5_mdi_editor);
    } else if (strcmp(key, ">") == 0) {
        lv_textarea_cursor_right(g_v5_mdi_editor);
    } else if (strcmp(key, "^") == 0) {
        lv_textarea_cursor_up(g_v5_mdi_editor);
    } else if (strcmp(key, "v") == 0) {
        lv_textarea_cursor_down(g_v5_mdi_editor);
    } else if (strcmp(key, "UP\nPAGE") == 0) {
        for (i = 0U; i < V5_MDI_VISIBLE_ROWS; ++i) {
            lv_textarea_cursor_up(g_v5_mdi_editor);
        }
    } else if (strcmp(key, "PAGE\nDN") == 0) {
        for (i = 0U; i < V5_MDI_VISIBLE_ROWS; ++i) {
            lv_textarea_cursor_down(g_v5_mdi_editor);
        }
    } else {
        lv_textarea_add_text(g_v5_mdi_editor, key);
    }
    if (!shell_mdi_editor_copy_to_buffer()) {
        return 0;
    }
    shell_mdi_editor_follow_cursor();
    return 1;
}

void shell_mdi_editor_set_active(int active)
{
    uint32_t cursor;
    if (!g_v5_mdi_editor) {
        return;
    }
    cursor = lv_textarea_get_cursor_pos(g_v5_mdi_editor);
    if (active) {
        lv_obj_add_state(g_v5_mdi_editor, LV_STATE_FOCUSED);
        lv_obj_set_style_anim_time(
            g_v5_mdi_editor,
            V5_MDI_CURSOR_BLINK_MS,
            LV_PART_CURSOR | LV_STATE_FOCUSED);
    } else {
        lv_obj_clear_state(g_v5_mdi_editor, LV_STATE_FOCUSED);
        lv_obj_set_style_anim_time(
            g_v5_mdi_editor,
            0U,
            LV_PART_CURSOR);
    }
    lv_textarea_set_cursor_pos(g_v5_mdi_editor, (int32_t)cursor);
}

#ifdef V5_UI_SHELL_TEST_HOOKS
int v5_ui_shell_test_mdi_press_key(const char *key)
{
    return shell_mdi_editor_handle_key(key);
}

unsigned int v5_ui_shell_test_mdi_cursor_pos(void)
{
    return g_v5_mdi_editor ?
        (unsigned int)lv_textarea_get_cursor_pos(g_v5_mdi_editor) : 0U;
}

unsigned int v5_ui_shell_test_mdi_cursor_visual_row(void)
{
    return shell_mdi_editor_cursor_visual_row();
}

unsigned int v5_ui_shell_test_mdi_first_visible_row(void)
{
    return g_v5_mdi_first_visible_row;
}

unsigned int v5_ui_shell_test_mdi_cursor_blink_ms(void)
{
    return g_v5_mdi_editor ?
        (unsigned int)lv_obj_get_style_anim_time(
            g_v5_mdi_editor,
            LV_PART_CURSOR) : 0U;
}
#endif
