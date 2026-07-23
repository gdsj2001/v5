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

static int trigger_jog_for_captured_axis(
    V5MainPage *page,
    V5MainPageActionKind action,
    int keepalive,
    V5MainPageActionReport *report);

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

void v5_main_page_internal_log_button_event(V5MainPageActionKind action, int ok, const V5MainPageActionReport *report)
{
    const char *path = "/run/8ax_v5_product_ui/ui_events.jsonl";
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen(path, "ab");
    if (!fp) {
        return;
    }
    int layout_only = report && report->command.name && strcmp(report->command.name, "layout_only") == 0;
    int implemented = ok && report && !layout_only && (report->prepared || report->local_only);
    fprintf(fp, "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"button_clicked\",\"action\":", v5_main_page_internal_monotonic_seconds());
    write_json_text(fp, v5_main_page_action_label(action));
    fprintf(fp, ",\"ok\":%s,\"implemented\":%s,\"layout_only\":%s", ok ? "true" : "false", implemented ? "true" : "false", layout_only ? "true" : "false");
    if (report) {
        fprintf(fp, ",\"prepared\":%s,\"local_only\":%s,\"executed\":%s,\"send_status\":%d,\"machine_on_requested\":%s,\"machine_on_status\":%d,\"request_kind\":%d,\"command_kind\":%d,\"axis_value\":%.3f,\"command_name\":", report->prepared ? "true" : "false", report->local_only ? "true" : "false", report->executed ? "true" : "false", report->send_status, report->machine_on_requested ? "true" : "false", report->machine_on_status, (int)report->request.kind, (int)report->command.kind, report->request.axis_value);
        write_json_text(fp, report->command.name);
        fprintf(fp, ",\"command_owner\":");
        write_json_text(fp, report->command.owner);
        fprintf(fp, ",\"command_line\":");
        write_json_text(fp, report->command_line);
        fprintf(fp, ",\"readback_code\":");
        write_json_text(fp, report->readback_code);
    }
    fprintf(fp, "}\n");
    fclose(fp);
}


lv_color_t v5_main_page_internal_main_coordinate_digit_color(const V5MainPage *page, unsigned int axis, int is_wcs)
{
    int selected = 0;
    if (page && axis < V5_MAIN_PAGE_AXIS_COUNT) {
        if (is_wcs) {
            selected = page->selection.space == V5_MAIN_PAGE_SELECT_WCS &&
                       !page->selection.all_axes &&
                       page->selection.axis == page->wcs_targets[axis].axis;
        } else {
            selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS &&
                       !page->selection.all_axes &&
                       page->selection.axis == page->mcs_targets[axis].axis;
        }
    }
    if (selected) {
        return v5_main_page_internal_rgb(245, 214, 82);
    }
    return is_wcs ? v5_main_page_internal_rgb(68, 221, 144) : v5_main_page_internal_rgb(88, 204, 255);
}

static void refresh_main_coordinate_digits(V5MainPage *page)
{
    unsigned int i;
    if (!page || !page->coordinate_digits.canvas) {
        return;
    }
    for (i = 0U; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        const char *mcs_text = page->coordinate_digits.value_valid[0][i]
            ? page->coordinate_digits.value_text[0][i]
            : (page->mcs_labels[i] ? lv_label_get_text(page->mcs_labels[i]) : "");
        const char *cmd_text = page->coordinate_digits.value_valid[1][i]
            ? page->coordinate_digits.value_text[1][i]
            : (page->cmd_labels[i] ? lv_label_get_text(page->cmd_labels[i]) : "");
        if (mcs_text) {
            v5_coordinate_digits_set_value(
                &page->coordinate_digits,
                0U,
                i,
                mcs_text,
                v5_main_page_internal_main_coordinate_digit_color(page, i, 0));
        }
        if (cmd_text) {
            v5_coordinate_digits_set_value(
                &page->coordinate_digits,
                1U,
                i,
                cmd_text,
                v5_main_page_internal_main_coordinate_digit_color(page, i, 1));
        }
    }
}

