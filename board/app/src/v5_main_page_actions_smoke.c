#include "v5_main_page.h"
#include "v5_motion_model_registry.h"
#include "v5_main_page_internal.h"
#include "v5_button_visuals.h"
#include "v5_command_gate_ipc.h"
#include "v5_lvgl_headless.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int same_text(const char *left, const char *right)
{
    return left && right && strcmp(left, right) == 0;
}

static void capture_nav_action(void *user_data, V5MainPageActionKind action)
{
    V5MainPageActionKind *out = (V5MainPageActionKind *)user_data;
    if (out) {
        *out = action;
    }
}

static int expect_local(V5MainPage *page, V5MainPageActionKind action, const char *name)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || !report.local_only) {
        return 0;
    }
    if (report.request.kind != V5_COMMAND_UI_LOCAL || report.command.kind != V5_COMMAND_UI_LOCAL) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, "ui_local") && report.command.accepted;
}

static int expect_command_line(V5MainPage *page, V5MainPageActionKind action, const char *name, const char *owner, const char *line)
{
    V5MainPageActionReport report;
    (void)line;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (report.command_line[0]) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, owner) && report.command.accepted;
}

static int expect_command(V5MainPage *page, V5MainPageActionKind action, const char *name, const char *owner)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (report.command_line[0]) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, owner) && report.command.accepted;
}


static int expect_home_native_gate(V5MainPage *page)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    return same_text(report.command.name, "home") &&
           same_text(report.command.owner, "native_home_mode_gate") &&
           report.command.accepted &&
           !report.command_line[0] &&
           report.send_status == 0;
}

static int expect_axis_zero_position(V5MainPage *page, char axis, const char *space)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    return same_text(report.command.name, "axis_zero_position") &&
           same_text(report.command.owner, "native_axis_zero_position") &&
           report.command.accepted &&
           report.request.kind == V5_COMMAND_AXIS_ZERO_POSITION &&
           report.request.text_value && report.request.text_value[0] == axis &&
           report.request.text_value[1] == '\0' &&
           same_text(report.request.mode_value, space) &&
           !report.command_line[0] &&
           report.send_status == 0;
}


static int expect_first_point(V5MainPage *page, const char *path)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_FIRST_POINT, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_FIRST_POINT || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (!same_text(report.command.name, "first_point") || !same_text(report.command.owner, "native_first_point")) {
        return 0;
    }
    if (!same_text(report.request.text_value, path) || !same_text(report.request.mode_value, "AC_XY_Z")) {
        return 0;
    }
    if (!report.request.secondary_text_value || strlen(report.request.secondary_text_value) != 64U ||
        report.request.index_value <= 0 || report.request.axis_mask !=
            (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK | V5_COMMAND_AXIS_Z_MASK | V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK)) {
        return 0;
    }
    if (report.request.point_axis[0] != 1.0 || report.request.point_axis[1] != 2.0 ||
        report.request.point_axis[2] != 3.0 || report.request.point_axis[3] != 4.0 ||
        report.request.point_axis[4] != 5.0) {
        return 0;
    }
    return !report.command_line[0];
}

static const char *button_text_for_action(V5MainPage *page, V5MainPageActionKind action)
{
    unsigned int i;
    if (!page) {
        return "";
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == action && page->button_labels[i]) {
            return lv_label_get_text(page->button_labels[i]);
        }
    }
    return "";
}

static lv_obj_t *button_for_action(V5MainPage *page, V5MainPageActionKind action)
{
    unsigned int i;
    if (!page) {
        return 0;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == action) {
            return page->buttons[i];
        }
    }
    return 0;
}

static int button_bg_matches(V5MainPage *page, V5MainPageActionKind action, int r, int g, int b)
{
    lv_obj_t *button = button_for_action(page, action);
    lv_color_t actual;
    lv_color_t expected;
    if (!button) {
        return 0;
    }
    actual = lv_obj_get_style_bg_color(button, LV_PART_MAIN);
    expected = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return lv_color_to32(actual) == lv_color_to32(expected);
}

static int program_row_bg_matches(V5MainPage *page, unsigned int row, int r, int g, int b)
{
    lv_color_t actual;
    lv_color_t expected;
    if (!page || row >= 4U || !page->program_line_bg[row]) {
        return 0;
    }
    actual = lv_obj_get_style_bg_color(page->program_line_bg[row], LV_PART_MAIN);
    expected = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return lv_color_to32(actual) == lv_color_to32(expected);
}

