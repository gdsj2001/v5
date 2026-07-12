#include "v5_main_page.h"

#include "v5_command_gate_ipc.h"
#include "v5_button_visuals.h"
#include "v5_native_wcs_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_layout_icons.h"
#include "v5_lvgl_clock.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "v5_main_page_internal.h"
#include "v5_popup_layout.h"

void v5_main_page_internal_show_power_on_home_popup(V5MainPage *page, int status_known);

int v5_main_page_internal_action_needs_native_readback_refresh(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_START:
    case V5_MAIN_PAGE_ACTION_PAUSE:
    case V5_MAIN_PAGE_ACTION_RESUME:
    case V5_MAIN_PAGE_ACTION_HOME:
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
    case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
    case V5_MAIN_PAGE_ACTION_RTCP_TOGGLE:
    case V5_MAIN_PAGE_ACTION_JOG_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_MINUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS:
    case V5_MAIN_PAGE_ACTION_FIRST_POINT:
        return 1;
    default:
        return 0;
    }
}

int v5_main_page_internal_action_requires_power_on_home(
    const V5MainPage *page,
    V5MainPageActionKind action)
{
    if (!page) {
        return 0;
    }
    switch (action) {
    case V5_MAIN_PAGE_ACTION_START:
    case V5_MAIN_PAGE_ACTION_RESUME:
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
    case V5_MAIN_PAGE_ACTION_JOG_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_MINUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS:
    case V5_MAIN_PAGE_ACTION_FIRST_POINT:
        return 1;
    case V5_MAIN_PAGE_ACTION_PAUSE:
        return v5_native_readback_interpreter_known(&page->native_readback) &&
            page->native_readback.interpreter_paused;
    case V5_MAIN_PAGE_ACTION_HOME:
        return page->selection.space != V5_MAIN_PAGE_SELECT_MCS ||
            !page->selection.all_axes;
    default:
        return 0;
    }
}

void v5_main_page_internal_block_action_for_power_on_home(
    V5MainPage *page,
    V5MainPageActionKind action,
    V5MainPageActionReport *report)
{
    int status_known;
    if (!page || !report) {
        return;
    }
    status_known = v5_native_readback_all_homed_known(&page->native_readback);
    memset(report, 0, sizeof(*report));
    report->action = action;
    report->local_only = 1;
    report->send_status = V5_COMMAND_GATE_SEND_INVALID;
    report->request.kind = V5_COMMAND_UI_LOCAL;
    report->command.kind = V5_COMMAND_UI_LOCAL;
    report->command.name = "power_on_home_precondition";
    report->command.owner = "native_home_precondition";
    report->command.accepted = 0;
    snprintf(
        report->readback_code,
        sizeof(report->readback_code),
        "%s",
        status_known ? "POWER_ON_HOME_REQUIRED" : "POWER_ON_HOME_STATUS_UNAVAILABLE");
    v5_main_page_internal_show_power_on_home_popup(page, status_known);
    page->last_action = *report;
}

static void button_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;

    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < page->button_count; ++i) {
        if (page->buttons[i] == target) {
            V5MainPageActionReport report;
            int ok;
            v5_button_visual_release_now(target);
            ok = v5_main_page_trigger_action(page, page->button_actions[i], &report);
            v5_main_page_internal_log_button_event(page->button_actions[i], ok, ok ? &report : 0);
            return;
        }
    }
}

void v5_main_page_internal_make_button_rgb(V5MainPage *page, int x, int y, int w, int h, V5MainPageActionKind action, const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *button;
    lv_obj_t *label;
    if (!page || page->button_count >= V5_MAIN_PAGE_BUTTON_COUNT) {
        return;
    }
    button = lv_btn_create(page->root);
    v5_main_page_internal_clear_obj_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, v5_main_page_internal_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, v5_main_page_internal_rgb(76, 119, 146), 0);
    v5_button_visual_bind(button);
    if (action == V5_MAIN_PAGE_ACTION_JOG_PLUS || action == V5_MAIN_PAGE_ACTION_JOG_MINUS) {
        lv_obj_add_event_cb(button, v5_main_page_internal_jog_button_event_cb, LV_EVENT_PRESSED, page);
        lv_obj_add_event_cb(button, v5_main_page_internal_jog_button_event_cb, LV_EVENT_PRESSING, page);
        lv_obj_add_event_cb(button, v5_main_page_internal_jog_button_event_cb, LV_EVENT_RELEASED, page);
        lv_obj_add_event_cb(button, v5_main_page_internal_jog_button_event_cb, LV_EVENT_PRESS_LOST, page);
        lv_obj_add_event_cb(button, v5_main_page_internal_jog_button_event_cb, LV_EVENT_CANCEL, page);
    } else {
        lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, page);
    }

    label = lv_label_create(button);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : v5_main_page_action_label(action));
    if (x >= 920) {
        lv_obj_set_size(label, w - 38, h > 4 ? h - 4 : h);
        lv_obj_set_pos(label, 36, h > 24 ? (h - 22) / 2 : 1);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        v5_layout_add_button_icon(button, action, w, h, 1);
    } else if (action == V5_MAIN_PAGE_ACTION_NAV_NETWORK) {
        lv_label_set_text(label, "");
        lv_obj_set_size(label, w, h);
        lv_obj_set_pos(label, 0, 0);
        v5_layout_add_button_icon(button, action, w, h, 0);
    } else {
        lv_obj_set_size(label, w, h > 4 ? h - 4 : h);
        lv_obj_set_pos(label, 0, h > 24 ? (h - 22) / 2 : 1);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_set_style_text_color(label, v5_main_page_internal_rgb(238, 245, 248), 0);
    lv_obj_set_style_text_color(label, v5_main_page_internal_rgb(16, 20, 24), LV_STATE_PRESSED);

    page->buttons[page->button_count] = button;
    page->button_labels[page->button_count] = label;
    page->button_actions[page->button_count] = action;
    if (action == V5_MAIN_PAGE_ACTION_ESTOP_FORCE) {
        v5_ui_first_frame_guard_register_safety_button(button);
    }
    page->button_count += 1u;
}

