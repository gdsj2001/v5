#include "v5_app.h"

#include "lvgl.h"
#include "v5_lvgl_headless.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"

static V5MainPage g_main_page;
static V5ProgramController g_program_controller;

int v5_ui_shell_bootstrap(V5ShellBootReport *report, const char *project_root)
{
    V5BootClosure closure;
    V5UiModel model;
    lv_obj_t *screen;
    int status_refresh_ok;
    int main_page_created;
    int main_page_applied;
    v5_ui_model_init(&model);
    v5_boot_closure_load(&closure, project_root);

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    screen = lv_scr_act();
    model.lvgl_initialized = 1;
    model.boot_closure_abi = closure.abi_version;
    model.command_count = (unsigned int)closure.command_count;
    model.drive_profile_count = (unsigned int)closure.drive_profile_count;
    model.drive_profile_map_count = (unsigned int)closure.drive_profile_map_count;
    model.parameter_owner_count = (unsigned int)closure.parameter_owner_count;
    model.resource_count = (unsigned int)closure.resource_count;
    status_refresh_ok = v5_ui_model_refresh_status_from_shm(&model, V5_STATUS_SHM_PATH);
    v5_program_controller_init(&g_program_controller);
    main_page_created = v5_main_page_create(&g_main_page, screen);
    if (main_page_created) {
        v5_main_page_bind_program_controller(&g_main_page, &g_program_controller);
    }
    main_page_applied = main_page_created ? v5_main_page_apply_status(&g_main_page, &model.status_view) : 0;

    if (report) {
        report->boot_closure_abi = model.boot_closure_abi;
        report->command_count = model.command_count;
        report->drive_profile_count = model.drive_profile_count;
        report->drive_profile_map_count = model.drive_profile_map_count;
        report->parameter_owner_count = model.parameter_owner_count;
        report->microkernel_manifest_count = (unsigned int)closure.microkernel_manifest_count;
        report->native_gate_count = (unsigned int)closure.native_gate_count;
        report->native_readback_count = (unsigned int)closure.native_readback_count;
        report->resource_count = model.resource_count;
        report->status_refresh_ok = (unsigned int)status_refresh_ok;
        report->status_valid_mask = model.status_view.valid_mask;
        report->status_frame_flags = model.status_view.frame_flags;
        report->main_page_created = (unsigned int)main_page_created;
        report->main_page_applied = (unsigned int)main_page_applied;
    }

    return model.lvgl_initialized ? 0 : 1;
}
