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
#include "v5_popup_layout.h"

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
           strcmp(action, "drive_set_parameters") == 0;
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

static void settings_popup_set_close_enabled(V5SettingsPage *page, int enabled)
{
    if (!page || !page->popup_close) {
        return;
    }
    (void)v5_ui_first_frame_guard_set_disabled(
        &page->popup_frame_guard,
        page->popup_close,
        enabled ? 0 : 1);
}

static void settings_popup_set_eta(V5SettingsPage *page, int seconds_left)
{
    char text[32];
    if (!page || !page->popup_eta) return;
    if (seconds_left <= 0) {
        (void)v5_ui_first_frame_guard_set_label_text(&page->popup_frame_guard, page->popup_eta, "");
        return;
    }
    snprintf(text, sizeof(text), "预计: %02d:%02d", seconds_left / 60, seconds_left % 60);
    (void)v5_ui_first_frame_guard_set_label_text(&page->popup_frame_guard, page->popup_eta, text);
}

void v5_settings_page_popup_show(V5SettingsPage *page, const char *action, const char *title, const char *message, int final, int ok)
{
    int was_active;
    lv_color_t message_color;
    if (!page || !page->popup_overlay) return;
    was_active = page->popup_active;
    if (!was_active) {
        if (!v5_ui_first_frame_guard_begin_overlay(&page->popup_frame_guard)) {
            return;
        }
        page->popup_run_id[0] = '\0';
        page->popup_cancel_pending = 0;
    }
    snprintf(page->popup_action, sizeof(page->popup_action), "%s", action ? action : "");
    page->popup_active = 1;
    page->popup_final = final ? 1 : 0;
    page->popup_started_s = v5_settings_page_monotonic_seconds();
    page->popup_eta_seconds = (!final && settings_action_requires_countdown(action)) ? settings_action_eta_seconds(action) : 0;
    (void)v5_ui_first_frame_guard_set_label_text(
        &page->popup_frame_guard,
        page->popup_title,
        title && title[0] ? title : v5_settings_page_status_action_label(action));
    (void)v5_ui_first_frame_guard_set_label_text(
        &page->popup_frame_guard,
        page->popup_message,
        message && message[0] ? message : (final ? "动作结束" : "正在处理"));
    message_color = final ?
        (ok ? v5_settings_page_rgb(42, 221, 128) : v5_settings_page_rgb(255, 96, 104)) :
        v5_settings_page_rgb(226, 238, 246);
    (void)v5_ui_first_frame_guard_set_text_color(
        &page->popup_frame_guard,
        page->popup_message,
        message_color);
    settings_popup_set_eta(page, page->popup_eta_seconds);
    settings_popup_set_close_enabled(page, final ? 1 : 0);
    if (!was_active &&
        !v5_ui_first_frame_guard_present_overlay(&page->popup_frame_guard, page->popup_overlay)) {
        page->popup_active = 0;
        page->popup_final = 0;
        page->popup_action[0] = '\0';
        page->popup_run_id[0] = '\0';
        page->popup_cancel_pending = 0;
        page->popup_eta_seconds = 0;
    }
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
    if (page->popup_overlay &&
        !v5_ui_first_frame_guard_dismiss_overlay(&page->popup_frame_guard, page->popup_overlay)) {
        return;
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
    if (title && title[0]) {
        (void)v5_ui_first_frame_guard_set_label_text(&page->popup_frame_guard, page->popup_title, title);
    }
    if (message && message[0]) {
        (void)v5_ui_first_frame_guard_set_label_text(&page->popup_frame_guard, page->popup_message, message);
    }
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
    if (lv_indev_get_act()) {
        lv_indev_wait_release(lv_indev_get_act());
    }
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
            settings_popup_set_close_enabled(page, 0);
        }
        settings_popup_update_running(page, 0, "提示: 正在取消\n原因: 正在终止本次后台进程及进程组\n下一步: 等待取消完成");
    } else if (!page->popup_cancel_pending) {
        settings_popup_update_running(page, 0, "提示: 取消未受理\n原因: 尚未取得本次任务标识或后台未接受取消\n下一步: 保持窗口并重新读取后台状态");
    }
}

