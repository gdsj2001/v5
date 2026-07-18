#ifndef V5_APP_H
#define V5_APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5ShellBootReport {
    unsigned int boot_closure_abi;
    unsigned int command_count;
    unsigned int drive_profile_count;
    unsigned int drive_profile_map_count;
    unsigned int parameter_owner_count;
    unsigned int microkernel_manifest_count;
    unsigned int native_gate_count;
    unsigned int native_readback_count;
    unsigned int resource_count;
    unsigned int status_refresh_ok;
    unsigned int status_valid_mask;
    unsigned int status_frame_flags;
    unsigned int main_page_created;
    unsigned int main_page_applied;
} V5ShellBootReport;

int v5_ui_shell_bootstrap(V5ShellBootReport *report, const char *project_root);
int v5_ui_shell_bootstrap_remote(V5ShellBootReport *report, const char *project_root);
int v5_ui_shell_refresh_once(void);

#ifdef V5_UI_SHELL_TEST_HOOKS
int v5_ui_shell_test_program_selected_index(void);
int v5_ui_shell_test_program_loaded(void);
const char *v5_ui_shell_test_program_loaded_path(void);
int v5_ui_shell_test_click_program_name(unsigned int idx);
int v5_ui_shell_test_click_program_edit(void);
int v5_ui_shell_test_double_click_main_program_area(void);
int v5_ui_shell_test_current_page_is_mdi(void);
const char *v5_ui_shell_test_mdi_text(void);
int v5_ui_shell_test_mdi_press_key(const char *key);
unsigned int v5_ui_shell_test_mdi_cursor_pos(void);
unsigned int v5_ui_shell_test_mdi_cursor_visual_row(void);
unsigned int v5_ui_shell_test_mdi_first_visible_row(void);
unsigned int v5_ui_shell_test_mdi_cursor_blink_ms(void);
const char *v5_ui_shell_test_top_status_text(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
