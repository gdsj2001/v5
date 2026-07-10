#include "v5_app.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_clock.h"
#include "v5_process_residency.h"
#include "v5_remote_input_ipc.h"

#include "lvgl.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define V5_UI_SHELL_LOOP_MS 10U

static volatile sig_atomic_t g_stop_requested;

static void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static void print_report(const V5ShellBootReport *report)
{
    printf(
        "v5 LVGL shell initialized: closure_abi=%u commands=%u profile_maps=%u profiles=%u parameter_owners=%u microkernel_files=%u native_gates=%u native_readbacks=%u resources=%u status_refresh=%u status_mask=0x%08x status_flags=0x%08x main_page=%u applied=%u\n",
        report->boot_closure_abi,
        report->command_count,
        report->drive_profile_map_count,
        report->drive_profile_count,
        report->parameter_owner_count,
        report->microkernel_manifest_count,
        report->native_gate_count,
        report->native_readback_count,
        report->resource_count,
        report->status_refresh_ok,
        report->status_valid_mask,
        report->status_frame_flags,
        report->main_page_created,
        report->main_page_applied);
}

int main(int argc, char **argv)
{
    V5ShellBootReport report;
    int serve = 0;
    int i;
    int rc;
    const char *project_root = getenv("V5_PROJECT_ROOT");

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--serve") == 0) {
            serve = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            serve = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_lvgl_shell [--once|--serve]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!v5_process_residency_lock("v5_lvgl_shell")) {
        return 3;
    }

    if (!project_root || !project_root[0]) {
        if (access("/opt/8ax/v5/config/drive-profiles/public/driver_profile_map.json", R_OK) == 0) {
            project_root = "/opt/8ax/v5";
        } else {
            project_root = ".";
        }
    }
    rc = serve ? v5_ui_shell_bootstrap_remote(&report, project_root) : v5_ui_shell_bootstrap(&report, project_root);
    if (rc != 0) {
        return rc;
    }
    print_report(&report);
    fflush(stdout);
    if (!serve) {
        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    printf("v5 UI remote framebuffer IPC ready: path=/run/8ax_v5_product_ui/remote_framebuffer.bgra dirty=/run/8ax_v5_product_ui/remote_dirty format=bgra32\n");
    fflush(stdout);
    v5_lvgl_clock_init();
    while (!g_stop_requested) {
        v5_lvgl_clock_advance();
        (void)v5_ui_shell_refresh_once();
        if (!v5_remote_frame_ipc_pump()) {
            fprintf(stderr, "v5 UI remote framebuffer IPC failed\n");
            return 1;
        }
        if (!v5_remote_input_ipc_process()) {
            fprintf(stderr, "v5 UI remote input IPC failed\n");
            return 1;
        }
        usleep(V5_UI_SHELL_LOOP_MS * 1000U);
    }
    return 0;
}
