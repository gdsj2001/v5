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

#ifdef __cplusplus
}
#endif

#endif