static void power_on_home_popup_hide(V5MainPage *page)
{
    if (!page || !page->power_on_home_popup) {
        return;
    }
    if (!v5_ui_first_frame_guard_dismiss_overlay(
            &page->power_on_home_popup_frame_guard,
            page->power_on_home_popup)) {
        return;
    }
    if (!page->selection.all_axes) {
        v5_main_page_internal_reset_selection_idle_timer(page);
    }
}

static void power_on_home_popup_close_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_indev_t *indev;
    if (!page || lv_event_get_code(event) != LV_EVENT_RELEASED) {
        return;
    }
    v5_button_visual_release_now(lv_event_get_target(event));
    indev = lv_indev_get_act();
    if (indev) {
        lv_indev_wait_release(indev);
    }
    power_on_home_popup_hide(page);
}

void v5_main_page_internal_create_power_on_home_popup(V5MainPage *page)
{
    V5PopupLayoutConfig config = {0};
    V5PopupLayoutObjects popup = {0};
    if (!page || !page->root) {
        return;
    }
    config.title = "开机回零前置条件";
    config.message = "";
    config.message_color_rgb = 0xFF6068U;
    config.confirm_text = "确认继续";
    config.confirm_enabled = 0;
    config.close_text = "关闭";
    config.close_enabled = 1;
    config.close_cb = power_on_home_popup_close_cb;
    config.close_user_data = page;
    if (!v5_popup_layout_create(lv_obj_get_screen(page->root), &config, &popup)) {
        return;
    }
    page->power_on_home_popup = popup.overlay;
    page->power_on_home_popup_message = popup.message;
    page->power_on_home_popup_confirm = popup.confirm;
    page->power_on_home_popup_close = popup.close;
}

void v5_main_page_internal_show_power_on_home_popup(V5MainPage *page, int status_known)
{
    V5NativeOperatorErrorStatus operator_message;
    const char *alias_code = status_known ?
        "POWER_ON_HOME_REQUIRED" : "POWER_ON_HOME_STATUS_UNAVAILABLE";
    char message[384];
    int opening;
    if (!page || !page->power_on_home_popup || !page->power_on_home_popup_message) {
        return;
    }
    opening = lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN);
    if (opening) {
        if (!v5_ui_first_frame_guard_begin_overlay(&page->power_on_home_popup_frame_guard)) {
            return;
        }
    }
    if (!v5_native_operator_error_status_from_alias(alias_code, &operator_message)) {
        if (opening) {
            v5_ui_first_frame_guard_restore(&page->power_on_home_popup_frame_guard);
        }
        return;
    }
    snprintf(
        message,
        sizeof(message),
        "提示: %s\n\n原因: %s\n\n下一步: %s",
        operator_message.title_cn,
        operator_message.reason_cn,
        operator_message.next_cn);
    (void)v5_ui_first_frame_guard_set_label_text(
        &page->power_on_home_popup_frame_guard,
        page->power_on_home_popup_message,
        message);
    if (opening) {
        (void)v5_ui_first_frame_guard_present_overlay(
            &page->power_on_home_popup_frame_guard,
            page->power_on_home_popup);
    }
}

int v5_main_page_internal_view_action_matches_plane(V5MainPageActionKind action, V5ToolpathDisplayPlane plane)
{
    return (action == V5_MAIN_PAGE_ACTION_VIEW_XY && plane == V5_TOOLPATH_DISPLAY_XY) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_XZ && plane == V5_TOOLPATH_DISPLAY_XZ) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_YZ && plane == V5_TOOLPATH_DISPLAY_YZ) ||
        (action == V5_MAIN_PAGE_ACTION_VIEW_3D && plane == V5_TOOLPATH_DISPLAY_3D);
}

int v5_main_page_internal_is_view_action(V5MainPageActionKind action)
{
    return action == V5_MAIN_PAGE_ACTION_VIEW_XY ||
        action == V5_MAIN_PAGE_ACTION_VIEW_XZ ||
        action == V5_MAIN_PAGE_ACTION_VIEW_YZ ||
        action == V5_MAIN_PAGE_ACTION_VIEW_3D;
}
