#include "v5_main_page.h"
#include "v5_command_gate_ipc.h"
#include "v5_lvgl_headless.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int same_text(const char *left, const char *right)
{
    return left && right && strcmp(left, right) == 0;
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

static int expect_rotary_equiv_zero(V5MainPage *page, char axis)
{
    V5MainPageActionReport report;
    unsigned int expected_mask = axis == 'A' ? V5_COMMAND_AXIS_A_MASK : V5_COMMAND_AXIS_C_MASK;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    return same_text(report.command.name, "rotary_equiv_zero") &&
           same_text(report.command.owner, "native_rotary_gate") &&
           report.command.accepted &&
           report.request.kind == V5_COMMAND_ROTARY_EQUIV_ZERO &&
           report.request.axis_mask == expected_mask &&
           report.request.enabled_value == 1 &&
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

static int expect_missing_gate(V5MainPage *page, V5MainPageActionKind action)
{
    V5MainPageActionReport report;
    if (v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    return page->last_action.action == action && !page->last_action.prepared;
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
    v5_program_controller_init(&controller);
    v5_main_page_bind_program_controller(&page, &controller);

    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MAIN, "nav_main") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_SETTINGS, "nav_settings") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_PROGRAM, "nav_program") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MDI, "nav_mdi") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all")) {
        return 4;
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "jog_step") || page.jog_step != 10.0) {
        return 5;
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
    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X)) {
        return 16;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'Y') ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y 100.000 10.000") ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y -100.000 10.000")) {
        return 17;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'A') ||
        !expect_rotary_equiv_zero(&page, 'A')) {
        return 18;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'C') ||
        !expect_rotary_equiv_zero(&page, 'C')) {
        return 36;
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
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_native_readback_set_modal_actual(&readback, "G0 G17 G21 G40 G49 G56 G64 G80 G90 G94 G97");
        v5_native_readback_set_tool_actual(&readback, 0, 1, 15.0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_WCS, 'Z') ||
            !expect_command_line(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, "work_zero", "native_linuxcncrsh", "Set MDI G10 L20 P3 Z0")) {
            return 19;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all") || !page.selection.all_axes || page.selection.space != V5_MAIN_PAGE_SELECT_MCS) {
        return 20;
    }

    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_START)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (!v5_main_page_set_mdi_text(&page, "G4 P0")) {
        v5_program_controller_destroy(&controller);
        return 10;
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
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_linuxcncrsh", "Set Mode MDI\nSet MDI M128")) {
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
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_linuxcncrsh", "Set Mode MDI\nSet MDI M129")) {
            v5_program_controller_destroy(&controller);
            return 14;
        }
    }

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

    printf("v5 main page actions: buttons=%u local=view_3d mdi=start jog=native rtcp=toggle axis_select=prepared rotary_equiv_zero=prepared first_point=prepared native_lines=prepared missing_gates=0\n",
           page.button_count);
    v5_program_controller_destroy(&controller);
    return 0;
}