void v5_main_page_internal_update_coordinate_selection_style(V5MainPage *page)
{
    unsigned int i;
    if (!page) {
        return;
    }
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        int mcs_selected = page->selection.space == V5_MAIN_PAGE_SELECT_MCS && !page->selection.all_axes && page->selection.axis == page->mcs_targets[i].axis;
        int wcs_selected = page->selection.space == V5_MAIN_PAGE_SELECT_WCS && !page->selection.all_axes && page->selection.axis == page->wcs_targets[i].axis;
        if (page->mcs_labels[i]) {
            lv_obj_set_style_bg_opa(page->mcs_labels[i], mcs_selected ? LV_OPA_40 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(page->mcs_labels[i], v5_main_page_internal_rgb(245, 214, 82), 0);
        }
        if (page->cmd_labels[i]) {
            lv_obj_set_style_bg_opa(page->cmd_labels[i], wcs_selected ? LV_OPA_40 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(page->cmd_labels[i], v5_main_page_internal_rgb(245, 214, 82), 0);
        }
    }
    refresh_main_coordinate_digits(page);
}

void v5_main_page_internal_refresh_coordinate_selection_now(V5MainPage *page)
{
    v5_main_page_internal_update_coordinate_selection_style(page);
    if (page && page->coordinate_digits.canvas) {
        lv_obj_invalidate(page->coordinate_digits.canvas);
    }
}

static void log_coordinate_select_event(const V5MainPage *page)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/ui_events.jsonl", "ab");
    if (!fp || !page) {
        if (fp) fclose(fp);
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.ui_event.v1\",\"source\":\"v5_lvgl_shell\",\"time_monotonic_s\":%.6f,\"event\":\"coordinate_selected\",\"space\":\"%s\",\"axis\":\"%c\",\"all_axes\":%s}\n",
            v5_main_page_internal_monotonic_seconds(),
            page->selection.space == V5_MAIN_PAGE_SELECT_WCS ? "wcs" : page->selection.space == V5_MAIN_PAGE_SELECT_MCS ? "mcs" : "none",
            page->selection.axis ? page->selection.axis : '-',
            page->selection.all_axes ? "true" : "false");
    fclose(fp);
}

static void coordinate_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    unsigned int i;
    if (!page || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    for (i = 0; i < V5_MAIN_PAGE_AXIS_COUNT; ++i) {
        if (page->mcs_labels[i] == target) {
            (void)v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, page->mcs_targets[i].axis);
            log_coordinate_select_event(page);
            return;
        }
        if (page->cmd_labels[i] == target) {
            (void)v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_WCS, page->wcs_targets[i].axis);
            log_coordinate_select_event(page);
            return;
        }
    }
}

void v5_main_page_internal_make_coordinate_value_clickable(V5MainPage *page, lv_obj_t *label)
{
    if (!page || !label) {
        return;
    }
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(label, v5_main_page_internal_rgb(245, 214, 82), 0);
    lv_obj_add_event_cb(label, coordinate_event_cb, LV_EVENT_CLICKED, page);
}

static void trigger_value_reset_action(V5MainPage *page, int spindle)
{
    V5MainPageActionReport report;
    int ok;
    if (!page) {
        return;
    }
    ok = v5_main_page_trigger_override(page, spindle, 100, &report);
    v5_main_page_internal_log_button_event(
        spindle ? V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_SET : V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET,
        ok,
        ok ? &report : 0);
}

void v5_main_page_internal_spindle_override_reset_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        trigger_value_reset_action((V5MainPage *)lv_event_get_user_data(event), 1);
    }
}

void v5_main_page_internal_feed_override_reset_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        trigger_value_reset_action((V5MainPage *)lv_event_get_user_data(event), 0);
    }
}

lv_obj_t *v5_main_page_internal_make_override_reset_hit(V5MainPage *page, int x, int y, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *hit;
    if (!page || !page->root || !cb) {
        return 0;
    }
    hit = lv_obj_create(page->root);
    v5_main_page_internal_clear_obj_style(hit);
    lv_obj_set_pos(hit, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_size(hit, (lv_coord_t)w, (lv_coord_t)h);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, page);
    lv_obj_move_foreground(hit);
    return hit;
}

