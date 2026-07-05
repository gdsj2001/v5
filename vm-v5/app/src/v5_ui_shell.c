#include "v5_app.h"

#include "lvgl.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"

static V5MainPage g_main_page;
static V5ProgramController g_program_controller;
static V5UiModel g_model;
static int g_ui_ready;

static void fill_report(V5ShellBootReport *report, const V5BootClosure *closure, int status_refresh_ok, int main_page_created, int main_page_applied)
{
    if (!report || !closure) {
        return;
    }
    report->boot_closure_abi = g_model.boot_closure_abi;
    report->command_count = g_model.command_count;
    report->drive_profile_count = g_model.drive_profile_count;
    report->drive_profile_map_count = g_model.drive_profile_map_count;
    report->parameter_owner_count = g_model.parameter_owner_count;
    report->microkernel_manifest_count = (unsigned int)closure->microkernel_manifest_count;
    report->native_gate_count = (unsigned int)closure->native_gate_count;
    report->native_readback_count = (unsigned int)closure->native_readback_count;
    report->resource_count = g_model.resource_count;
    report->status_refresh_ok = (unsigned int)status_refresh_ok;
    report->status_valid_mask = g_model.status_view.valid_mask;
    report->status_frame_flags = g_model.status_view.frame_flags;
    report->main_page_created = (unsigned int)main_page_created;
    report->main_page_applied = (unsigned int)main_page_applied;
}

static int v5_ui_shell_bootstrap_common(V5ShellBootReport *report, const char *project_root, int remote_display)
{
    V5BootClosure closure;
    lv_obj_t *screen;
    int status_refresh_ok;
    int main_page_created;
    int main_page_applied;

    v5_ui_model_init(&g_model);
    v5_boot_closure_load(&closure, project_root);

    lv_init();
    if (remote_display) {
        if (!v5_lvgl_remote_display_setup(800U, 480U)) {
            return 1;
        }
    } else if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }

    screen = lv_scr_act();
    g_model.lvgl_initialized = 1;
    g_model.boot_closure_abi = closure.abi_version;
    g_model.command_count = (unsigned int)closure.command_count;
    g_model.drive_profile_count = (unsigned int)closure.drive_profile_count;
    g_model.drive_profile_map_count = (unsigned int)closure.drive_profile_map_count;
    g_model.parameter_owner_count = (unsigned int)closure.parameter_owner_count;
    g_model.resource_count = (unsigned int)closure.resource_count;
    status_refresh_ok = v5_ui_model_refresh_status_from_shm(&g_model, V5_STATUS_SHM_PATH);
    v5_program_controller_init(&g_program_controller);
    main_page_created = v5_main_page_create(&g_main_page, screen);
    if (main_page_created) {
        v5_main_page_bind_program_controller(&g_main_page, &g_program_controller);
    }
    main_page_applied = main_page_created ? v5_main_page_apply_status(&g_main_page, &g_model.status_view) : 0;
    lv_timer_handler();
    lv_refr_now(0);
    fill_report(report, &closure, status_refresh_ok, main_page_created, main_page_applied);
    g_ui_ready = g_model.lvgl_initialized;
    return g_ui_ready ? 0 : 1;
}

int v5_ui_shell_bootstrap(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 0);
}

int v5_ui_shell_bootstrap_remote(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 1);
}

int v5_ui_shell_refresh_once(void)
{
    if (!g_ui_ready || !g_main_page.root) {
        return 0;
    }
    (void)v5_ui_model_refresh_status_from_shm(&g_model, V5_STATUS_SHM_PATH);
    (void)v5_main_page_apply_status(&g_main_page, &g_model.status_view);
    lv_timer_handler();
    lv_refr_now(0);
    return 1;
}
