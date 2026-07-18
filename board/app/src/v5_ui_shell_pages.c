#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_wcs_status.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"
#include "v5_v3_local_pages.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "v5_ui_shell_internal.h"
#include "v5_ui_shell_program_delete.h"

void shell_clear_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *shell_make_label(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}


lv_color_t shell_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

static lv_obj_t *shell_make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    shell_clear_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, shell_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, shell_rgb(33, 72, 98), 0);
    return panel;
}

static lv_obj_t *shell_text_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;
    shell_clear_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, shell_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, shell_rgb(76, 119, 146), 0);
    v5_button_visual_bind(button);
    if (cb) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, 0);
    }
    label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, 0, (h - 24) / 2);
    lv_obj_set_size(label, w, 24);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, (r + g + b > 540) ? shell_rgb(16, 20, 24) : shell_rgb(238, 245, 248), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return button;
}

static lv_obj_t *shell_table_label(lv_obj_t *parent, const char *text, int x, int y, int w, uint8_t r, uint8_t g, uint8_t b)
{
    return shell_make_label(parent, x, y, w, 22, text, shell_rgb(r, g, b), LV_TEXT_ALIGN_LEFT);
}

void shell_format_program_size(off_t size, char *out, size_t out_cap)
{
    if (!out || out_cap == 0U) {
        return;
    }
    if (size < 1024) {
        snprintf(out, out_cap, "%lldB", (long long)size);
    } else if (size < 1024 * 1024) {
        snprintf(out, out_cap, "%.1fK", (double)size / 1024.0);
    } else {
        snprintf(out, out_cap, "%.1fM", (double)size / (1024.0 * 1024.0));
    }
}

void shell_format_program_date(time_t when, char *out, size_t out_cap)
{
    struct tm local_time;
    if (!out || out_cap == 0U) {
        return;
    }
    if (when <= 0) {
        out[0] = '\0';
        return;
    }
    localtime_r(&when, &local_time);
    strftime(out, out_cap, "%m-%d", &local_time);
}

static void shell_refresh_program_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        shell_update_program_row();
    }
}

static void shell_mdi_clear_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        g_v5_shell_mdi_line[0] = '\0';
        shell_clear_mdi_edit_metadata();
        shell_update_mdi_line();
    }
}

static void shell_mdi_save_cb(lv_event_t *event)
{
    char tmp_path[512];
    FILE *fp;
    size_t len;
    int rc;
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (!g_v5_shell_mdi_edit_program_path[0] || !shell_program_path_allowed(g_v5_shell_mdi_edit_program_path)) {
        shell_log_mdi_event("program_save_rejected", "no editable program path", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "当前没有可写回的G代码文件");
        }
        return;
    }
    if (!g_v5_shell_mdi_line[0]) {
        shell_log_mdi_event("program_save_rejected", "empty mdi text", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "修改区为空，已拒绝覆盖");
        }
        return;
    }
    rc = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", g_v5_shell_mdi_edit_program_path, (long)getpid());
    if (rc <= 0 || (size_t)rc >= sizeof(tmp_path)) {
        shell_log_mdi_event("program_save_rejected", "tmp path too long", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "保存路径过长");
        }
        return;
    }
    fp = fopen(tmp_path, "wb");
    if (!fp) {
        shell_log_mdi_event("program_save_failed", "open tmp failed", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "保存失败，无法写入");
        }
        return;
    }
    fputs(g_v5_shell_mdi_line, fp);
    len = strlen(g_v5_shell_mdi_line);
    if (len == 0U || g_v5_shell_mdi_line[len - 1U] != '\n') {
        fputc('\n', fp);
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        remove(tmp_path);
        shell_log_mdi_event("program_save_failed", "flush failed", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "保存失败，写入未完成");
        }
        return;
    }
    if (fclose(fp) != 0) {
        remove(tmp_path);
        shell_log_mdi_event("program_save_failed", "close failed", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "保存失败，关闭文件失败");
        }
        return;
    }
    if (rename(tmp_path, g_v5_shell_mdi_edit_program_path) != 0) {
        remove(tmp_path);
        shell_log_mdi_event("program_save_failed", "rename failed", 0);
        if (g_v5_shell_mdi_status_label) {
            lv_label_set_text(g_v5_shell_mdi_status_label, "保存失败，未覆盖原文件");
        }
        return;
    }
    shell_log_mdi_event("program_saved", g_v5_shell_mdi_edit_program_path, 1);
    shell_update_program_row();
    if (g_v5_shell_mdi_status_label) {
        lv_label_set_text(g_v5_shell_mdi_status_label, "G代码文件已保存");
    }
}