static void main_program_edit_hit_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    lv_indev_t *indev;
    int changed = 0;
    uint32_t now;
    if (!page) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        indev = lv_indev_get_act();
        if (indev) {
            lv_indev_get_point(indev, &point);
            (void)v5_main_page_internal_main_page_handle_program_preview_touch(page, &point, 1, &changed);
            if (changed) {
                lv_obj_invalidate(page->program_edit_hit_area);
            }
        }
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        (void)v5_main_page_internal_main_page_handle_program_preview_touch(page, 0, 0, 0);
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    if (page->program_preview_dragged) {
        page->program_preview_dragged = 0;
        page->program_edit_last_click_tick = 0U;
        return;
    }
    now = lv_tick_get();
    if (page->program_edit_last_click_tick != 0U &&
        (uint32_t)(now - page->program_edit_last_click_tick) <= 550U) {
        V5MainPageActionReport report;
        int ok;
        page->program_edit_last_click_tick = 0U;
        ok = v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, &report);
        v5_main_page_internal_log_button_event(V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, ok, ok ? &report : 0);
        return;
    }
    page->program_edit_last_click_tick = now;
}

void v5_main_page_internal_create_main_program_edit_hit_area(V5MainPage *page)
{
    lv_obj_t *hit;
    if (!page || !page->root) {
        return;
    }
    hit = lv_obj_create(page->root);
    v5_main_page_internal_clear_obj_style(hit);
    lv_obj_set_pos(hit, 0, 441);
    lv_obj_set_size(hit, 560, 154);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hit, main_program_edit_hit_event_cb, LV_EVENT_PRESSED, page);
    lv_obj_add_event_cb(hit, main_program_edit_hit_event_cb, LV_EVENT_PRESSING, page);
    lv_obj_add_event_cb(hit, main_program_edit_hit_event_cb, LV_EVENT_RELEASED, page);
    lv_obj_add_event_cb(hit, main_program_edit_hit_event_cb, LV_EVENT_CLICKED, page);
    lv_obj_move_foreground(hit);
    page->program_edit_hit_area = hit;
}

void v5_main_page_internal_reset_selection_idle_timer(V5MainPage *page)
{
    if (!page || !page->root ||
        lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN) ||
        !page->selection_idle_timer || page->selection.all_axes) {
        return;
    }
    lv_timer_reset(page->selection_idle_timer);
    lv_timer_resume(page->selection_idle_timer);
}

void v5_main_page_set_page_visible(V5MainPage *page, int visible)
{
    int normalized;
    if (!page) {
        return;
    }
    normalized = visible ? 1 : 0;
    if (page->page_visible != normalized) {
        page->page_visible = normalized;
        (void)v5_main_page_internal_publish_program_scene_request(page);
        page->toolpath_request_last_send_tick = lv_tick_get();
        page->toolpath_request_retry_count = 0U;
    }
    if (page->selection_idle_timer) {
        lv_timer_pause(page->selection_idle_timer);
    }
    if (!normalized && (page->jog_pressed_button || page->jog_continuous_active)) {
        if (page->jog_pressed_axis) {
            V5MainPageActionReport report;
            int ok = trigger_jog_for_captured_axis(
                page,
                V5_MAIN_PAGE_ACTION_JOG_STOP,
                0,
                &report);
            v5_main_page_internal_log_button_event(
                V5_MAIN_PAGE_ACTION_JOG_STOP,
                ok,
                ok ? &report : 0);
        }
        if (page->jog_pressed_button) {
            lv_obj_clear_state(page->jog_pressed_button, LV_STATE_PRESSED);
        }
        page->jog_pressed_button = 0;
        page->jog_pressed_axis = '\0';
        page->jog_continuous_active = 0;
        page->jog_keepalive_last_tick = 0U;
    }
    if (normalized) {
        v5_main_page_internal_reset_selection_idle_timer(page);
    }
}

void v5_main_page_internal_selection_idle_timer_cb(lv_timer_t *timer)
{
    V5MainPage *page = timer ? (V5MainPage *)timer->user_data : 0;
    if (!page || !page->root ||
        lv_obj_has_flag(page->root, LV_OBJ_FLAG_HIDDEN)) {
        if (timer) {
            lv_timer_pause(timer);
        }
        return;
    }
    if (page->power_on_home_popup &&
        !lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN)) {
        v5_main_page_internal_reset_selection_idle_timer(page);
        return;
    }
    if (page->jog_pressed_button || page->jog_continuous_active) {
        v5_main_page_internal_reset_selection_idle_timer(page);
        return;
    }
    v5_main_page_select_all_axes(page);
    log_coordinate_select_event(page);
}

