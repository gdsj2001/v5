#include "v5_main_page.h"
#include "v5_lvgl_headless.h"

#include <stdio.h>
#include <string.h>

static int same_text(const char *left, const char *right)
{
    return left && right && strcmp(left, right) == 0;
}

static int expect(
    V5MainPage *page,
    V5MainPageActionKind action,
    V5CommandKind kind,
    const char *name,
    const char *owner,
    int index_value,
    int enabled_value,
    const char *text_value)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (!report.prepared || report.request.kind != kind || report.command.kind != kind) {
        return 0;
    }
    if (!same_text(report.command.name, name) || !same_text(report.command.owner, owner)) {
        return 0;
    }
    if (!report.command.accepted || report.request.index_value != index_value || report.request.enabled_value != enabled_value) {
        return 0;
    }
    if (text_value) {
        return same_text(report.request.text_value, text_value);
    }
    return report.request.text_value == 0;
}

static int write_program(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G90\nG1 X1 F100\nM2\n", fp);
    fclose(fp);
    return 1;
}

int main(void)
{
    const char *program_path = "v5_main_page_actions_program.ngc";
    V5ProgramController controller;
    V5ProgramOpenResult open_report;
    V5MainPage page;
    lv_obj_t *screen;

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    screen = lv_scr_act();
    v5_program_controller_init(&controller);
    if (!v5_main_page_create(&page, screen)) {
        v5_program_controller_destroy(&controller);
        return 2;
    }
    v5_main_page_bind_program_controller(&page, &controller);
    if (!write_program(program_path)) {
        v5_program_controller_destroy(&controller);
        return 16;
    }
    if (!v5_main_page_open_program(&page, program_path, &open_report)) {
        remove(program_path);
        v5_program_controller_destroy(&controller);
        return 17;
    }
    remove(program_path);
    if (!open_report.ok || open_report.byte_count == 0U || open_report.generation != 1U) {
        v5_program_controller_destroy(&controller);
        return 18;
    }
    if (page.button_count != V5_MAIN_PAGE_BUTTON_COUNT) {
        v5_program_controller_destroy(&controller);
        return 3;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_START, V5_COMMAND_START, "start", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 4;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_PAUSE, V5_COMMAND_PAUSE, "pause", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 19;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_RESUME, V5_COMMAND_RESUME, "resume", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 5;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, V5_COMMAND_ESTOP_FORCE, "estop_force", "native_safety", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 6;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_ESTOP_RESET, V5_COMMAND_ESTOP_RESET, "estop_reset", "native_safety", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 7;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_WCS_G54, V5_COMMAND_WCS_SELECT, "wcs_select", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 8;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_WCS_G55, V5_COMMAND_WCS_SELECT, "wcs_select", "native_linuxcncrsh", 1, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, V5_COMMAND_WORK_ZERO, "work_zero", "native_linuxcncrsh", 2, 0, "X")) {
        v5_program_controller_destroy(&controller);
        return 10;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_G92_CLEAR, V5_COMMAND_G92_CLEAR, "g92_clear", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 11;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_RTCP_ON, V5_COMMAND_RTCP_SET, "rtcp_set", "native_linuxcncrsh", 0, 1, 0)) {
        v5_program_controller_destroy(&controller);
        return 12;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_RTCP_OFF, V5_COMMAND_RTCP_SET, "rtcp_set", "native_linuxcncrsh", 0, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 13;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100, V5_COMMAND_FEED_OVERRIDE_SET, "feed_override_set", "native_linuxcncrsh", 100, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 14;
    }
    if (!expect(&page, V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100, V5_COMMAND_SPINDLE_OVERRIDE_SET, "spindle_override_set", "native_linuxcncrsh", 100, 0, 0)) {
        v5_program_controller_destroy(&controller);
        return 15;
    }

    printf("v5 main page actions: buttons=%u open_generation=%u start=%s last=%s owner=%s\n", page.button_count, page.last_program_open.generation, "start", page.last_action.command.name, page.last_action.command.owner);
    v5_program_controller_destroy(&controller);
    return 0;
}