static void shell_mdi_key_cb(lv_event_t *event)
{
    const char *key;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    key = (const char *)lv_event_get_user_data(event);
    if (!key) {
        return;
    }
    (void)shell_mdi_editor_handle_key(key);
}

static void shell_keyboard_key_label(lv_obj_t *key, const char *text, int w, int h)
{
    const char *newline = text ? strchr(text, '\n') : 0;
    if (!newline) {
        shell_make_label(key, 0, (h - 22) / 2, w, 24, text ? text : "", shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
    } else {
        char top[24];
        size_t n = (size_t)(newline - text);
        if (n >= sizeof(top)) {
            n = sizeof(top) - 1U;
        }
        memcpy(top, text, n);
        top[n] = '\0';
        shell_make_label(key, 0, (h - 42) / 2, w, 22, top, shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
        shell_make_label(key, 0, (h - 42) / 2 + 22, w, 22, newline + 1, shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
    }
}

static lv_obj_t *shell_keyboard_key(lv_obj_t *parent, int x, int y, int w, int h, const char *text, int pressed, int interactive)
{
    lv_obj_t *key = shell_make_panel(parent, x, y, w, h, pressed ? 247 : 244, pressed ? 221 : 242, pressed ? 187 : 237);
    lv_obj_set_style_border_color(key, shell_rgb(158, 165, 170), 0);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    shell_keyboard_key_label(key, text, w, h);
    if (interactive) {
        lv_obj_add_event_cb(key, shell_mdi_key_cb, LV_EVENT_CLICKED, (void *)text);
    }
    return key;
}

static void shell_create_keyboard_matrix(lv_obj_t *parent, int x, int y, int w, int h, const char *labels[], int rows, int cols, const char *special, int interactive)
{
    const int row_gap = 8;
    const int col_gap = 8;
    const int row_h = (h - row_gap * (rows - 1)) / rows;
    const int col_w = (w - col_gap * (cols - 1)) / cols;
    int r;
    int c;
    for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
            const char *label = labels[r * cols + c];
            shell_keyboard_key(parent, x + c * (col_w + col_gap), y + r * (row_h + row_gap), col_w, row_h, label, special && strcmp(label, special) == 0, interactive);
        }
    }
}

static void shell_create_cnc_keyboard_panel(lv_obj_t *parent, int x, int y, int interactive)
{
    static const char *letters[] = {
        "O", "N", "G", "P",
        "X", "Y", "Z", "Q",
        "I", "J", "K", "R",
        "M", "S", "T", "L",
        "F", "D", "H", "B",
    };
    static const char *numbers[] = {
        "7", "8", "9",
        "4", "5", "6",
        "1", "2", "3",
        "-", "0", ".",
        "/", "EOB", "CAN",
    };
    shell_create_keyboard_matrix(parent, x, y, 252, 314, letters, 5, 4, 0, interactive);
    shell_create_keyboard_matrix(parent, x + 274, y, 196, 314, numbers, 5, 3, "CAN", interactive);
    shell_keyboard_key(parent, x, y + 328, 64, 58, "UP\nPAGE", 1, interactive);
    shell_keyboard_key(parent, x, y + 402, 64, 58, "PAGE\nDN", 1, interactive);
    shell_keyboard_key(parent, x + 72, y + 388, 54, 54, "<", 0, interactive);
    shell_keyboard_key(parent, x + 134, y + 328, 54, 54, "^", 0, interactive);
    shell_keyboard_key(parent, x + 134, y + 436, 54, 42, "v", 0, interactive);
    shell_keyboard_key(parent, x + 196, y + 388, 54, 54, ">", 0, interactive);
    shell_keyboard_key(parent, x + 274, y + 328, 86, 54, "SP", 0, interactive);
    shell_keyboard_key(parent, x + 376, y + 328, 86, 54, "BKSP", 0, interactive);
}

lv_obj_t *shell_create_program_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    shell_make_panel(root, 10, 10, 1004, 580, 9, 22, 34);
    shell_make_label(root, 30, 52, 48, 24, "搜索", shell_rgb(142, 162, 184), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 90, 38, 294, 40, 11, 24, 38);
    shell_make_label(root, 98, 52, 180, 24, "输入程序名", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 22, 102, 160, 24, "本机程序文件", shell_rgb(79, 168, 214), LV_TEXT_ALIGN_LEFT);
    g_v5_shell_program_count_label = shell_make_label(root, 253, 102, 80, 24, "共 0", shell_rgb(156, 178, 202), LV_TEXT_ALIGN_LEFT);

    shell_make_panel(root, 18, 126, 500, 344, 22, 37, 54);
    shell_make_panel(root, 19, 127, 498, 28, 16, 31, 48);
    shell_table_label(root, "名称 ^", 32, 132, 120, 199, 212, 226);
    shell_table_label(root, "大小", 218, 132, 60, 199, 212, 226);
    shell_table_label(root, "创建日期", 290, 132, 88, 199, 212, 226);
    shell_table_label(root, "修改日期", 402, 132, 88, 199, 212, 226);
    shell_make_panel(root, 212, 127, 1, 342, 46, 77, 105);
    shell_make_panel(root, 282, 127, 1, 342, 46, 77, 105);
    shell_make_panel(root, 396, 127, 1, 342, 46, 77, 105);

    g_v5_shell_program_empty_label = shell_make_label(root, 56, 174, 360, 24, "", shell_rgb(156, 178, 202), LV_TEXT_ALIGN_LEFT);
    g_v5_shell_program_source_label = shell_make_label(root, 22, 482, 470, 24, "来源: 本机", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < (int)V5_PROGRAM_ROWS_MAX; ++i) {
        lv_obj_t *row;
        g_v5_shell_program_row_indices[i] = i;
        row = shell_make_panel(root, 20, 155 + i * V5_PROGRAM_ROW_HIT_H, 478, V5_PROGRAM_ROW_HIT_H, 17, 40, 58);
        g_v5_shell_program_row_layers[i] = row;
        shell_bind_program_row_hit(row, &g_v5_shell_program_row_indices[i]);
        shell_bind_program_row_child_hit(shell_make_panel(row, 14, 7, 12, 15, 205, 216, 228), &g_v5_shell_program_row_indices[i]);
        g_v5_shell_program_row_name_labels[i] = shell_make_label(row, 36, 6, 132, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_v5_shell_program_row_name_labels[i], &g_v5_shell_program_row_indices[i]);
        g_v5_shell_program_row_size_labels[i] = shell_make_label(row, 208, 6, 56, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_v5_shell_program_row_size_labels[i], &g_v5_shell_program_row_indices[i]);
        g_v5_shell_program_row_created_labels[i] = shell_make_label(row, 270, 6, 80, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_v5_shell_program_row_created_labels[i], &g_v5_shell_program_row_indices[i]);
        g_v5_shell_program_row_modified_labels[i] = shell_make_label(row, 382, 6, 80, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_v5_shell_program_row_modified_labels[i], &g_v5_shell_program_row_indices[i]);
    }
    shell_make_panel(root, 497, 162, 15, 300, 112, 132, 154);

    shell_create_cnc_keyboard_panel(root, 536, 26, 0);
    shell_text_button(root, "刷新", 392, 37, 118, 40, 20, 62, 91, shell_refresh_program_cb);
    shell_text_button(root, "本机", 18, 517, 110, 54, 43, 133, 83, shell_refresh_program_cb);
    shell_text_button(root, "删除", 136, 517, 110, 54, 199, 70, 46, shell_program_delete_cb);
    g_v5_shell_program_edit_button = shell_text_button(root, "打开修改", 254, 517, 128, 54, 74, 91, 111, shell_program_edit_cb);
    shell_text_button(root, "返回", 390, 517, 128, 54, 20, 62, 91, shell_return_button_cb);

    shell_create_program_delete_popup(lv_obj_get_screen(root));
    shell_update_program_row();
    return root;
}