static int trigger_jog_for_captured_axis(
    V5MainPage *page,
    V5MainPageActionKind action,
    int keepalive,
    V5MainPageActionReport *report)
{
    V5MainPageSelection saved;
    int ok;
    if (!page || !page->jog_pressed_axis) {
        return 0;
    }
    saved = page->selection;
    page->selection.space = page->jog_pressed_space;
    page->selection.axis = page->jog_pressed_axis;
    page->selection.all_axes = 0;
    ok = keepalive
        ? v5_main_page_internal_trigger_jog_keepalive(page, action, report)
        : v5_main_page_trigger_action(page, action, report);
    page->selection = saved;
    return ok;
}

static void refresh_jog_keepalive(V5MainPage *page)
{
    V5MainPageActionReport report;
    V5MainPageActionKind action;
    int ok;
    if (!page || !page->jog_pressed_button ||
        lv_tick_elaps(page->jog_keepalive_last_tick) < V5_MAIN_PAGE_JOG_KEEPALIVE_MS) {
        return;
    }
    action = page->jog_pressed_positive
        ? V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS
        : V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS;
    ok = trigger_jog_for_captured_axis(page, action, 1, &report);
    page->jog_continuous_active = ok && report.executed;
    page->jog_keepalive_last_tick = lv_tick_get();
    v5_main_page_internal_log_button_event(action, ok, ok ? &report : 0);
}

static void finish_jog_press(V5MainPage *page, lv_obj_t *button)
{
    V5MainPageActionReport report;
    int ok;
    if (!page || page->jog_pressed_button != button) {
        return;
    }
    ok = trigger_jog_for_captured_axis(
        page,
        V5_MAIN_PAGE_ACTION_JOG_STOP,
        0,
        &report);
    v5_main_page_internal_log_button_event(
        V5_MAIN_PAGE_ACTION_JOG_STOP,
        ok,
        ok ? &report : 0);
    page->jog_pressed_button = 0;
    page->jog_pressed_axis = '\0';
    page->jog_continuous_active = 0;
    page->jog_keepalive_last_tick = 0U;
    v5_main_page_internal_reset_selection_idle_timer(page);
}

void v5_main_page_internal_jog_button_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    lv_obj_t *button = lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    unsigned int i;
    if (!page) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        if (page->selection.all_axes ||
            (page->selection.space != V5_MAIN_PAGE_SELECT_MCS &&
             page->selection.space != V5_MAIN_PAGE_SELECT_WCS)) {
            return;
        }
        for (i = 0U; i < page->button_count; ++i) {
            if (page->buttons[i] == button) {
                V5MainPageActionReport report;
                V5MainPageActionKind action;
                int ok;
                page->jog_pressed_button = button;
                page->jog_pressed_space = page->selection.space;
                page->jog_pressed_axis = page->selection.axis;
                page->jog_pressed_positive = page->button_actions[i] == V5_MAIN_PAGE_ACTION_JOG_PLUS;
                page->jog_continuous_active = 0;
                if (page->selection_idle_timer) {
                    lv_timer_pause(page->selection_idle_timer);
                }
                action = page->jog_pressed_positive
                    ? V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS
                    : V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS;
                ok = trigger_jog_for_captured_axis(page, action, 0, &report);
                page->jog_continuous_active = ok && report.executed;
                page->jog_keepalive_last_tick = lv_tick_get();
                v5_main_page_internal_log_button_event(
                    action,
                    ok,
                    ok ? &report : 0);
                return;
            }
        }
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        refresh_jog_keepalive(page);
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_CANCEL) {
        v5_button_visual_release_now(button);
        finish_jog_press(page, button);
    }
}

void v5_main_page_internal_main_page_root_delete_event_cb(lv_event_t *event)
{
    V5MainPage *page = (V5MainPage *)lv_event_get_user_data(event);
    if (!page || lv_event_get_code(event) != LV_EVENT_DELETE) {
        return;
    }
    if (page->jog_pressed_button && page->jog_pressed_axis) {
        V5MainPageActionReport report;
        (void)trigger_jog_for_captured_axis(
            page,
            V5_MAIN_PAGE_ACTION_JOG_STOP,
            0,
            &report);
    }
    if (page->selection_idle_timer) {
        lv_timer_del(page->selection_idle_timer);
        page->selection_idle_timer = 0;
    }
    page->jog_pressed_button = 0;
    page->jog_continuous_active = 0;
    v5_ui_first_frame_guard_clear(&page->power_on_home_popup_frame_guard);
}
