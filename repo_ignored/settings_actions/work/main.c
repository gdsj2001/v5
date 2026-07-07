#include "v5_app.h"
#include "v5_lvgl_remote_display.h"

#include "lvgl.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    unsigned short port = 18080U;
    int serve = 0;
    int i;
    int rc;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--serve") == 0) {
            serve = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            serve = 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (unsigned short)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_lvgl_shell [--once|--serve] [--port 18080]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    rc = serve ? v5_ui_shell_bootstrap_remote(&report, ".") : v5_ui_shell_bootstrap(&report, ".");
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
    printf("v5 UI remote relay listening: port=%u path=/remote/frame/full format=bgra32\n", (unsigned int)port);
    fflush(stdout);
    while (!g_stop_requested) {
        lv_tick_inc(30);
        (void)v5_ui_shell_refresh_once();
        if (!v5_remote_frame_poll(port, 30U)) {
            fprintf(stderr, "v5 UI remote relay failed on port %u\n", (unsigned int)port);
            return 1;
        }
    }
    return 0;
}
