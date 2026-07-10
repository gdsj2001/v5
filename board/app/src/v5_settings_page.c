#include "v5_settings_page.h"
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



static void json_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p;
    const char *end;
    char pattern[80];
    size_t n;
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
    p = strstr(json ? json : "", pattern);
    if (!p) {
        return;
    }
    p = strchr(p, ':');
    if (!p) {
        return;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p != '"') {
        return;
    }
    ++p;
    end = p;
    while (*end && *end != '"') {
        ++end;
    }
    n = (size_t)(end - p);
    if (n >= out_size) {
        n = out_size - 1U;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

static int is_six_digit_id(const char *value)
{
    size_t i;
    if (!value || strlen(value) != 6U) {
        return 0;
    }
    for (i = 0; i < 6U; ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static char g_resident_machine_code[16];
static int g_resident_machine_code_loaded;

void v5_settings_page_set_boot_closure(const V5BootClosure *closure)
{
    char value[32];
    g_resident_machine_code[0] = '\0';
    g_resident_machine_code_loaded = 0;
    if (!closure || !closure->device_register_status.loaded) {
        return;
    }
    json_string_field(closure->device_register_status.text, "vpsDistributionId", value, sizeof(value));
    if (is_six_digit_id(value)) {
        snprintf(g_resident_machine_code, sizeof(g_resident_machine_code), "%s", value);
        g_resident_machine_code_loaded = 1;
    }
}

static int resident_machine_code(char *out, size_t out_size)
{
    if (!out || out_size == 0U || !g_resident_machine_code_loaded) {
        return 0;
    }
    snprintf(out, out_size, "%s", g_resident_machine_code);
    return 1;
}

static void refresh_machine_code_label(V5SettingsPage *page)
{
    char id[16];
    char text[32];
    if (!page || !page->machine_code_label) {
        return;
    }
    if (resident_machine_code(id, sizeof(id))) {
        snprintf(text, sizeof(text), "本机码 %s", id);
        lv_label_set_text(page->machine_code_label, text);
        lv_obj_set_style_text_color(page->machine_code_label, rgb(42, 221, 128), 0);
        return;
    }
    lv_label_set_text(page->machine_code_label, "本机码 未登记");
    lv_obj_set_style_text_color(page->machine_code_label, rgb(155, 177, 198), 0);
}

static void set_status_text(V5SettingsPage *page, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    if (!page || !page->status_label || !fmt) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    lv_label_set_text(page->status_label, text);
    lv_obj_set_style_text_color(page->status_label, rgb(r, g, b), 0);
}

static const char *settings_status_action_label(const char *action)
{
    if (!action || !action[0]) {
        return "设置页动作";
    }
    if (strcmp(action, "device_dna_register") == 0) {
        return "登记本机码";
    }
    if (strcmp(action, "device_authorization_download") == 0) {
        return "下载授权";
    }
    if (strcmp(action, "drive_profile_server_download") == 0) {
        return "服务器下载";
    }
    if (strcmp(action, "drive_scan_slaves") == 0) {
        return "扫描从站";
    }
    if (strcmp(action, "drive_factory_reset") == 0) {
        return "复位驱动";
    }
    if (strcmp(action, "drive_parameter_read") == 0) {
        return "读取驱动";
    }
    if (strcmp(action, "drive_fault_reset") == 0) {
        return "清除故障";
    }
    if (strcmp(action, "drive_set_parameters") == 0) {
        return "设置驱动";
    }
    if (strcmp(action, "settings_axis_zero") == 0) {
        return "轴号缺失 设0";
    }
    return action;
}

static int settings_action_requires_countdown(const char *action)
{
    if (!action) return 0;
    return strcmp(action, "device_authorization_download") == 0 ||
           strcmp(action, "drive_profile_server_download") == 0 ||
           strcmp(action, "drive_scan_slaves") == 0 ||
           strcmp(action, "drive_factory_reset") == 0 ||
           strcmp(action, "drive_parameter_read") == 0 ||
           strcmp(action, "drive_fault_reset") == 0 ||
           strcmp(action, "drive_set_parameters") == 0 ||
           strcmp(action, "settings_axis_zero") == 0;
}

static void settings_status_popup_title(const V5SettingsActionStatus *status, char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (status && strcmp(status->action, "settings_axis_zero") == 0 && status->axis[0]) {
        snprintf(out, out_size, "%s 设0", status->axis);
        return;
    }
    snprintf(out, out_size, "%s", settings_status_action_label(status ? status->action : ""));
}

static int settings_action_eta_seconds(const char *action)
{
    if (!action) return 0;
    if (strcmp(action, "device_authorization_download") == 0) return 120;
    if (strcmp(action, "drive_profile_server_download") == 0) return 120;
    if (strcmp(action, "drive_scan_slaves") == 0) return 20;
    if (strcmp(action, "drive_factory_reset") == 0) return 90;
    if (strcmp(action, "drive_parameter_read") == 0) return 45;
    if (strcmp(action, "drive_fault_reset") == 0) return 45;
    if (strcmp(action, "drive_set_parameters") == 0) return 180;
    if (strcmp(action, "settings_axis_zero") == 0) return 20;
    return 0;
}

static int settings_action_refreshes_axis_table(const char *action)
{
    return action &&
           (strcmp(action, "drive_profile_server_download") == 0 ||
            strcmp(action, "drive_scan_slaves") == 0 ||
            strcmp(action, "drive_factory_reset") == 0 ||
            strcmp(action, "drive_parameter_read") == 0 ||
            strcmp(action, "drive_fault_reset") == 0 ||
            strcmp(action, "drive_set_parameters") == 0 ||
            strcmp(action, "settings_axis_zero") == 0);
}

static void settings_refresh_axis_table_once(V5SettingsPage *page, const V5SettingsActionStatus *status)
{
    const char *run_id;
    if (!page || !status || !status->ok || !settings_action_refreshes_axis_table(status->action)) {
        return;
    }
    run_id = status->run_id[0] ? status->run_id : status->action;
    if (strcmp(page->last_axis_table_refresh_run_id, run_id) == 0) {
        return;
    }
    snprintf(page->last_axis_table_refresh_run_id, sizeof(page->last_axis_table_refresh_run_id), "%s", run_id);
    v5_settings_axis_table_reload_current_readback();
}

static void settings_popup_set_eta(V5SettingsPage *page, int seconds_left)
{
    char text[32];
    if (!page || !page->popup_eta) return;
    if (seconds_left <= 0) {
        lv_label_set_text(page->popup_eta, "");
        return;
    }
    snprintf(text, sizeof(text), "预计: %02d:%02d", seconds_left / 60, seconds_left % 60);
    lv_label_set_text(page->popup_eta, text);
}

static void settings_popup_show(V5SettingsPage *page, const char *action, const char *title, const char *message, int final, int ok)
{
    int was_active;
    if (!page || !page->popup_overlay) return;
    was_active = page->popup_active;
    if (!was_active) {
        v5_ui_first_frame_guard_begin(&page->popup_frame_guard, V5_REMOTE_DISPLAY_CACHE_POPUP);
        page->popup_run_id[0] = '\0';
        page->popup_cancel_pending = 0;
    }
    snprintf(page->popup_action, sizeof(page->popup_action), "%s", action ? action : "");
    page->popup_active = 1;
    page->popup_final = final ? 1 : 0;
    page->popup_started_s = monotonic_seconds();
    page->popup_eta_seconds = (!final && settings_action_requires_countdown(action)) ? settings_action_eta_seconds(action) : 0;
    lv_label_set_text(page->popup_title, title && title[0] ? title : settings_status_action_label(action));
    lv_label_set_text(page->popup_message, message && message[0] ? message : (final ? "动作结束" : "动作已启动"));
    lv_obj_set_style_text_color(page->popup_message, final ? (ok ? rgb(42, 221, 128) : rgb(255, 96, 104)) : rgb(226, 238, 246), 0);
    settings_popup_set_eta(page, page->popup_eta_seconds);
    lv_obj_clear_flag(page->popup_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page->popup_overlay);
}

static void settings_popup_hide(V5SettingsPage *page)
{
    if (!page) {
        return;
    }
    if (page->popup_overlay) {
        lv_obj_add_flag(page->popup_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (page->popup_close) {
        lv_obj_clear_state(page->popup_close, LV_STATE_DISABLED);
    }
    page->popup_active = 0;
    page->popup_final = 0;
    page->popup_action[0] = '\0';
    page->popup_run_id[0] = '\0';
    page->popup_cancel_pending = 0;
    page->popup_eta_seconds = 0;
    v5_ui_first_frame_guard_restore(&page->popup_frame_guard);
    if (page->status_label) {
        lv_obj_invalidate(page->status_label);
    }
    if (page->machine_code_label) {
        lv_obj_invalidate(page->machine_code_label);
    }
}

static void settings_popup_update_running(V5SettingsPage *page, const char *title, const char *message)
{
    int left;
    if (!page || !page->popup_active || page->popup_final) return;
    if (title && title[0]) lv_label_set_text(page->popup_title, title);
    if (message && message[0]) lv_label_set_text(page->popup_message, message);
    if (page->popup_eta_seconds > 0) {
        left = page->popup_eta_seconds - (int)(monotonic_seconds() - page->popup_started_s);
        settings_popup_set_eta(page, left > 0 ? left : 1);
    }
}

static void settings_popup_update_final(V5SettingsPage *page, const char *title, int ok, const char *code, const char *message)
{
    char body[1024];
    const char *msg = message && message[0] ? message : (ok ? "动作完成" : "动作未完成");
    const char *next = ok ? "确认结果后点关闭" : "按提示处理后重新执行";
    if (!page || !page->popup_overlay) return;
    snprintf(body, sizeof(body), "提示: %s\n原因: %s\n下一步: %s", code && code[0] ? code : (ok ? "OK" : "FAILED"), msg, next);
    settings_popup_show(page, page->popup_action, title, body, 1, ok);
}

static void clear_button_pressed_visual_now(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_clear_state(button, LV_STATE_PRESSED);
    lv_obj_invalidate(button);
    lv_refr_now(NULL);
}

static void button_release_visual_cb(lv_event_t *event)
{
    clear_button_pressed_visual_now(lv_event_get_target(event));
}

static void settings_popup_close_cb(lv_event_t *event)
{
    V5SettingsPage *page = (V5SettingsPage *)lv_event_get_user_data(event);
    V5SettingsActionStatus status;
    if (!page || lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    clear_button_pressed_visual_now(lv_event_get_target(event));
    if (page->popup_final) {
        settings_popup_hide(page);
        return;
    }
    if (!page->popup_run_id[0] && v5_settings_action_poll_status(&status) &&
        status.available && status.busy &&
        (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0)) {
        snprintf(page->popup_run_id, sizeof(page->popup_run_id), "%s", status.run_id);
    }
    if (!page->popup_cancel_pending && page->popup_run_id[0] &&
        v5_settings_action_cancel(page->popup_run_id)) {
        page->popup_cancel_pending = 1;
        if (page->popup_close) {
            lv_obj_add_state(page->popup_close, LV_STATE_DISABLED);
        }
        settings_popup_update_running(page, 0, "提示: CANCELLING\n原因: 正在终止本次后台进程及进程组\n下一步: 等待 cancelled 终态");
    } else if (!page->popup_cancel_pending) {
        settings_popup_update_running(page, 0, "提示: CANCEL_NOT_ACCEPTED\n原因: 未取得本次 run_id 或后台未接受取消\n下一步: 保持窗口并重新确认后台状态");
    }
}

static void settings_popup_create(V5SettingsPage *page)
{
    lv_obj_t *box;
    lv_obj_t *close_label;
    if (!page || !page->root) return;
    page->popup_overlay = make_panel(page->root, 0, 0, 1024, 600, 3, 16, 26);
    lv_obj_set_style_bg_opa(page->popup_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(page->popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    box = make_panel(page->popup_overlay, 218, 92, 588, 420, 7, 31, 48);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, rgb(76, 119, 146), 0);
    page->popup_title = make_label(box, "设置页动作", 22, 18, 544, 32, 88, 204, 255);
    lv_obj_set_style_text_align(page->popup_title, LV_TEXT_ALIGN_CENTER, 0);
    page->popup_message = make_label(box, "", 34, 62, 520, 250, 226, 238, 246);
    lv_label_set_long_mode(page->popup_message, LV_LABEL_LONG_WRAP);
    page->popup_eta = make_label(box, "", 34, 328, 160, 24, 245, 214, 82);
    page->popup_close = lv_btn_create(box);
    clear_obj_style(page->popup_close);
    lv_obj_set_pos(page->popup_close, 434, 348);
    lv_obj_set_size(page->popup_close, 118, 44);
    lv_obj_set_style_bg_color(page->popup_close, rgb(42, 86, 116), 0);
    lv_obj_set_style_bg_color(page->popup_close, rgb(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(page->popup_close, 1, 0);
    lv_obj_set_style_border_color(page->popup_close, rgb(76, 119, 146), 0);
    lv_obj_add_event_cb(page->popup_close, button_release_visual_cb, LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(page->popup_close, settings_popup_close_cb, LV_EVENT_RELEASED, page);
    close_label = lv_label_create(page->popup_close);
    lv_label_set_text(close_label, "关闭");
    lv_obj_set_pos(close_label, 0, 10);
    lv_obj_set_size(close_label, 118, 26);
    lv_obj_set_style_text_align(close_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(close_label, rgb(238, 245, 248), 0);
    lv_obj_add_flag(page->popup_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void settings_status_timer_cb(lv_timer_t *timer)
{
    V5SettingsPage *page = timer ? (V5SettingsPage *)timer->user_data : 0;
    V5SettingsActionStatus status;
    char label[96];
    const char *detail;
    char running[256];
    if (!page || !page->root || !page->status_label) {
        return;
    }
    if (lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (!v5_settings_action_poll_status(&status) || !status.available) {
        if (page->popup_active && !page->popup_final) {
            settings_popup_update_running(page, 0, "等待后台状态...");
        }
        return;
    }
    settings_status_popup_title(&status, label, sizeof(label));
    detail = status.message[0] ? status.message : status.code;
    if (status.busy) {
        if (page->popup_active && !page->popup_final &&
            (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0)) {
            if (!page->popup_run_id[0]) {
                snprintf(page->popup_run_id, sizeof(page->popup_run_id), "%s", status.run_id);
            }
            if (strcmp(page->popup_run_id, status.run_id) == 0) {
                snprintf(running, sizeof(running), "提示: %s\n原因: %s\n下一步: %s",
                         page->popup_cancel_pending ? "CANCELLING" : "RUNNING",
                         detail[0] ? detail : "执行中",
                         page->popup_cancel_pending ? "等待 cancelled 终态" : "等待后台完成");
                settings_popup_update_running(page, label, running);
            }
        }
        set_status_text(page, 88, 204, 255, "%s: 执行中", label);
    } else if (status.ok) {
        if (page->popup_active && !page->popup_final &&
            (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0) &&
            (!page->popup_run_id[0] || strcmp(page->popup_run_id, status.run_id) == 0)) {
            settings_popup_update_final(page, label, 1, status.code, detail);
        }
        settings_refresh_axis_table_once(page, &status);
        if (strcmp(status.action, "device_dna_register") == 0) {
            refresh_machine_code_label(page);
        }
        set_status_text(page, 42, 221, 128, "%s: 完成 %s", label, status.code[0] ? status.code : detail);
    } else {
        if (page->popup_active && !page->popup_final &&
            (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0) &&
            (!page->popup_run_id[0] || strcmp(page->popup_run_id, status.run_id) == 0)) {
            if (page->popup_cancel_pending && strcmp(status.code, "SETTINGS_ACTION_CANCELLED") == 0) {
                settings_popup_hide(page);
            } else {
                settings_popup_update_final(page, label, 0, status.code, detail);
            }
        }
        set_status_text(page, 245, 214, 82, "%s: %s", label, detail[0] ? detail : "未完成");
    }
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

static void settings_parameter_changed_cb(void *user_data)
{
    V5SettingsPage *page = (V5SettingsPage *)user_data;
    if (!page) {
        return;
    }
    set_status_text(page, 42, 221, 128, "参数已保存，等待保存并重启");
}

static void settings_axis_zero_requested_cb(const char *axis,
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
        set_status_text(page, 88, 204, 255, "%s: 后端校验已启动", title);
        settings_popup_show(page, "settings_axis_zero", title, body, 0, 0);
        return;
    }
    snprintf(body, sizeof(body),
             "提示: SETTINGS_AXIS_ZERO_START_FAILED\n原因: %s\n下一步: 检查 settings_actiond 后重新设0",
             action_result.message ? action_result.message : "后端动作通道未接受请求");
    set_status_text(page, 245, 214, 82, "%s: 后端未接受", title);
    settings_popup_show(page, "settings_axis_zero", title, body, 1, 0);
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
            clear_button_pressed_visual_now(target);
            if (v5_settings_page_trigger_action(page, page->button_actions[i], &report)) {
                log_button_event(report.action, &report);
            }
            return;
        }
    }
}

static lv_obj_t *make_button(V5SettingsPage *page, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, V5MainPageActionKind action)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_SETTINGS_PAGE_BUTTON_COUNT) {
        return 0;
    }
    button = lv_btn_create(page->root);
    clear_obj_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, rgb(r, g, b), 0);
    lv_obj_set_style_bg_color(button, rgb(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, rgb(76, 119, 146), 0);
    lv_obj_add_event_cb(button, button_release_visual_cb, LV_EVENT_RELEASED, 0);
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
    return label;
}

static void make_value_cell_colored(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int muted, uint8_t tr, uint8_t tg, uint8_t tb)
{
    lv_obj_t *cell = make_panel(parent, x, y, w, h, muted ? 38 : 5, muted ? 54 : 27, muted ? 65 : 43);
    lv_obj_t *label = make_label(cell, text, 0, 5, w, h - 6, muted ? 150 : tr, muted ? 170 : tg, muted ? 190 : tb);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void settings_motion_model_refresh_dropdown(V5SettingsPage *page)
{
    const V5MotionModelDescriptor *model;
    unsigned int model_index = 0U;
    if (!page || !page->motion_model_dropdown) {
        return;
    }
    model = v5_motion_model_find(v5_settings_axis_table_motion_model_value());
    if (!v5_motion_model_registry_index(model, &model_index)) {
        lv_dropdown_set_selected(page->motion_model_dropdown, 0U);
        return;
    }
    lv_dropdown_set_selected(page->motion_model_dropdown, model_index + 1U);
}

static void settings_motion_model_changed_cb(lv_event_t *event)
{
    V5SettingsPage *page = (V5SettingsPage *)lv_event_get_user_data(event);
    char selected[64];
    const V5MotionModelDescriptor *model;
    int ok;
    if (!page || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || !page->motion_model_dropdown) {
        return;
    }
    selected[0] = '\0';
    lv_dropdown_get_selected_str(page->motion_model_dropdown, selected, sizeof(selected));
    model = v5_motion_model_find(selected);
    if (!model) {
        settings_motion_model_refresh_dropdown(page);
        set_status_text(page, 245, 214, 82, "运动模型: 未登记模型");
        return;
    }
    ok = v5_settings_axis_table_commit_motion_model(model->canonical);
    settings_motion_model_refresh_dropdown(page);
    if (ok) {
        set_status_text(page, 42, 221, 128, "运动模型: %s", v5_settings_axis_table_motion_model_value());
    } else {
        set_status_text(page, 245, 214, 82, "运动模型: owner readback 未确认");
    }
}

static lv_obj_t *make_motion_model_dropdown(V5SettingsPage *page, lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *dd;
    char registry_options[128];
    char options[136];
    if (!page || !parent) {
        return 0;
    }
    if (!v5_motion_model_dropdown_options(registry_options, sizeof(registry_options))) {
        return 0;
    }
    snprintf(options, sizeof(options), "--\n%s", registry_options);
    dd = lv_dropdown_create(parent);
    lv_obj_set_pos(dd, x, y);
    lv_obj_set_size(dd, w, h);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_style_bg_color(dd, rgb(5, 27, 43), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_radius(dd, 2, 0);
    lv_obj_set_style_pad_all(dd, 0, 0);
    lv_obj_set_style_text_color(dd, rgb(150, 170, 190), 0);
    lv_obj_set_style_text_align(dd, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dd, settings_motion_model_changed_cb, LV_EVENT_VALUE_CHANGED, page);
    page->motion_model_dropdown = dd;
    settings_motion_model_refresh_dropdown(page);
    return dd;
}

static void make_value_cell(lv_obj_t *parent, const char *text, int x, int y, int w, int h, int muted)
{
    make_value_cell_colored(parent, text, x, y, w, h, muted, 226, 238, 246);
}

static void settings_axis_color(const char *axis, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!axis || !r || !g || !b) return;
    *r = 218;
    *g = 232;
    *b = 242;
    if (strcmp(axis, "X") == 0 || strcmp(axis, "A") == 0) {
        *r = 255;
        *g = 100;
        *b = 106;
    } else if (strcmp(axis, "Y") == 0) {
        *r = 0;
        *g = 232;
        *b = 150;
    } else if (strcmp(axis, "Z") == 0) {
        *r = 82;
        *g = 178;
        *b = 255;
    } else if (strcmp(axis, "C") == 0) {
        *r = 0;
        *g = 225;
        *b = 220;
    }
}

static void format_settings_mcs_value(double value, int valid, char *out, size_t out_size)
{
    double abs_value;
    long whole;
    long frac;
    int visible_whole_digits = 3;
    if (!out || out_size == 0U) {
        return;
    }
    if (!valid || value != value || value > 99999999.0 || value < -99999999.0) {
        snprintf(out, out_size, "--");
        return;
    }
    if (value > -0.0005 && value < 0.0005) {
        value = 0.0;
    }
    abs_value = value < 0.0 ? -value : value;
    whole = (long)abs_value;
    frac = (long)((abs_value - (double)whole) * 1000.0 + 0.5);
    if (frac >= 1000) {
        frac -= 1000;
        ++whole;
    }
    if (whole >= 10000) {
        visible_whole_digits = 5;
    } else if (whole >= 1000) {
        visible_whole_digits = 4;
    }
    if (whole > 99999) {
        snprintf(out, out_size, "%c99999.999", value < 0.0 ? '<' : '>');
    } else {
        snprintf(out, out_size, "%s%0*ld.%03ld", value < 0.0 ? "-" : "", visible_whole_digits, whole, frac);
    }
}

static lv_obj_t *make_settings_mcs_value(lv_obj_t *panel, int row_y)
{
    lv_obj_t *value = make_label(panel, "--", 76, row_y, 124, 30, 88, 204, 255);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_bg_color(value, rgb(6, 26, 39), 0);
    lv_obj_set_style_bg_opa(value, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_opa(value, LV_OPA_TRANSP, 0);
    return value;
}

static void make_settings_machine_coordinate_widget(V5SettingsPage *page, lv_obj_t *root)
{
    static const char *axes[] = {"X", "Y", "Z", "A", "C"};
    lv_obj_t *outer = make_panel(root, 696, 42, 312, 176, 7, 31, 48);
    lv_obj_t *panel = make_panel(root, 696, 42, 216, 176, 6, 26, 39);
    lv_obj_t *unit;
    (void)outer;
    lv_obj_set_style_border_color(panel, rgb(84, 96, 102), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    make_label(panel, "机械坐标", 12, 8, 92, 24, 88, 204, 255);
    unit = make_label(panel, "MCS", 158, 8, 44, 24, 155, 177, 198);
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_RIGHT, 0);
    if (page) {
        v5_coordinate_digits_create_settings(&page->mcs_digits, panel, page->mcs_digits_buffer);
    }
    for (unsigned int i = 0U; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        int row_y = 40 + (int)i * 25;
        settings_axis_color(axes[i], &r, &g, &b);
        make_label(panel, axes[i], 12, row_y, 32, 26, r, g, b);
        if (page && i < V5_MAIN_PAGE_AXIS_COUNT) {
            page->mcs_labels[i] = make_settings_mcs_value(panel, 39 + (int)i * 25);
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
    int i;
    if (!page || !parent) {
        return 0;
    }
    v5_settings_page_init(page);
    page->root = lv_obj_create(parent);
    v5_settings_axis_table_begin_page();
    clear_obj_style(page->root);
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_size(page->root, 1024, 600);
    lv_obj_set_style_bg_color(page->root, rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);

    make_label(page->root, "参数设置", 30, 17, 100, 24, 226, 238, 246);
    page->machine_code_label = make_label(page->root, "本机码 未登记", 500, 17, 198, 24, 155, 177, 198);
    refresh_machine_code_label(page);

    make_panel(page->root, 16, 42, 220, 176, 7, 31, 48);
    make_label(page->root, "运动模型", 24, 55, 100, 24, 155, 177, 198);
    make_motion_model_dropdown(page, page->root, 25, 78, 190, 36);
    make_label(page->root, "脉冲/总线", 24, 132, 100, 24, 226, 238, 246);
    make_value_cell_colored(page->root,
                            v5_settings_axis_table_bus_pulse_value(),
                            162,
                            127,
                            70,
                            31,
                            0,
                            150,
                            170,
                            190);

    make_panel(page->root, 247, 42, 449, 176, 7, 31, 48);
    make_label(page->root, "机床坐标目标位置表(G53)", 264, 55, 230, 24, 0, 190, 255);
    make_label(page->root, "X", 395, 86, 36, 20, 155, 177, 198);
    make_label(page->root, "Y", 479, 86, 36, 20, 155, 177, 198);
    make_label(page->root, "Z", 563, 86, 36, 20, 155, 177, 198);
    {
        static const char *g53_labels[] = {"A中心", "B中心", "C中心", "对刀仪", "5方向监测仪"};
        static const unsigned char g53_label_colors[][3] = {
            {255, 100, 106},
            {226, 238, 246},
            {0, 225, 220},
            {155, 177, 198},
            {155, 177, 198},
        };
        for (i = 0; i < 5; ++i) {
            make_label(page->root,
                       g53_labels[i],
                       276,
                       107 + i * 22,
                       i == 4 ? 100 : 68,
                       22,
                       g53_label_colors[i][0],
                       g53_label_colors[i][1],
                       g53_label_colors[i][2]);
        }
    }
    for (i = 0; i < 15; ++i) {
        unsigned int row = (unsigned int)(i / 3);
        unsigned int col = (unsigned int)(i % 3);
        v5_settings_axis_table_create_g53_cell(page->root,
                                               row,
                                               col,
                                               366 + (i % 3) * 84,
                                               106 + (i / 3) * 22,
                                               76,
                                               22);
    }

    make_settings_machine_coordinate_widget(page, page->root);

    v5_settings_axis_table_set_commit_callback(settings_parameter_changed_cb, page);
    v5_settings_axis_table_set_axis_zero_callback(settings_axis_zero_requested_cb, page);
    v5_settings_axis_table_create(page->root);
    page->status_label = make_label(page->root, "", 24, 34, 1, 1, 155, 177, 198);
    page->status_timer = lv_timer_create(settings_status_timer_cb, 250, page);

    make_button(page, "下载授权", 410, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD);
    make_button(page, "服务器下载", 494, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD);
    make_button(page, "扫描从站", 578, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_SCAN);
    make_button(page, "复位驱动", 662, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET);
    make_button(page, "读取驱动", 746, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_READ);
    make_button(page, "清除故障", 830, 236, 82, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET);
    make_button(page, "设置驱动", 914, 236, 82, 34, 39, 113, 164, V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE);
    make_button(page, "登记本机码", 806, 8, 94, 34, 20, 62, 91, V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER);
    make_button(page, "保存并重启", 902, 8, 92, 34, 74, 91, 111, V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN);
    settings_popup_create(page);
    return page->button_count == V5_SETTINGS_PAGE_BUTTON_COUNT;
}

int v5_settings_page_apply_status(V5SettingsPage *page, const V5UiStatusView *status)
{
    char text[24];
    int valid;
    if (!page) {
        return 0;
    }
    valid = status && ((status->valid_mask & V5_STATUS_VALID_MCS) != 0U);
    for (unsigned int i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        if (!page->mcs_labels[i]) {
            continue;
        }
        format_settings_mcs_value(valid ? status->mcs[i] : 0.0, valid, text, sizeof(text));
        lv_label_set_text(page->mcs_labels[i], text);
        lv_obj_set_style_text_color(page->mcs_labels[i], valid ? rgb(88, 204, 255) : rgb(155, 177, 198), 0);
        v5_coordinate_digits_set_value(
            &page->mcs_digits,
            0U,
            i,
            text,
            valid ? rgb(88, 204, 255) : rgb(155, 177, 198));
    }
    return 1;
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
    if (v5_settings_action_start(action, &action_result)) {
        out->prepared = 1;
        out->local_only = 0;
        out->request.kind = V5_COMMAND_UI_LOCAL;
        out->command.kind = V5_COMMAND_UI_LOCAL;
        out->command.name = action_result.name;
        out->command.owner = action_result.owner;
        out->command.accepted = 1;
        page->last_action = *out;
        set_status_text(page, 88, 204, 255, "%s: 已启动 %s",
                        v5_main_page_action_label(action),
                        action_result.result_path ? action_result.result_path : "");
        {
            const char *daemon_action = action_result.message ? action_result.message : "accepted";
            char body[256];
            snprintf(body, sizeof(body), "提示: 已启动\n原因: %s\n下一步: 等待后台完成", daemon_action);
            settings_popup_show(page, action_result.daemon_action, settings_status_action_label(action_result.daemon_action), body, 0, 0);
        }
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
    set_status_text(page, 245, 214, 82, "%s: 已阻断 %s",
                    v5_main_page_action_label(action),
                    action_result.message ? action_result.message : "unsupported");
    {
        char body[256];
        snprintf(body, sizeof(body), "提示: BLOCKED\n原因: %s\n下一步: 检查后台动作通道", action_result.message ? action_result.message : "unsupported");
        settings_popup_show(page, "", v5_main_page_action_label(action), body, 1, 0);
    }
    return 1;
}
