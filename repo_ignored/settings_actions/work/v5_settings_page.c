#include "v5_settings_page.h"
#include "v5_settings_actions.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static lv_color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void clear_obj_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    clear_obj_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, rgb(r, g, b), 0);
    return label;
}

static void write_json_text(FILE *fp, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', fp);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('_', fp);
        } else if (*p >= 32U && *p < 127U) {
            fputc((int)*p, fp);
        }
        ++p;
    }
    fputc('"', fp);
}

static void log_button_event(V5MainPageActionKind action, const V5MainPageActionReport *report)
{
    int implemented = report ? report->prepared : 0;
    int layout_only = !implemented;
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/ui_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp, "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"settings_button_clicked\",\"action\":", monotonic_seconds());
    write_json_text(fp, v5_main_page_action_label(action));
    fprintf(fp, ",\"ok\":true,\"implemented\":%s,\"layout_only\":%s", implemented ? "true" : "false", layout_only ? "true" : "false");
    if (report) {
        fprintf(fp, ",\"prepared\":%s,\"local_only\":%s,\"command_kind\":%d,\"command_name\":", report->prepared ? "true" : "false", report->local_only ? "true" : "false", (int)report->command.kind);
        write_json_text(fp, report->command.name);
        fprintf(fp, ",\"command_owner\":");
        write_json_text(fp, report->command.owner);
    }
    fprintf(fp, "}\n");
    fclose(fp);
}

static void button_event_cb(lv_event_t *event)
{
    V5SettingsPage *page = (V5SettingsPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;
    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < page->button_count; ++i) {
        if (page->buttons[i] == target) {
            V5MainPageActionReport report;
            if (v5_settings_page_trigger_action(page, page->button_actions[i], &report)) {
                log_button_event(page->button_actions[i], &report);
            }
            return;
        }
    }
}

static void make_button(V5SettingsPage *page, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, V5MainPageActionKind action)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_SETTINGS_PAGE_BUTTON_COUNT) {
        return;
    }
    button = lv_btn_create(page->root);
    clear_obj_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, rgb(r, g, b), 0);
    lv_obj_set_style_bg_color(button, rgb(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, rgb(76, 119, 146), 0);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, page);
    label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, 0, 4);
    lv_obj_set_size(label, w, h - 6);
    lv_obj_set_style_text_color(label, rgb(238, 245, 248), 0);
    lv_obj_set_style_text_color(label, rgb(16, 20, 24), LV_STATE_PRESSED);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    page->buttons[page->button_count] = button;
    page->button_actions[page->button_count] = action;
    page->button_count += 1u;
}

static void make_value_cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int muted)
{
    lv_obj_t *cell = make_panel(parent, x, y, w, h, muted ? 38 : 5, muted ? 54 : 27, muted ? 65 : 43);
    lv_obj_t *label = make_label(cell, text, 0, 5, w, h - 6, muted ? 150 : 226, muted ? 170 : 238, muted ? 190 : 246);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *make_axis_param_scroller(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *scroller = lv_obj_create(parent);
    clear_obj_style(scroller);
    lv_obj_set_pos(scroller, x, y);
    lv_obj_set_size(scroller, w, h);
    lv_obj_set_style_bg_opa(scroller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroller, 0, 0);
    lv_obj_set_style_pad_all(scroller, 0, 0);
    lv_obj_add_flag(scroller, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroller, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_ON);
    return scroller;
}

static void make_axis_param_table(lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "B", "C", "龙门", "刀库"};
    static const char *cols[] = {
        "轴模式", "从站", "bit", "目标精度", "螺距/角度", "电机", "负载", "分子", "分母",
        "回零顺序", "回零方向", "-软限位", "+软限位", "零位", "状态"
    };
    enum { axis_count = 8, col_count = 15, col_w = 84, row_h = 27, row_step = 29 };
    lv_obj_t *scroller;
    int i;
    int c;

    make_panel(root, 21, 225, 982, 343, 7, 31, 48);
    make_label(root, "轴参数(总线/8轴)", 30, 230, 150, 24, 226, 238, 246);
    make_label(root, "单位:mm/deg", 198, 230, 160, 24, 155, 177, 198);
    make_label(root, "轴", 54, 285, 32, 20, 150, 170, 190);

    for (i = 0; i < axis_count; ++i) {
        make_value_cell(root, axes[i], 30, 312 + i * row_step, 63, row_h, 0);
    }

    scroller = make_axis_param_scroller(root, 102, 278, 890, 275);
    for (c = 0; c < col_count; ++c) {
        make_label(scroller, cols[c], c * col_w, 7, col_w - 8, 20, 150, 170, 190);
    }
    for (i = 0; i < axis_count; ++i) {
        int y = 34 + i * row_step;
        for (c = 0; c < col_count; ++c) {
            const char *text = (c == 2) ? "NAT" : ((c == 13) ? "设" : "--");
            int muted = (c == 0 || c == 2 || c == 13) ? 0 : 1;
            make_value_cell(scroller, text, c * col_w, y, col_w - 8, row_h, muted);
        }
    }
}

void v5_settings_page_init(V5SettingsPage *page)
{
    if (page) {
        memset(page, 0, sizeof(*page));
    }
}

