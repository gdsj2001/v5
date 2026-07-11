#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_wcs_status.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"
#include "v5_v3_local_pages.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "v5_ui_shell_internal.h"

static V5ShellPageKind shell_page_for_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        return V5_SHELL_PAGE_SETTINGS;
    case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        return V5_SHELL_PAGE_TOOL;
    case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        return V5_SHELL_PAGE_PROBE;
    case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        return V5_SHELL_PAGE_OFFSET;
    case V5_MAIN_PAGE_ACTION_NAV_IO:
        return V5_SHELL_PAGE_IO;
    case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        return V5_SHELL_PAGE_NETWORK;
    case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        return V5_SHELL_PAGE_PROGRAM;
    case V5_MAIN_PAGE_ACTION_NAV_MDI:
    case V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT:
        return V5_SHELL_PAGE_MDI;
    case V5_MAIN_PAGE_ACTION_NAV_MAIN:
    default:
        return V5_SHELL_PAGE_MAIN;
    }
}

static const char *shell_page_name(V5ShellPageKind page)
{
    switch (page) {
    case V5_SHELL_PAGE_SETTINGS:
        return "settings";
    case V5_SHELL_PAGE_TOOL:
        return "tool";
    case V5_SHELL_PAGE_PROBE:
        return "probe";
    case V5_SHELL_PAGE_OFFSET:
        return "offset";
    case V5_SHELL_PAGE_IO:
        return "io";
    case V5_SHELL_PAGE_NETWORK:
        return "network";
    case V5_SHELL_PAGE_PROGRAM:
        return "program";
    case V5_SHELL_PAGE_MDI:
        return "mdi";
    case V5_SHELL_PAGE_MAIN:
    default:
        return "main";
    }
}

