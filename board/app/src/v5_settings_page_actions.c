#include "v5_settings_page.h"
#include "v5_button_visuals.h"
#include "v5_settings_actions.h"
#include "v5_settings_axis_table.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_page_internal.h"

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

void v5_settings_page_parameter_changed_cb(void *user_data)
{
    V5SettingsPage *page = (V5SettingsPage *)user_data;
    if (!page) {
        return;
    }
    v5_settings_page_set_status_text(page, 42, 221, 128, "参数已保存，等待保存并重启");
}

void v5_settings_page_axis_zero_requested_cb(const char *axis,
                                            const char *driver_mode,
                                            const char *target_scope,
                                            const char *apply_mode,
                                            const char *slave_index,
                                            const char *home_offset,
                                            void *user_data)
{
    V5SettingsPage *page = (V5SettingsPage *)user_data;
    V5SettingsActionResult action_result;
    char title[96];
    char body[768];
    int pulse = driver_mode && strcmp(driver_mode, "pulse") == 0;
    if (!page) {
        return;
    }
    snprintf(title, sizeof(title), "%s%s", axis && axis[0] ? axis : "轴", pulse ? " 设零" : " 设0");
    if (v5_settings_axis_zero_start(axis, driver_mode, target_scope, apply_mode, slave_index, home_offset, &action_result)) {
        snprintf(body, sizeof(body),
                 "提示: 已启动\n原因: 后端将读取%s轴编码器当前位置，先写成本次硬盘新零位，再按%s差值 <= 0.1 读回校验；本动作不打开键盘、不写驱动 preset。\n下一步: 等待成功/失败结果弹窗",
                 axis && axis[0] ? axis : "",
                 pulse ? "deg" : "mm");
        v5_settings_page_set_status_text(page, 88, 204, 255, "%s: 后端校验已启动", title);
        v5_settings_page_popup_show(page, "settings_axis_zero", title, body, 0, 0);
        return;
    }
    snprintf(body, sizeof(body),
             "提示: SETTINGS_AXIS_ZERO_START_FAILED\n原因: %s\n下一步: 检查 settings_actiond 后重新设0",
             action_result.message ? action_result.message : "后端动作通道未接受请求");
    v5_settings_page_set_status_text(page, 245, 214, 82, "%s: 后端未接受", title);
    v5_settings_page_popup_show(page, "settings_axis_zero", title, body, 1, 0);
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
    fprintf(fp, "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"settings_button_clicked\",\"action\":", v5_settings_page_monotonic_seconds());
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
            v5_button_visual_release_now(target);
            if (v5_settings_page_trigger_action(page, page->button_actions[i], &report)) {
                log_button_event(report.action, &report);
                if (report.prepared && !report.local_only && page->popup_action[0]) {
                    v5_settings_page_action_visual_bind(page, target, page->popup_action);
                } else {
                    v5_settings_page_action_visual_clear(page, 1);
                }
            }
            return;
        }
    }
}

lv_obj_t *v5_settings_page_make_button(V5SettingsPage *page, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, V5MainPageActionKind action)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_SETTINGS_PAGE_BUTTON_COUNT) {
        return 0;
    }
    button = lv_btn_create(page->root);
    v5_settings_page_clear_obj_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, v5_settings_page_rgb(r, g, b), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, v5_settings_page_rgb(76, 119, 146), 0);
    v5_button_visual_bind(button);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, page);
    label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, 0, 4);
    lv_obj_set_size(label, w, h - 6);
    lv_obj_set_style_text_color(label, v5_settings_page_rgb(238, 245, 248), 0);
    lv_obj_set_style_text_color(label, v5_settings_page_rgb(16, 20, 24), LV_STATE_PRESSED);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    page->buttons[page->button_count] = button;
    page->button_actions[page->button_count] = action;
    page->button_count += 1u;
    return label;
}
