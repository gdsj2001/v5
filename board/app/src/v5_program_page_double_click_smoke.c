#include "v5_app.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    V5ShellBootReport report;
    const char *loaded_path;

    memset(&report, 0, sizeof(report));
    if (v5_ui_shell_bootstrap(&report, ".") != 0) {
        return 2;
    }
    if (!report.main_page_created) {
        return 2;
    }
    if (v5_ui_shell_test_program_selected_index() != -1) {
        return 3;
    }
    if (!v5_ui_shell_test_click_program_name(0U)) {
        return 4;
    }
    if (v5_ui_shell_test_program_selected_index() != 0 || v5_ui_shell_test_program_loaded()) {
        return 5;
    }
    if (!v5_ui_shell_test_click_program_edit()) {
        return 9;
    }
    if (!v5_ui_shell_test_current_page_is_mdi() ||
        !v5_ui_shell_test_mdi_text() ||
        !strstr(v5_ui_shell_test_mdi_text(), "RE_V5_CC_AC_LINUXCNC_NATIVE")) {
        return 10;
    }
    if (!v5_ui_shell_test_click_program_name(0U)) {
        return 6;
    }
    if (v5_ui_shell_test_program_loaded()) {
        return 13;
    }
    if (!v5_ui_shell_test_click_program_name(0U)) {
        return 14;
    }
    if (!v5_ui_shell_test_program_loaded()) {
        return 7;
    }
    loaded_path = v5_ui_shell_test_program_loaded_path();
    if (!loaded_path || !strstr(loaded_path, "cc-ac.ngc")) {
        return 8;
    }
    if (!v5_ui_shell_test_double_click_main_program_area()) {
        return 11;
    }
    if (!v5_ui_shell_test_current_page_is_mdi() ||
        !v5_ui_shell_test_mdi_text() ||
        !strstr(v5_ui_shell_test_mdi_text(), "RE_V5_CC_AC_LINUXCNC_NATIVE")) {
        return 12;
    }
    printf("v5 program page double click: first_touch=select second_touch=open path=%s\n", loaded_path);
    return 0;
}
