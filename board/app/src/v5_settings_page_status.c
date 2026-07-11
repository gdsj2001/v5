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

void v5_settings_page_set_status_text(V5SettingsPage *page, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...)
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
    lv_obj_set_style_text_color(page->status_label, v5_settings_page_rgb(r, g, b), 0);
}

const char *v5_settings_page_status_action_label(const char *action)
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
    snprintf(out, out_size, "%s", v5_settings_page_status_action_label(status ? status->action : ""));
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

void v5_settings_page_popup_show(V5SettingsPage *page, const char *action, const char *title, const char *message, int final, int ok)
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
    page->popup_started_s = v5_settings_page_monotonic_seconds();
    page->popup_eta_seconds = (!final && settings_action_requires_countdown(action)) ? settings_action_eta_seconds(action) : 0;
    lv_label_set_text(page->popup_title, title && title[0] ? title : v5_settings_page_status_action_label(action));
    lv_label_set_text(page->popup_message, message && message[0] ? message : (final ? "动作结束" : "动作已启动"));
    lv_obj_set_style_text_color(page->popup_message, final ? (ok ? v5_settings_page_rgb(42, 221, 128) : v5_settings_page_rgb(255, 96, 104)) : v5_settings_page_rgb(226, 238, 246), 0);
    settings_popup_set_eta(page, page->popup_eta_seconds);
    lv_obj_clear_flag(page->popup_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page->popup_overlay);
}

void v5_settings_page_action_visual_clear(V5SettingsPage *page, int clear_binding)
{
    if (!page) {
        return;
    }
    if (page->action_visual_active && page->action_visual_button) {
        v5_button_visual_set_transaction_active(page->action_visual_button, 0);
    }
    page->action_visual_active = 0;
    if (clear_binding) {
        page->action_visual_button = 0;
        page->action_visual_name[0] = '\0';
    }
}

void v5_settings_page_action_visual_bind(V5SettingsPage *page, lv_obj_t *button, const char *action)
{
    if (!page) {
        return;
    }
    v5_settings_page_action_visual_clear(page, 1);
    if (!button || !action || !action[0]) {
        return;
    }
    page->action_visual_button = button;
    snprintf(page->action_visual_name, sizeof(page->action_visual_name), "%s", action);
}

static void settings_action_visual_apply_status(
    V5SettingsPage *page,
    const V5SettingsActionStatus *status)
{
    int matches;
    if (!page || !status || !status->available || !page->action_visual_button ||
        !page->action_visual_name[0]) {
        v5_settings_page_action_visual_clear(page, status && status->available ? 0 : 1);
        return;
    }
    matches = strcmp(page->action_visual_name, status->action) == 0;
    if (!matches) {
        return;
    }
    if (status->busy) {
        if (!page->action_visual_active) {
            v5_button_visual_set_transaction_active(page->action_visual_button, 1);
            page->action_visual_active = 1;
        }
        return;
    }
    v5_settings_page_action_visual_clear(page, 1);
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
        v5_button_visual_set_transaction_active(page->popup_close, 0);
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
        left = page->popup_eta_seconds - (int)(v5_settings_page_monotonic_seconds() - page->popup_started_s);
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
    v5_settings_page_popup_show(page, page->popup_action, title, body, 1, ok);
}

static void settings_popup_close_cb(lv_event_t *event)
{
    V5SettingsPage *page = (V5SettingsPage *)lv_event_get_user_data(event);
    V5SettingsActionStatus status;
    if (!page || lv_event_get_code(event) != LV_EVENT_RELEASED) return;
    v5_button_visual_release_now(lv_event_get_target(event));
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
        v5_button_visual_set_transaction_active(page->popup_close, 1);
        if (page->popup_close) {
            lv_obj_add_state(page->popup_close, LV_STATE_DISABLED);
        }
        settings_popup_update_running(page, 0, "提示: CANCELLING\n原因: 正在终止本次后台进程及进程组\n下一步: 等待 cancelled 终态");
    } else if (!page->popup_cancel_pending) {
        settings_popup_update_running(page, 0, "提示: CANCEL_NOT_ACCEPTED\n原因: 未取得本次 run_id 或后台未接受取消\n下一步: 保持窗口并重新确认后台状态");
    }
}

void v5_settings_page_popup_create(V5SettingsPage *page)
{
    lv_obj_t *box;
    lv_obj_t *close_label;
    if (!page || !page->root) return;
    page->popup_overlay = v5_settings_page_make_panel(page->root, 0, 0, 1024, 600, 3, 16, 26);
    lv_obj_set_style_bg_opa(page->popup_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(page->popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    box = v5_settings_page_make_panel(page->popup_overlay, 218, 92, 588, 420, 7, 31, 48);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, v5_settings_page_rgb(76, 119, 146), 0);
    page->popup_title = v5_settings_page_make_label(box, "设置页动作", 22, 18, 544, 32, 88, 204, 255);
    lv_obj_set_style_text_align(page->popup_title, LV_TEXT_ALIGN_CENTER, 0);
    page->popup_message = v5_settings_page_make_label(box, "", 34, 62, 520, 250, 226, 238, 246);
    lv_label_set_long_mode(page->popup_message, LV_LABEL_LONG_WRAP);
    page->popup_eta = v5_settings_page_make_label(box, "", 34, 328, 160, 24, 245, 214, 82);
    page->popup_close = lv_btn_create(box);
    v5_settings_page_clear_obj_style(page->popup_close);
    lv_obj_set_pos(page->popup_close, 434, 348);
    lv_obj_set_size(page->popup_close, 118, 44);
    lv_obj_set_style_bg_color(page->popup_close, v5_settings_page_rgb(42, 86, 116), 0);
    lv_obj_set_style_border_width(page->popup_close, 1, 0);
    lv_obj_set_style_border_color(page->popup_close, v5_settings_page_rgb(76, 119, 146), 0);
    v5_button_visual_bind(page->popup_close);
    lv_obj_add_event_cb(page->popup_close, settings_popup_close_cb, LV_EVENT_RELEASED, page);
    close_label = lv_label_create(page->popup_close);
    lv_label_set_text(close_label, "关闭");
    lv_obj_set_pos(close_label, 0, 10);
    lv_obj_set_size(close_label, 118, 26);
    lv_obj_set_style_text_align(close_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(close_label, v5_settings_page_rgb(238, 245, 248), 0);
    lv_obj_add_flag(page->popup_overlay, LV_OBJ_FLAG_HIDDEN);
}

void v5_settings_page_status_timer_cb(lv_timer_t *timer)
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
        v5_settings_page_action_visual_clear(page, 1);
        if (page->popup_active && !page->popup_final) {
            settings_popup_update_running(page, 0, "等待后台状态...");
        }
        return;
    }
    settings_action_visual_apply_status(page, &status);
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
        v5_settings_page_set_status_text(page, 88, 204, 255, "%s: 执行中", label);
    } else if (status.ok) {
        if (page->popup_active && !page->popup_final &&
            (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0) &&
            (!page->popup_run_id[0] || strcmp(page->popup_run_id, status.run_id) == 0)) {
            settings_popup_update_final(page, label, 1, status.code, detail);
        }
        settings_refresh_axis_table_once(page, &status);
        if (strcmp(status.action, "device_dna_register") == 0) {
            v5_settings_page_refresh_machine_code_label(page);
        }
        v5_settings_page_set_status_text(page, 42, 221, 128, "%s: 完成 %s", label, status.code[0] ? status.code : detail);
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
        v5_settings_page_set_status_text(page, 245, 214, 82, "%s: %s", label, detail[0] ? detail : "未完成");
    }
}
