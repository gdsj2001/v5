#include "v5_ui_shell_internal.h"

#include "v5_boot_closure.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_page_cache_registry.h"

#include <stdio.h>
#include <string.h>

static void fill_report(
    V5ShellBootReport *report,
    const V5BootClosure *closure,
    int status_refresh_ok,
    int main_page_created,
    int main_page_applied)
{
    if (!report || !closure) {
        return;
    }
    report->boot_closure_abi = g_v5_shell_model.boot_closure_abi;
    report->command_count = g_v5_shell_model.command_count;
    report->drive_profile_count = g_v5_shell_model.drive_profile_count;
    report->drive_profile_map_count = g_v5_shell_model.drive_profile_map_count;
    report->parameter_owner_count = g_v5_shell_model.parameter_owner_count;
    report->microkernel_manifest_count = (unsigned int)closure->microkernel_manifest_count;
    report->native_gate_count = (unsigned int)closure->native_gate_count;
    report->native_readback_count = (unsigned int)closure->native_readback_count;
    report->resource_count = g_v5_shell_model.resource_count;
    report->status_refresh_ok = (unsigned int)status_refresh_ok;
    report->status_valid_mask = g_v5_shell_model.status_view.valid_mask;
    report->status_frame_flags = g_v5_shell_model.status_view.frame_flags;
    report->main_page_created = (unsigned int)main_page_created;
    report->main_page_applied = (unsigned int)main_page_applied;
}

static int v5_ui_shell_bootstrap_common(
    V5ShellBootReport *report,
    const char *project_root,
    int remote_display)
{
    V5BootClosure closure;
    lv_obj_t *screen;
    int status_refresh_ok;
    int main_page_created;
    int settings_page_created;
    int main_page_applied;
    int all_page_caches_ready;
    int page_registry_ready;
    V5UiPageCacheQueueEvidence queue_evidence[V5_SHELL_PAGE_COUNT];
    unsigned long long peak_cpu_pct_x100 = 0ULL;

    g_v5_shell_ui_ready = 0;
    shell_reset_axis_slave_mapping_status_probe();
    v5_ui_model_init(&g_v5_shell_model);
    g_v5_shell_remote_display_active = remote_display ? 1 : 0;
    snprintf(
        g_v5_shell_project_root,
        sizeof(g_v5_shell_project_root),
        "%s",
        (project_root && project_root[0]) ? project_root : ".");
    v5_boot_closure_load(&closure, project_root);

    lv_init();
    if (remote_display) {
        if (!v5_lvgl_remote_display_setup(1024U, 600U)) {
            return 1;
        }
    } else if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }

    screen = lv_scr_act();
    g_v5_shell_model.lvgl_initialized = 1;
    g_v5_shell_model.boot_closure_abi = closure.abi_version;
    g_v5_shell_model.command_count = (unsigned int)closure.command_count;
    g_v5_shell_model.drive_profile_count = (unsigned int)closure.drive_profile_count;
    g_v5_shell_model.drive_profile_map_count = (unsigned int)closure.drive_profile_map_count;
    g_v5_shell_model.parameter_owner_count = (unsigned int)closure.parameter_owner_count;
    g_v5_shell_model.resource_count = (unsigned int)closure.resource_count;
    status_refresh_ok = v5_ui_model_refresh_status_from_shm(
        &g_v5_shell_model,
        V5_STATUS_SHM_PATH);
    v5_program_controller_init(&g_v5_shell_program_controller);
    v5_settings_page_set_boot_closure(&closure);
    v5_settings_axis_table_load_boot_closure(&closure);
    memset(g_v5_shell_shell_pages, 0, sizeof(g_v5_shell_shell_pages));
    memset(g_v5_shell_page_cache_dirty, 0, sizeof(g_v5_shell_page_cache_dirty));
    g_v5_shell_current_page = V5_SHELL_PAGE_MAIN;
    shell_create_top_status_layer(screen);
    shell_create_operator_error_popup(screen);
    all_page_caches_ready = shell_run_boot_page_cache_queue(
        screen,
        g_v5_shell_remote_display_active,
        queue_evidence,
        &peak_cpu_pct_x100);
    main_page_created = queue_evidence[V5_SHELL_PAGE_MAIN].create_ok ? 1 : 0;
    settings_page_created = queue_evidence[V5_SHELL_PAGE_SETTINGS].create_ok ? 1 : 0;
    main_page_applied = g_v5_shell_remote_display_active
        ? (queue_evidence[V5_SHELL_PAGE_MAIN].apply_ok ? 1 : 0)
        : 0;
    if (!all_page_caches_ready) {
        return 1;
    }
    page_registry_ready = shell_boot_page_cache_registry_validate();
    if (!page_registry_ready) {
        return 1;
    }
    if (g_v5_shell_remote_display_active) {
        lv_disp_t *disp = lv_obj_get_disp(g_v5_shell_shell_pages[V5_SHELL_PAGE_MAIN]);
        if (!shell_show_page_objects_for_cache_blit(V5_SHELL_PAGE_MAIN) ||
            !disp || disp->rendering_in_progress || disp->inv_p != 0U) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u stage=main_select invalidation_clean=0\n",
                    V5_UI_CACHE_BOOT_WORKER_ID);
            return 1;
        }
        if (!v5_lvgl_remote_display_claim_physical_framebuffer()) {
            fprintf(stderr, "V5_UI_BOOT event=fail stage=physical_framebuffer_claim\n");
            return 1;
        }
        (void)v5_lvgl_remote_display_set_output_suppressed(0);
        if (!v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_MAIN)) {
            return 1;
        }
        if (disp->rendering_in_progress || disp->inv_p != 0U) {
            fprintf(stderr,
                    "V5_UI_CACHE_QUEUE event=fail worker_id=%u stage=main_blit invalidation_clean=0\n",
                    V5_UI_CACHE_BOOT_WORKER_ID);
            return 1;
        }
        (void)v5_lvgl_remote_input_setup();
        if (!v5_lvgl_touch_input_setup()) {
            fprintf(stderr, "V5_UI_BOOT event=fail stage=touch_input_registration\n");
            return 1;
        }
    } else {
        shell_show_page_objects(V5_SHELL_PAGE_MAIN);
        main_page_applied = main_page_created
            ? v5_main_page_apply_status(
                &g_v5_shell_main_page,
                &g_v5_shell_model.status_view)
            : 0;
        if (settings_page_created) {
            (void)v5_settings_page_apply_status(
                &g_v5_shell_settings_page,
                &g_v5_shell_model.status_view);
        }
        lv_obj_invalidate(screen);
        lv_refr_now(NULL);
    }
    fill_report(
        report,
        &closure,
        status_refresh_ok,
        main_page_created,
        main_page_applied);
    g_v5_shell_ui_ready =
        g_v5_shell_model.lvgl_initialized && page_registry_ready && all_page_caches_ready;
    if (g_v5_shell_ui_ready && main_page_created) {
        v5_main_page_set_command_execution_enabled(&g_v5_shell_main_page, 1);
        (void)shell_refresh_axis_slave_mapping_status(1);
    }
    return g_v5_shell_ui_ready ? 0 : 1;
}

int v5_ui_shell_bootstrap(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 0);
}

int v5_ui_shell_bootstrap_remote(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 1);
}