static int program_row_text_has(V5MainPage *page, unsigned int row, const char *needle)
{
    const char *text;
    if (!page || row >= 4U || !page->program_line_labels[row] || !needle) {
        return 0;
    }
    text = lv_label_get_text(page->program_line_labels[row]);
    return text && strstr(text, needle) != 0;
}

static int button_pressed_state_clears_on_click(V5MainPage *page, V5MainPageActionKind action, int r, int g, int b)
{
    lv_obj_t *button = button_for_action(page, action);
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    if (!lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_CLICKED, 0);
    return !lv_obj_has_state(button, LV_STATE_PRESSED) && button_bg_matches(page, action, r, g, b);
}

static int button_pressed_state_clears_on_release(V5MainPage *page, V5MainPageActionKind action)
{
    lv_obj_t *button = button_for_action(page, action);
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    v5_lvgl_headless_reset_flush_count();
    lv_event_send(button, LV_EVENT_RELEASED, 0);
    return !lv_obj_has_state(button, LV_STATE_PRESSED) &&
           v5_lvgl_headless_flush_count() > 0U;
}

static int button_visual_state_cycle(V5MainPage *page, V5MainPageActionKind action)
{
    lv_obj_t *button = button_for_action(page, action);
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    lv_event_send(button, LV_EVENT_PRESS_LOST, 0);
    if (lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    lv_event_send(button, LV_EVENT_CANCEL, 0);
    if (lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    v5_button_visual_set_transaction_active(button, 1);
    if (!lv_obj_has_state(button, LV_STATE_USER_1) ||
        !button_bg_matches(page, action, 29, 151, 104)) {
        return 0;
    }
    v5_button_visual_set_transaction_active(button, 0);
    return !lv_obj_has_state(button, LV_STATE_USER_1);
}

static int exercise_jog_press_timing(V5MainPage *page)
{
    lv_obj_t *button;
    if (!page || !v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, 'X')) {
        return 0;
    }
    button = button_for_action(page, V5_MAIN_PAGE_ACTION_JOG_PLUS);
    if (!button) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(499U);
    (void)lv_timer_handler();
    lv_event_send(button, LV_EVENT_RELEASED, 0);
    if (page->last_action.request.kind != V5_COMMAND_JOG_INCREMENT ||
        page->last_action.request.increment_value != page->jog_step ||
        page->last_action.request.axis_value <= 0.0 || page->jog_pressed_button) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(500U);
    (void)lv_timer_handler();
    if (page->last_action.request.kind != V5_COMMAND_JOG_CONTINUOUS ||
        !page->jog_long_press_elapsed) {
        return 0;
    }
    lv_tick_inc(V5_MAIN_PAGE_JOG_KEEPALIVE_MS);
    lv_event_send(button, LV_EVENT_PRESSING, 0);
    if (page->last_action.request.kind != V5_COMMAND_JOG_CONTINUOUS ||
        page->jog_keepalive_last_tick == 0U) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESS_LOST, 0);
    if (page->jog_pressed_button || page->jog_long_press_elapsed ||
        page->last_action.request.kind != V5_COMMAND_JOG_STOP) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(V5_MAIN_PAGE_JOG_HOLD_MS);
    (void)lv_timer_handler();
    lv_event_send(button, LV_EVENT_CANCEL, 0);
    return !page->jog_pressed_button && !page->jog_long_press_elapsed &&
           page->last_action.request.kind == V5_COMMAND_JOG_STOP;
}

static int exercise_axis_selection_timeout(V5MainPage *page)
{
    if (!page || !v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, 'Y')) {
        return 0;
    }
    lv_tick_inc(2500U);
    (void)lv_timer_handler();
    if (page->selection.all_axes || !expect_local(page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "jog_step")) {
        return 0;
    }
    lv_tick_inc(1000U);
    (void)lv_timer_handler();
    if (page->selection.all_axes) {
        return 0;
    }
    lv_tick_inc(2100U);
    (void)lv_timer_handler();
    return page->selection.all_axes && page->selection.space == V5_MAIN_PAGE_SELECT_MCS;
}

