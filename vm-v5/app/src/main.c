#include "v5_app.h"

#include <stdio.h>

int main(void)
{
    V5ShellBootReport report;
    int rc = v5_ui_shell_bootstrap(&report, ".");
    if (rc == 0) {
        printf(
            "v5 LVGL shell initialized: closure_abi=%u commands=%u profile_maps=%u profiles=%u parameter_owners=%u microkernel_files=%u native_gates=%u native_readbacks=%u resources=%u status_refresh=%u status_mask=0x%08x status_flags=0x%08x main_page=%u applied=%u\n",
            report.boot_closure_abi,
            report.command_count,
            report.drive_profile_map_count,
            report.drive_profile_count,
            report.parameter_owner_count,
            report.microkernel_manifest_count,
            report.native_gate_count,
            report.native_readback_count,
            report.resource_count,
            report.status_refresh_ok,
            report.status_valid_mask,
            report.status_frame_flags,
            report.main_page_created,
            report.main_page_applied);
    }
    return rc;
}