int v5_settings_page_create(V5SettingsPage *page, lv_obj_t *parent)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "B", "C", "龙门", "刀库"};
    int i;
    if (!page || !parent) {
        return 0;
    }
    v5_settings_page_init(page);
    page->root = lv_obj_create(parent);
    clear_obj_style(page->root);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 1024, 600);
    lv_obj_set_style_bg_color(page->root, rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);

    make_label(page->root, "参数设置", 30, 17, 100, 24, 226, 238, 246);
    make_label(page->root, "请求:NAT 实际:NAT", 132, 17, 360, 24, 155, 177, 198);
    make_label(page->root, "本机码 未登记", 500, 17, 198, 24, 155, 177, 198);

    make_panel(page->root, 16, 42, 220, 176, 7, 31, 48);
    make_label(page->root, "运动模型", 24, 55, 100, 24, 155, 177, 198);
    make_value_cell(page->root, "AC摇篮", 25, 78, 190, 36, 0);
    make_label(page->root, "脉冲/总线", 24, 132, 100, 24, 226, 238, 246);
    make_value_cell(page->root, "NAT", 162, 127, 70, 31, 0);

    make_panel(page->root, 247, 42, 449, 176, 7, 31, 48);
    make_label(page->root, "机床坐标目标位置表(G53)", 264, 55, 230, 24, 0, 190, 255);
    make_label(page->root, "X", 395, 86, 36, 20, 155, 177, 198);
    make_label(page->root, "Y", 479, 86, 36, 20, 155, 177, 198);
    make_label(page->root, "Z", 563, 86, 36, 20, 155, 177, 198);
    make_label(page->root, "A中心", 276, 109, 68, 22, 255, 100, 106);
    make_label(page->root, "C中心", 276, 137, 68, 22, 0, 225, 220);
    make_label(page->root, "对刀仪", 276, 165, 68, 22, 155, 177, 198);
    make_label(page->root, "5方向监测仪", 276, 193, 100, 22, 155, 177, 198);
    for (i = 0; i < 12; ++i) {
        make_value_cell(page->root, "NAT", 366 + (i % 3) * 84, 108 + (i / 3) * 28, 78, 26, 0);
    }

    make_panel(page->root, 696, 42, 312, 176, 7, 31, 48);
    make_label(page->root, "机械坐标", 708, 50, 92, 24, 88, 204, 255);
    make_label(page->root, "MCS", 854, 50, 44, 24, 155, 177, 198);
    for (i = 0; i < 8; ++i) {
        make_label(page->root, axes[i], 708, 78 + i * 17, 58, 18, 218, 232, 242);
        make_label(page->root, "--.---", 788, 78 + i * 17, 100, 18, 88, 204, 255);
    }

    make_axis_param_table(page->root);

    make_button(page, "下载授权", 410, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD);
    make_button(page, "服务器下载", 494, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD);
    make_button(page, "扫描从站", 578, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SCAN);
    make_button(page, "复位驱动", 662, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET);
    make_button(page, "读取驱动", 746, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_READ);
    make_button(page, "清除故障", 830, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET);
    make_button(page, "设置驱动", 914, 236, 82, 34, 39, 113, 164, V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE);
    make_button(page, "登记本机码", 806, 8, 94, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER);
    make_button(page, "返回", 902, 8, 92, 34, 74, 91, 111, V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN);
    return page->button_count == V5_SETTINGS_PAGE_BUTTON_COUNT;
}

void v5_settings_page_set_navigation_callback(V5SettingsPage *page, V5UiNavigationCallback cb, void *user_data)
{
    if (!page) {
        return;
    }
    page->navigation_cb = cb;
    page->navigation_user_data = user_data;
}

int v5_settings_page_trigger_action(V5SettingsPage *page, V5MainPageActionKind action, V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    V5SettingsActionResult action_result;
    if (!page) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->action = action;
    if (page->navigation_cb && action == V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN) {
        out->prepared = 1;
        out->local_only = 1;
        out->request.kind = V5_COMMAND_UI_LOCAL;
        out->command.kind = V5_COMMAND_UI_LOCAL;
        out->command.name = "settings_return";
        out->command.owner = "ui_local";
        out->command.accepted = 1;
        page->last_action = *out;
        page->navigation_cb(page->navigation_user_data, V5_MAIN_PAGE_ACTION_NAV_MAIN);
        return 1;
    }
    if (v5_settings_action_start(action, &action_result)) {
        out->prepared = 1;
        out->local_only = 0;
        out->request.kind = V5_COMMAND_UI_LOCAL;
        out->command.kind = V5_COMMAND_UI_LOCAL;
        out->command.name = action_result.name;
        out->command.owner = action_result.owner;
        out->command.accepted = 1;
        page->last_action = *out;
        return 1;
    }
    out->prepared = 0;
    out->local_only = 1;
    out->request.kind = V5_COMMAND_UI_LOCAL;
    out->command.kind = V5_COMMAND_UI_LOCAL;
    out->command.name = "settings_action_blocked";
    out->command.owner = "ui_layout_shell";
    out->command.accepted = 0;
    page->last_action = *out;
    return 1;
}