static void refresh_estop_active(void *user_data, V5MainPageActionKind action)
{
    V5MainPage *page = (V5MainPage *)user_data;
    V5NativeReadback readback;
    if (!page || action != V5_MAIN_PAGE_ACTION_ESTOP_FORCE) {
        return;
    }
    v5_native_readback_init(&readback);
    v5_native_readback_set_safety_estop(&readback, 1);
    v5_native_readback_set_machine_enabled(&readback, 0);
    v5_main_page_set_native_readback(page, &readback);
}

static int write_first_point_program(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G21\nG90\nG1 X1 Y2 Z3 A4 C5\n", fp);
    return fclose(fp) == 0;
}

static int write_preview_scroll_program(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G21\nG90\nG1 X1\nG1 X2\nG1 X3\nG1 X4\nG1 X5\nG1 X6\n", fp);
    return fclose(fp) == 0;
}

static int expect_missing_gate(V5MainPage *page, V5MainPageActionKind action)
{
    V5MainPageActionReport report;
    if (v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    return page->last_action.action == action && !page->last_action.prepared;
}

static int expect_power_on_home_block(
    V5MainPage *page,
    V5MainPageActionKind action,
    const char *readback_code)
{
    V5MainPageActionReport report;
    const char *expected_title;
    const char *message;
    if (!page || !readback_code || !v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || report.prepared || !report.local_only || report.executed ||
        report.send_status != V5_COMMAND_GATE_SEND_INVALID ||
        report.request.kind != V5_COMMAND_UI_LOCAL ||
        !same_text(report.command.name, "power_on_home_precondition") ||
        !same_text(report.command.owner, "native_home_precondition") ||
        report.command.accepted || !same_text(report.readback_code, readback_code)) {
        return 0;
    }
    if (!page->power_on_home_popup || !page->power_on_home_popup_message ||
        !page->power_on_home_popup_confirm ||
        !lv_obj_has_state(page->power_on_home_popup_confirm, LV_STATE_DISABLED) ||
        !page->power_on_home_popup_close ||
        lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    expected_title = same_text(readback_code, "POWER_ON_HOME_REQUIRED") ?
        "需要回零" : "回零状态不可用";
    message = lv_label_get_text(page->power_on_home_popup_message);
    if (!message || strstr(message, readback_code) || !strstr(message, "提示:") ||
        !strstr(message, expected_title) || !strstr(message, "原因:") ||
        !strstr(message, "下一步:") || strstr(message, "LinuxCNC") ||
        strstr(message, "linuxcnc")) {
        return 0;
    }
    lv_event_send(page->power_on_home_popup_close, LV_EVENT_RELEASED, 0);
    return lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN);
}

static int expect_estop_home_block(V5MainPage *page)
{
    V5MainPageActionReport report;
    const char *message;
    if (!page || !v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || report.prepared || !report.local_only ||
        report.executed || report.send_status != V5_COMMAND_GATE_SEND_INVALID ||
        report.request.kind != V5_COMMAND_UI_LOCAL ||
        !same_text(report.command.name, "home_safety_precondition") ||
        !same_text(report.command.owner, "native_home_precondition") ||
        report.command.accepted || !same_text(report.readback_code, "HOME_PRECONDITION_ESTOP")) {
        return 0;
    }
    if (!page->power_on_home_popup || !page->power_on_home_popup_message ||
        lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    message = lv_label_get_text(page->power_on_home_popup_message);
    if (!message || !strstr(message, "提示: 请先取消急停") ||
        !strstr(message, "原因: 机器当前处于急停状态，不能执行回零") ||
        !strstr(message, "下一步:") || !strstr(message, "取消急停") ||
        strstr(message, "HOME_PRECONDITION_ESTOP")) {
        return 0;
    }
    lv_event_send(page->power_on_home_popup_close, LV_EVENT_RELEASED, 0);
    return lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN);
}

int main(void)
{
    V5MainPage page;
    V5ProgramController controller;
    lv_obj_t *screen;

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    screen = lv_scr_act();
    if (!v5_main_page_create(&page, screen)) {
        return 2;
    }
    if (page.button_count != V5_MAIN_PAGE_BUTTON_COUNT) {
        return 3;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
        return 26;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL), "机械全轴")) {
        return 37;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
        return 33;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_START, 16, 48, 77) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_HOME, 42, 63, 85) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, 42, 63, 85) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS, 32, 52, 73)) {
        return 43;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G54, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 29, 151, 104) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_1, 29, 151, 104) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, 32, 52, 73)) {
        return 44;
    }
    if (!button_pressed_state_clears_on_click(&page, V5_MAIN_PAGE_ACTION_HOME, 42, 63, 85)) {
        return 50;
    }
    if (!button_pressed_state_clears_on_release(&page, V5_MAIN_PAGE_ACTION_HOME)) {
        return 65;
    }
    if (!button_visual_state_cycle(&page, V5_MAIN_PAGE_ACTION_HOME)) {
        return 67;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X') ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_HOME,
                "POWER_ON_HOME_REQUIRED") ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_START,
                "POWER_ON_HOME_REQUIRED")) {
            return 68;
        }
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X')) {
            return 71;
        }
        {
            V5MainPageActionReport modal_report;
            if (!v5_main_page_trigger_action(
                    &page,
                    V5_MAIN_PAGE_ACTION_HOME,
                    &modal_report) ||
                !same_text(modal_report.readback_code, "POWER_ON_HOME_REQUIRED")) {
                return 71;
            }
        }
        lv_tick_inc(3100U);
        (void)lv_timer_handler();
        if (lv_obj_has_flag(page.power_on_home_popup, LV_OBJ_FLAG_HIDDEN) ||
            page.selection.all_axes || page.selection.axis != 'X') {
            return 71;
        }
        lv_event_send(page.power_on_home_popup_close, LV_EVENT_RELEASED, 0);
        lv_tick_inc(3100U);
        (void)lv_timer_handler();
        if (!page.selection.all_axes || page.selection.space != V5_MAIN_PAGE_SELECT_MCS) {
            return 72;
        }
        v5_main_page_select_all_axes(&page);
        if (!expect_home_native_gate(&page)) {
            return 69;
        }
        v5_native_readback_set_unavailable(&readback, "home_status_unknown");
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X') ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_HOME,
                "POWER_ON_HOME_STATUS_UNAVAILABLE")) {
            return 70;
        }
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_select_all_axes(&page);
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_native_readback_set_safety_estop(&readback, 1);
        v5_native_readback_set_machine_enabled(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_select_all_axes(&page);
        if (!expect_estop_home_block(&page)) {
            return 73;
        }
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
    }
    v5_program_controller_init(&controller);
    v5_main_page_bind_program_controller(&page, &controller);

    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MAIN, "nav_main") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_SETTINGS, "nav_settings") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_PROGRAM, "nav_program") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MDI, "nav_mdi") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, "nav_mdi_edit") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all")) {
        return 4;
    }
    {
        V5MainPageActionKind nav_action = 0;
        if (!page.program_edit_hit_area) {
            return 51;
        }
        v5_main_page_set_navigation_callback(&page, capture_nav_action, &nav_action);
        lv_tick_inc(1);
        lv_event_send(page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
        if (nav_action != 0) {
            return 52;
        }
        lv_tick_inc(100);
        lv_event_send(page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
        if (nav_action != V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT) {
            return 53;
        }
        v5_main_page_set_navigation_callback(&page, 0, 0);
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "jog_step") || page.jog_step != 0.01) {
        return 5;
    }
    if (!exercise_jog_press_timing(&page) || !exercise_axis_selection_timeout(&page)) {
        return 66;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_1, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, 29, 151, 104)) {
        return 45;
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_VIEW_XZ, "view_xz") || page.view_plane != V5_TOOLPATH_DISPLAY_XZ) {
        return 6;
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_VIEW_3D, "view_3d") || page.view_plane != V5_TOOLPATH_DISPLAY_3D) {
        return 7;
    }

    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_PAUSE) ||
        !expect_home_native_gate(&page) ||
        !expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_force", "native_safety") ||
        !expect_command(&page, V5_MAIN_PAGE_ACTION_WCS_G55, "wcs_select", "native_linuxcncrsh")) {
        return 8;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_interpreter_paused(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_PAUSE, "pause", "native_linuxcncrsh", "Set Pause")) {
            return 22;
        }
        v5_native_readback_set_interpreter_paused(&readback, 1);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_PAUSE, "resume", "native_linuxcncrsh", "Set Resume")) {
            return 23;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
    }

    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_safety_estop(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "取消急停")) {
            return 27;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 24;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        v5_native_readback_init(&readback);
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "取消急停")) {
            return 29;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 30;
        }
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
            return 31;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_force", "native_safety")) {
            return 32;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_set_native_readback_refresh_callback(&page, refresh_estop_active, &page);
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 25;
        }
        v5_main_page_set_native_readback_refresh_callback(&page, 0, 0);
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
            return 28;
        }
    }

    if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100, "feed_override_set", "native_linuxcncrsh", "Set Feed_Override 100") ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100, "spindle_override_set", "native_linuxcncrsh", "Set Spindle_Override 100")) {
        return 21;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
    }
    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X)) {
        return 16;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'Y') ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y 1.000 0.010") ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y -1.000 0.010") ||
        !expect_axis_zero_position(&page, 'Y', "mcs")) {
        return 17;
    }
    {
        V5NativeReadback model_readback;
        v5_native_readback_init(&model_readback);
        v5_native_readback_set_motion_model(&model_readback, "XYZAC_TRT");
        v5_native_readback_set_all_homed(&model_readback, 1);
        v5_main_page_set_native_readback(&page, &model_readback);
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'A') ||
        !expect_axis_zero_position(&page, 'A', "mcs")) {
        return 18;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'C') ||
        !expect_axis_zero_position(&page, 'C', "mcs")) {
        return 36;
    }
    {
        V5NativeReadback model_readback;
        v5_native_readback_init(&model_readback);
        v5_native_readback_set_motion_model(&model_readback, "XYZBC_TRT");
        v5_native_readback_set_all_homed(&model_readback, 1);
        v5_main_page_set_native_readback(&page, &model_readback);
        if (page.mcs_targets[3].axis != 'B' || page.wcs_targets[3].axis != 'B' ||
            page.mcs_targets[4].axis != 'C' ||
            !v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'B') ||
            !expect_axis_zero_position(&page, 'B', "mcs")) {
            return 67;
        }
    }
    {
        V5NativeReadback readback;
        double table[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT];
        memset(table, 0, sizeof(table));
        table[2][0] = 10.0;
        table[2][1] = 20.0;
        table[2][2] = -30.0;
        v5_native_readback_init(&readback);
        v5_native_readback_set_wcs_table(
            &readback,
            2,
            &table[0][0],
            V5_NATIVE_READBACK_WCS_COUNT,
            V5_NATIVE_READBACK_WCS_AXIS_COUNT,
            9U);
        v5_native_readback_set_rtcp_actual(&readback, 0);
        v5_native_readback_set_interpreter_idle(&readback, 1);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_native_readback_set_modal_actual(&readback, "G0 G17 G21 G40 G49 G56 G64 G80 G90 G94 G97");
        v5_native_readback_set_tool_actual(&readback, 0, 1, 15.0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G54, 32, 52, 73) ||
            !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G56, 35, 198, 120)) {
            return 46;
        }
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_WCS, 'Z') ||
            !expect_command_line(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, "work_zero", "native_work_zero", "Set MDI G10 L20 P3 Z0") ||
            !expect_axis_zero_position(&page, 'Z', "wcs")) {
            return 19;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 42, 63, 85)) {
            return 47;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G56, 32, 52, 73)) {
            return 48;
        }
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all") || !page.selection.all_axes || page.selection.space != V5_MAIN_PAGE_SELECT_MCS) {
        return 20;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 29, 151, 104)) {
        return 49;
    }

    v5_native_readback_set_all_homed(&page.native_readback, 1);
    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_START)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (!v5_main_page_set_mdi_text(&page, "G4 P0")) {
        v5_program_controller_destroy(&controller);
        return 10;
    }
    if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
        v5_program_controller_destroy(&controller);
        return 50;
    }
    {
        V5NativeReadback line_readback;
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_all_homed(&line_readback, 1);
        v5_native_readback_set_current_line(&line_readback, 1);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            v5_program_controller_destroy(&controller);
            return 51;
        }
        v5_native_readback_set_interpreter_idle(&line_readback, 1);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            v5_program_controller_destroy(&controller);
            return 52;
        }
    }
    if (!expect_command(&page, V5_MAIN_PAGE_ACTION_START, "mdi_run", "native_linuxcncrsh")) {
        v5_program_controller_destroy(&controller);
        return 11;
    }

    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE)) {
        v5_program_controller_destroy(&controller);
        return 12;
    }

    {
        V5NativeReadback readback;
        const char *modal_text;
        v5_native_readback_init(&readback);
        v5_native_readback_set_rtcp_actual(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        modal_text = lv_label_get_text(page.modal_label);
        if (!modal_text || !strstr(modal_text, "L--\nRTCP OFF\n")) {
            v5_program_controller_destroy(&controller);
            return 38;
        }
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
            v5_program_controller_destroy(&controller);
            return 34;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 42, 63, 85)) {
            v5_program_controller_destroy(&controller);
            return 40;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_rtcp_control")) {
            v5_program_controller_destroy(&controller);
            return 13;
        }
        v5_native_readback_set_rtcp_actual(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        modal_text = lv_label_get_text(page.modal_label);
        if (!modal_text || !strstr(modal_text, "L--\nRTCP ON\n")) {
            v5_program_controller_destroy(&controller);
            return 39;
        }
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
            v5_program_controller_destroy(&controller);
            return 35;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 29, 151, 104)) {
            v5_program_controller_destroy(&controller);
            return 41;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_rtcp_control")) {
            v5_program_controller_destroy(&controller);
            return 14;
        }
        v5_native_readback_set_unavailable(&readback, "rtcp_unknown");
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 42, 63, 85)) {
            v5_program_controller_destroy(&controller);
            return 42;
        }
    }

    v5_native_readback_set_all_homed(&page.native_readback, 1);
    {
        const char *first_point_path = "v5_first_point_smoke.ngc";
        V5ProgramOpenResult open_result;
        memset(&open_result, 0, sizeof(open_result));
        if (!write_first_point_program(first_point_path) ||
            !v5_main_page_open_program(&page, first_point_path, &open_result) ||
            !expect_first_point(&page, first_point_path)) {
            unlink(first_point_path);
            v5_program_controller_destroy(&controller);
            return 15;
        }
        unlink(first_point_path);
    }

    {
        const char *preview_scroll_path = "v5_preview_scroll_smoke.ngc";
        V5ProgramOpenResult open_result;
        V5NativeReadback idle_readback;
        V5NativeReadback line_readback;
        V5NativeReadback stopped_readback;
        lv_point_t preview_touch_start = {120, 535};
        lv_point_t preview_touch_move = {120, 505};
        int preview_changed = 0;
        memset(&open_result, 0, sizeof(open_result));
        if (!write_preview_scroll_program(preview_scroll_path) ||
            !v5_main_page_open_program(&page, preview_scroll_path, &open_result)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 56;
        }
        v5_native_readback_init(&idle_readback);
        v5_native_readback_set_interpreter_idle(&idle_readback, 1);
        v5_main_page_set_native_readback(&page, &idle_readback);
        if (!program_row_text_has(&page, 0U, "001 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 61;
        }
        v5_native_readback_set_current_line(&idle_readback, 8);
        v5_main_page_set_native_readback(&page, &idle_readback);
        if (!program_row_text_has(&page, 0U, "001 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 63;
        }
        if (!v5_main_page_handle_touch_points(&page, &preview_touch_start, 1, 1, &preview_changed) ||
            !v5_main_page_handle_touch_points(&page, &preview_touch_move, 1, 1, &preview_changed) ||
            !preview_changed ||
            !program_row_text_has(&page, 0U, "002 ") ||
            !program_row_text_has(&page, 3U, "005 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 57;
        }
        if (!v5_main_page_handle_touch_points(&page, 0, 0, 0, &preview_changed)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 58;
        }
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_current_line(&line_readback, 8);
        v5_native_readback_set_motion_line(&line_readback, 3);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "003 ") ||
            !program_row_text_has(&page, 3U, "006 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 64;
        }
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_current_line(&line_readback, 5);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 59;
        }
        v5_native_readback_set_current_line(&line_readback, 8);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 3U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 62;
        }
        v5_native_readback_init(&stopped_readback);
        v5_native_readback_set_safety_estop(&stopped_readback, 1);
        v5_native_readback_set_machine_enabled(&stopped_readback, 0);
        v5_native_readback_set_interpreter_idle(&stopped_readback, 1);
        v5_native_readback_set_current_line(&stopped_readback, 0);
        v5_native_readback_set_motion_line(&stopped_readback, 0);
        v5_main_page_set_native_readback(&page, &stopped_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 3U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 60;
        }
        unlink(preview_scroll_path);
    }

    printf("v5 main page actions: buttons=%u local=view_3d mdi=start jog=native rtcp=toggle axis_select=prepared axis_zero_position=prepared first_point=prepared native_lines=prepared missing_gates=0\n",
           page.button_count);
    v5_program_controller_destroy(&controller);
    return 0;
}