lv_obj_t *shell_create_mdi_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_t *line_number_labels[V5_MDI_VISIBLE_ROWS];
    const lv_font_t *mdi_font;
    int mdi_line_space;
    int i;
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(77, 85, 93), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 6, 7, 42, 22, "MDI", shell_rgb(245, 242, 232), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 56, 7, 120, 22, "MDI edit", shell_rgb(214, 209, 194), LV_TEXT_ALIGN_LEFT);

    shell_make_panel(root, 0, 28, 512, 548, 16, 24, 32);
    for (i = 0; i < (int)V5_MDI_VISIBLE_ROWS; ++i) {
        char line_no[8];
        snprintf(line_no, sizeof(line_no), "%03d", i + 1);
        line_number_labels[i] =
            shell_make_label(root, 12, 37 + i * 28, 42, 18, line_no,
                shell_rgb(127, 138, 148), LV_TEXT_ALIGN_LEFT);
    }
    g_v5_shell_mdi_line_label = lv_textarea_create(root);
    lv_obj_set_pos(g_v5_shell_mdi_line_label, 62, 38);
    lv_obj_set_size(g_v5_shell_mdi_line_label, 450, 492);
    lv_obj_set_style_bg_opa(g_v5_shell_mdi_line_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_v5_shell_mdi_line_label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_v5_shell_mdi_line_label, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        g_v5_shell_mdi_line_label,
        shell_rgb(247, 250, 252),
        LV_PART_MAIN);
    mdi_font = lv_obj_get_style_text_font(g_v5_shell_mdi_line_label, LV_PART_MAIN);
    mdi_line_space =
        (int)V5_MDI_VISUAL_ROW_H - (int)lv_font_get_line_height(mdi_font);
    lv_obj_set_style_text_line_space(
        g_v5_shell_mdi_line_label,
        mdi_line_space > 0 ? mdi_line_space : 0,
        LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(g_v5_shell_mdi_line_label, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_one_line(g_v5_shell_mdi_line_label, false);
    lv_textarea_set_placeholder_text(g_v5_shell_mdi_line_label, "MDI>");
    lv_textarea_set_cursor_click_pos(g_v5_shell_mdi_line_label, true);
    lv_textarea_set_max_length(g_v5_shell_mdi_line_label, V5_MDI_TEXT_CAP - 1U);
    lv_obj_set_style_bg_opa(g_v5_shell_mdi_line_label, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_width(g_v5_shell_mdi_line_label, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(g_v5_shell_mdi_line_label, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(
        g_v5_shell_mdi_line_label,
        shell_rgb(255, 255, 255),
        LV_PART_CURSOR);
    shell_mdi_editor_bind(
        g_v5_shell_mdi_line_label,
        line_number_labels,
        V5_MDI_VISIBLE_ROWS);
    g_v5_shell_mdi_status_label = shell_make_label(root, 62, 536, 390, 24, "Native MDI", shell_rgb(150, 162, 174), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 62, 558, 390, 24, "LinuxCNC RS274/NGC 原生输入", shell_rgb(150, 162, 174), LV_TEXT_ALIGN_LEFT);

    shell_create_cnc_keyboard_panel(root, 554, 28, 1);
    shell_text_button(root, "清空", 828, 418, 86, 54, 244, 242, 237, shell_mdi_clear_cb);
    shell_text_button(root, "发送", 930, 418, 86, 54, 216, 193, 160, shell_mdi_load_cb);
    shell_text_button(root, "保存", 708, 514, 98, 54, 244, 242, 237, shell_mdi_save_cb);
    shell_text_button(root, "关闭", 920, 514, 98, 54, 244, 242, 237, shell_return_button_cb);
    shell_update_mdi_line();
    return root;
}

lv_obj_t *shell_create_aux_page(lv_obj_t *screen, const char *title)
{
    lv_obj_t *root = lv_obj_create(screen);
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 30, 17, 160, 24, title, shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 28, 78, 560, 420, 7, 31, 48);
    shell_make_panel(root, 612, 78, 260, 220, 8, 36, 55);
    shell_text_button(root, "主页面", 920, 0, 104, 60, 41, 145, 107, shell_return_button_cb);
    return root;
}

lv_obj_t *shell_create_network_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    static const char *axes[] = {"X", "Y", "Z", "A", "C", "S", "备用7", "备用8"};
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 30, 17, 220, 24, "网络 / 总线只读详情", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 270, 17, 520, 24, "P8 只读：不改网口、不扫写总线、不复位驱动", shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 28, 72, 452, 270, 7, 31, 48);
    shell_make_label(root, 52, 96, 360, 24, "PS eth0: SSH/调试/普通网络", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 132, 390, 24, "PS eth0 IP: --", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 168, 390, 24, "调试软件: http://--:8091", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 204, 390, 24, "PL eth1: EtherCAT 专用，不 DHCP，不维护 IP", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 240, 300, 24, "EtherCAT master: --", shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 276, 260, 24, "Domain0: --/--", shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 312, 390, 24, "驱动状态: --（等待 native owner 回读）", shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 506, 72, 440, 360, 7, 31, 48);
    for (i = 0; i < 8; ++i) {
        int y = 98 + i * 38;
        shell_make_panel(root, 532, y, 364, 30, (i < 5) ? 8 : 38, (i < 5) ? 36 : 54, (i < 5) ? 55 : 65);
        if (i < 5) {
            char row[128];
            snprintf(row, sizeof(row), "%s轴  OP--  fault=--  statusword=--", axes[i]);
            shell_make_label(root, 548, y + 5, 320, 20, row, shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
        } else {
            shell_make_label(root, 548, y + 5, 320, 20, axes[i], shell_rgb(150, 170, 190), LV_TEXT_ALIGN_LEFT);
        }
    }
    shell_text_button(root, "主页面", 920, 0, 104, 60, 41, 145, 107, shell_return_button_cb);
    return root;
}