void v5_settings_page_popup_create(V5SettingsPage *page)
{
    V5PopupLayoutConfig config = {0};
    V5PopupLayoutObjects popup = {0};
    if (!page || !page->root) return;
    config.title = "设置页动作";
    config.message = "";
    config.status = "";
    config.confirm_text = "确认继续";
    config.confirm_enabled = 0;
    config.close_text = "关闭";
    config.close_enabled = 1;
    config.close_cb = settings_popup_close_cb;
    config.close_user_data = page;
    if (!v5_popup_layout_create(lv_obj_get_screen(page->root), &config, &popup)) {
        return;
    }
    page->popup_overlay = popup.overlay;
    page->popup_title = popup.title;
    page->popup_message = popup.message;
    page->popup_eta = popup.status;
    page->popup_confirm = popup.confirm;
    page->popup_close = popup.close;
}

void v5_settings_page_status_timer_cb(lv_timer_t *timer)
{
    V5SettingsPage *page = timer ? (V5SettingsPage *)timer->user_data : 0;
    V5SettingsActionStatus status;
    char label[96];
    const char *detail;
    char running[256];
    int modal_active;
    if (!page || !page->root || !page->status_label) {
        return;
    }
    if (lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    modal_active = v5_ui_first_frame_guard_overlay_active();
    if (!v5_settings_action_poll_status(&status) || !status.available) {
        if (!modal_active) {
            v5_settings_page_action_visual_clear(page, 1);
        }
        if (page->popup_active && !page->popup_final) {
            settings_popup_update_running(page, 0, "提示: 正在读取状态\n原因: 暂未取得后台回读\n下一步: 保持窗口并等待状态更新");
        }
        return;
    }
    if (!modal_active) {
        settings_action_visual_apply_status(page, &status);
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
                settings_popup_set_close_enabled(
                    page,
                    status.cancel_allowed && !page->popup_cancel_pending);
                snprintf(running, sizeof(running), "提示: %s\n原因: %s\n下一步: %s",
                         page->popup_cancel_pending ? "正在取消" : "正在处理",
                         detail[0] ? detail : "执行中",
                         page->popup_cancel_pending ? "等待取消完成" : "等待后台完成");
                settings_popup_update_running(page, label, running);
            }
        }
        if (!modal_active) {
            v5_settings_page_set_status_text(page, 88, 204, 255, "%s: 执行中", label);
        }
    } else if (status.ok) {
        if ((status.restart_required || status.restart_deferred) &&
            page->popup_action[0] &&
            strcmp(page->popup_action, status.action) == 0 &&
            (!page->popup_run_id[0] || strcmp(page->popup_run_id, status.run_id) == 0)) {
            v5_settings_page_mark_restart_pending(page);
        }
        if (page->popup_active && !page->popup_final &&
            (!page->popup_action[0] || strcmp(page->popup_action, status.action) == 0) &&
            (!page->popup_run_id[0] || strcmp(page->popup_run_id, status.run_id) == 0)) {
            settings_popup_update_final(page, label, 1, status.code, detail);
        }
        if (!modal_active) {
            settings_refresh_axis_table_once(page, &status);
            if (strcmp(status.action, "device_dna_register") == 0) {
                v5_settings_page_refresh_machine_code_label(page);
            }
            v5_settings_page_set_status_text(page, 42, 221, 128, "%s: 完成 %s", label, status.code[0] ? status.code : detail);
        }
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
        if (!modal_active) {
            v5_settings_page_set_status_text(page, 245, 214, 82, "%s: %s", label, detail[0] ? detail : "未完成");
        }
    }
}