static void shell_hide_all_pages(void)
{
    unsigned int i;
    for (i = 0; i < (unsigned int)V5_SHELL_PAGE_COUNT; ++i) {
        if (g_v5_shell_shell_pages[i]) {
            lv_obj_add_flag(g_v5_shell_shell_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * REQ-UI-FIRST-FRAME-CACHE: this is the canonical path for all current
 * and future page switches. Navigation must show a cached/restored frame
 * within 0.2 s on the board. Any later page, popup, keyboard, network, tool,
 * probe, offset, IO, or similar full-screen surface must be pre-rendered into
 * a resident display cache at boot/canonical reload, or preserve the opening
 * frame before covering the page. cache_blit/frame restore is always the first
 * visible action; normal LVGL refresh may follow only for the dirty target
 * page, overlay, or changed cell.
 */
void shell_navigate(void *user_data, V5MainPageActionKind action)
{
    unsigned long long t0;
    unsigned long long elapsed;
    int cache_ok = 0;
    V5ShellPageKind page;
    (void)user_data;
    if (!g_v5_shell_main_page.root || !g_v5_shell_settings_page.root) {
        return;
    }
    t0 = shell_monotonic_ns();
    page = shell_page_for_action(action);
    if (page == V5_SHELL_PAGE_MDI) {
        if (action == V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT) {
            if (!g_v5_shell_mdi_edit_prepared && !shell_load_current_program_for_mdi_edit()) {
                return;
            }
            g_v5_shell_mdi_edit_prepared = 0;
        } else {
            g_v5_shell_mdi_line[0] = '\0';
            shell_clear_mdi_edit_metadata();
        }
        shell_update_mdi_line();
    } else if (page == V5_SHELL_PAGE_PROGRAM) {
        shell_update_program_row();
    } else if (page == V5_SHELL_PAGE_SETTINGS) {
    }
    shell_hide_all_pages();
    if (g_v5_shell_shell_pages[page]) {
        lv_obj_clear_flag(g_v5_shell_shell_pages[page], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_v5_shell_top_status_layer) {
        if (page == V5_SHELL_PAGE_MAIN) {
            lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (page == V5_SHELL_PAGE_MAIN) {
        if (g_v5_shell_main_cache_dirty) {
            lv_timer_handler();
            v5_lvgl_remote_display_render_now();
            cache_ok = v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_MAIN);
            g_v5_shell_main_cache_dirty = 0;
        } else {
            cache_ok = v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_MAIN);
        }
    } else if (page == V5_SHELL_PAGE_SETTINGS) {
        cache_ok = v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_SETTINGS);
    }
    if (!cache_ok) {
        v5_lvgl_remote_display_render_now();
    }
    g_v5_shell_current_page = page;
    elapsed = shell_monotonic_ns() - t0;
    shell_log_navigation_perf(shell_page_name(page), cache_ok, elapsed);
}

static void fill_report(V5ShellBootReport *report, const V5BootClosure *closure, int status_refresh_ok, int main_page_created, int main_page_applied)
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

static int v5_ui_shell_bootstrap_common(V5ShellBootReport *report, const char *project_root, int remote_display)
{
    V5BootClosure closure;
    lv_obj_t *screen;
    int status_refresh_ok;
    int main_page_created;
    int settings_page_created;
    int main_page_applied;

    v5_ui_model_init(&g_v5_shell_model);
    snprintf(g_v5_shell_project_root, sizeof(g_v5_shell_project_root), "%s", (project_root && project_root[0]) ? project_root : ".");
    v5_boot_closure_load(&closure, project_root);

    lv_init();
    if (remote_display) {
        if (!v5_lvgl_remote_display_setup(1024U, 600U)) {
            return 1;
        }
        (void)v5_lvgl_remote_input_setup();
        (void)v5_lvgl_touch_input_setup();
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
    status_refresh_ok = v5_ui_model_refresh_status_from_shm(&g_v5_shell_model, V5_STATUS_SHM_PATH);
    v5_program_controller_init(&g_v5_shell_program_controller);
    v5_settings_page_set_boot_closure(&closure);
    v5_settings_axis_table_load_boot_closure(&closure);
    main_page_created = v5_main_page_create(&g_v5_shell_main_page, screen);
    settings_page_created = v5_settings_page_create(&g_v5_shell_settings_page, screen);
    if (main_page_created) {
        g_v5_shell_shell_pages[V5_SHELL_PAGE_MAIN] = g_v5_shell_main_page.root;
        v5_lvgl_touch_input_set_points_callback(shell_toolpath_touch_points, 0);
    }
    if (settings_page_created) {
        g_v5_shell_shell_pages[V5_SHELL_PAGE_SETTINGS] = g_v5_shell_settings_page.root;
    }
    g_v5_shell_shell_pages[V5_SHELL_PAGE_TOOL] = v5_v3_local_page_create_tool(screen, shell_return_button_cb);
    g_v5_shell_shell_pages[V5_SHELL_PAGE_PROBE] = shell_create_aux_page(screen, "探测");
    g_v5_shell_shell_pages[V5_SHELL_PAGE_OFFSET] = v5_v3_local_page_create_offset(screen, shell_return_button_cb);
    g_v5_shell_shell_pages[V5_SHELL_PAGE_IO] = shell_create_aux_page(screen, "输入输出设置");
    g_v5_shell_shell_pages[V5_SHELL_PAGE_NETWORK] = shell_create_network_page(screen);
    g_v5_shell_shell_pages[V5_SHELL_PAGE_PROGRAM] = shell_create_program_page(screen);
    g_v5_shell_shell_pages[V5_SHELL_PAGE_MDI] = shell_create_mdi_page(screen);
    shell_create_top_status_layer(screen);
    if (main_page_created) {
        v5_main_page_bind_program_controller(&g_v5_shell_main_page, &g_v5_shell_program_controller);
        v5_main_page_set_command_execution_enabled(&g_v5_shell_main_page, 1);
        v5_main_page_set_navigation_callback(&g_v5_shell_main_page, shell_navigate, 0);
        v5_main_page_set_native_readback_refresh_callback(&g_v5_shell_main_page, shell_refresh_native_readback_for_action, 0);
        (void)shell_refresh_native_readback(1);
        (void)shell_refresh_safety_readback(1);
    }
    if (settings_page_created) {
        v5_settings_page_set_navigation_callback(&g_v5_shell_settings_page, shell_navigate, 0);
    }
    shell_hide_all_pages();
    if (g_v5_shell_shell_pages[V5_SHELL_PAGE_MAIN]) {
        lv_obj_clear_flag(g_v5_shell_shell_pages[V5_SHELL_PAGE_MAIN], LV_OBJ_FLAG_HIDDEN);
    }
    g_v5_shell_current_page = V5_SHELL_PAGE_MAIN;
    if (g_v5_shell_top_status_layer) {
        lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
    }
    main_page_applied = main_page_created ? v5_main_page_apply_status(&g_v5_shell_main_page, &g_v5_shell_model.status_view) : 0;
    if (settings_page_created) {
        (void)v5_settings_page_apply_status(&g_v5_shell_settings_page, &g_v5_shell_model.status_view);
    }
    lv_obj_invalidate(screen);
    lv_refr_now(NULL);
    (void)v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_MAIN);
    if (settings_page_created && main_page_created) {
        shell_hide_all_pages();
        lv_obj_clear_flag(g_v5_shell_shell_pages[V5_SHELL_PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN);
        if (g_v5_shell_top_status_layer) {
            lv_obj_add_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(screen);
        lv_refr_now(NULL);
        (void)v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_SETTINGS);
        shell_hide_all_pages();
        lv_obj_clear_flag(g_v5_shell_shell_pages[V5_SHELL_PAGE_MAIN], LV_OBJ_FLAG_HIDDEN);
        if (g_v5_shell_top_status_layer) {
            lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
        (void)v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_MAIN);
        lv_obj_invalidate(screen);
        lv_refr_now(NULL);
        (void)v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_MAIN);
    }
    fill_report(report, &closure, status_refresh_ok, main_page_created, main_page_applied);
    g_v5_shell_ui_ready = g_v5_shell_model.lvgl_initialized;
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
