#include "v5_ui_shell_internal.h"

#include "v5_lvgl_remote_display.h"
#include "v5_ui_page_cache_registry.h"

#include <stdio.h>

static int g_v5_shell_navigation_pending;
static V5MainPageActionKind g_v5_shell_pending_navigation_action;

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

static void shell_hide_all_pages(void)
{
    unsigned int i;
    for (i = 0; i < (unsigned int)V5_SHELL_PAGE_COUNT; ++i) {
        if (g_v5_shell_shell_pages[i]) {
            lv_obj_add_flag(g_v5_shell_shell_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void shell_mark_page_cache_dirty(V5ShellPageKind page)
{
    if ((unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT) {
        return;
    }
    g_v5_shell_page_cache_dirty[page] = 1;
    if (g_v5_shell_remote_display_active &&
        v5_ui_page_cache_invalidate_now(
            (unsigned int)g_v5_shell_current_page,
            (unsigned int)page,
            g_v5_shell_shell_pages[page] &&
                !lv_obj_has_flag(g_v5_shell_shell_pages[page], LV_OBJ_FLAG_HIDDEN))) {
        v5_lvgl_remote_display_cache_invalidate(shell_page_cache_slot(page));
    }
}

void shell_show_page_objects(V5ShellPageKind page)
{
    unsigned long long now;
    int operator_status_active;
    shell_hide_all_pages();
    if ((unsigned int)page < (unsigned int)V5_SHELL_PAGE_COUNT && g_v5_shell_shell_pages[page]) {
        lv_obj_clear_flag(g_v5_shell_shell_pages[page], LV_OBJ_FLAG_HIDDEN);
    }
    v5_main_page_set_page_visible(
        &g_v5_shell_main_page,
        page == V5_SHELL_PAGE_MAIN);
    v5_settings_page_set_page_visible(
        &g_v5_shell_settings_page,
        page == V5_SHELL_PAGE_SETTINGS);
    if (!g_v5_shell_top_status_layer) {
        return;
    }
    now = shell_monotonic_ns();
    operator_status_active =
        g_v5_shell_operator_error_status.display_mode == V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS &&
        g_v5_shell_operator_error_show_until_ns != 0ULL &&
        now < g_v5_shell_operator_error_show_until_ns;
    if (page == V5_SHELL_PAGE_MAIN || operator_status_active) {
        lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_v5_shell_top_status_layer);
    } else {
        lv_obj_add_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

int shell_show_page_objects_for_cache_blit(V5ShellPageKind page)
{
    lv_obj_t *root;
    lv_disp_t *disp;
    if ((unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT) {
        return 0;
    }
    root = g_v5_shell_shell_pages[page];
    if (!root) {
        return 0;
    }
    disp = lv_obj_get_disp(root);
    if (!disp || disp->rendering_in_progress || disp->inv_p != 0U) {
        return 0;
    }
    lv_disp_enable_invalidation(disp, false);
    shell_show_page_objects(page);
    lv_disp_enable_invalidation(disp, true);
    return disp->inv_p == 0U &&
        !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
}

int shell_apply_page_resident_model(V5ShellPageKind page)
{
    V5NativeReadback readback = g_v5_shell_main_page.native_readback;
    shell_update_top_status_label();
    switch (page) {
    case V5_SHELL_PAGE_MAIN: {
        int applied;
        v5_main_page_set_native_readback(&g_v5_shell_main_page, &readback);
        applied = v5_main_page_apply_status(
            &g_v5_shell_main_page,
            &g_v5_shell_model.status_view);
        if (shell_main_page_structure_refresh_pending()) {
            shell_main_page_structure_refresh_consume();
        }
        return applied;
    }
    case V5_SHELL_PAGE_SETTINGS:
        (void)v5_settings_page_set_native_readback(&g_v5_shell_settings_page, &readback);
        return v5_settings_page_apply_status(
            &g_v5_shell_settings_page,
            &g_v5_shell_model.status_view);
    case V5_SHELL_PAGE_TOOL:
    case V5_SHELL_PAGE_PROBE:
    case V5_SHELL_PAGE_OFFSET:
    case V5_SHELL_PAGE_IO:
    case V5_SHELL_PAGE_NETWORK:
    case V5_SHELL_PAGE_PROGRAM:
    case V5_SHELL_PAGE_MDI:
        return 1;
    case V5_SHELL_PAGE_COUNT:
    default:
        return 0;
    }
}

static int shell_capture_current_page(void)
{
    V5ShellPageKind page = g_v5_shell_current_page;
    unsigned int slot = shell_page_cache_slot(page);
    lv_disp_t *disp;
    if (!g_v5_shell_remote_display_active ||
        (unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT ||
        slot >= V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT) {
        return 1;
    }
    if (g_v5_shell_page_cache_dirty[page]) {
        return shell_sync_current_page_cache_if_dirty() > 0;
    }
    disp = lv_obj_get_disp(g_v5_shell_shell_pages[page]);
    if (!disp || disp->rendering_in_progress) {
        return 0;
    }
    /* g_frame is the last frame already published to both local and remote
     * outputs. A pointer release may have queued a later LVGL invalidation,
     * but that pending work is not part of the currently visible frame and
     * must not block the cache-first page switch. */
    if (!v5_lvgl_remote_display_cache_capture(slot)) {
        return 0;
    }
    g_v5_shell_page_cache_dirty[page] = 0;
    return 1;
}

int shell_sync_current_page_cache_if_dirty(void)
{
    V5ShellPageKind page = g_v5_shell_current_page;
    unsigned int slot;
    lv_disp_t *disp;

    if ((unsigned int)page >= (unsigned int)V5_SHELL_PAGE_COUNT ||
        !g_v5_shell_page_cache_dirty[page] ||
        !g_v5_shell_shell_pages[page]) {
        return 0;
    }
    if (!shell_apply_page_resident_model(page)) {
        return -1;
    }
    lv_timer_handler();
    disp = lv_obj_get_disp(g_v5_shell_shell_pages[page]);
    if (!disp || disp->rendering_in_progress || disp->inv_p != 0U) {
        return -1;
    }
    if (!g_v5_shell_remote_display_active) {
        g_v5_shell_page_cache_dirty[page] = 0;
        return 1;
    }
    slot = shell_page_cache_slot(page);
    if (slot >= V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT ||
        !v5_lvgl_remote_display_cache_capture(slot)) {
        return -1;
    }
    g_v5_shell_page_cache_dirty[page] = 0;
    return 1;
}

static int shell_restore_previous_page_after_failed_prepare(V5ShellPageKind previous_page)
{
    unsigned int slot = shell_page_cache_slot(previous_page);
    lv_obj_t *root;
    lv_disp_t *disp;
    int previous_suppressed;
    int tree_restored = 0;

    if ((unsigned int)previous_page >= (unsigned int)V5_SHELL_PAGE_COUNT ||
        slot >= V5_REMOTE_DISPLAY_CACHE_PAGE_COUNT ||
        !v5_lvgl_remote_display_cache_valid(slot)) {
        return 0;
    }
    root = g_v5_shell_shell_pages[previous_page];
    disp = root ? lv_obj_get_disp(root) : NULL;
    previous_suppressed = v5_lvgl_remote_display_set_output_suppressed(1);
    if (disp && !disp->rendering_in_progress) {
        lv_disp_enable_invalidation(disp, false);
        shell_show_page_objects(previous_page);
        lv_disp_enable_invalidation(disp, true);
        /* The transition started from an empty invalidation queue. Drop only
         * invalidations produced by the suppressed failed target rebuild; the
         * target remains dirty and its next attempt invalidates its root. */
        disp->inv_p = 0U;
        tree_restored = !disp->rendering_in_progress && disp->inv_p == 0U &&
            !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    (void)v5_lvgl_remote_display_set_output_suppressed(previous_suppressed);
    if (!tree_restored) {
        return 0;
    }
    return v5_lvgl_remote_display_cache_blit(slot);
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
static int shell_navigate_now(V5MainPageActionKind action)
{
    unsigned long long t0;
    unsigned long long elapsed;
    int cache_ok = 0;
    V5ShellPageKind page;
    V5ShellPageKind previous_page;
    if (!g_v5_shell_main_page.root || !g_v5_shell_settings_page.root) {
        fprintf(stderr, "V5_UI_NAV_FAIL stage=roots action=%u\n", (unsigned int)action);
        return 0;
    }
    t0 = shell_monotonic_ns();
    page = shell_page_for_action(action);
    previous_page = g_v5_shell_current_page;
    if (!shell_capture_current_page()) {
        fprintf(stderr,
                "V5_UI_NAV_FAIL stage=capture_current current=%u target=%u current_dirty=%d\n",
                (unsigned int)previous_page,
                (unsigned int)page,
                g_v5_shell_page_cache_dirty[previous_page]);
        return 0;
    }
    if (page == V5_SHELL_PAGE_MDI) {
        if (action == V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT) {
            if (!g_v5_shell_mdi_edit_prepared && !shell_load_current_program_for_mdi_edit()) {
                fprintf(stderr, "V5_UI_NAV_FAIL stage=mdi_prepare target=%u\n", (unsigned int)page);
                return 0;
            }
            g_v5_shell_mdi_edit_prepared = 0;
        } else {
            g_v5_shell_mdi_line[0] = '\0';
            shell_clear_mdi_edit_metadata();
        }
        shell_update_mdi_line();
        shell_mark_page_cache_dirty(V5_SHELL_PAGE_MDI);
    }
    if (!g_v5_shell_remote_display_active) {
        shell_show_page_objects(page);
        v5_lvgl_remote_display_render_now();
        cache_ok = 0;
    } else {
        if (g_v5_shell_page_cache_dirty[page] ||
            !v5_lvgl_remote_display_cache_valid(shell_page_cache_slot(page))) {
            cache_ok = shell_prepare_page_cache(page);
        } else {
            cache_ok = shell_show_page_objects_for_cache_blit(page);
        }
        if (cache_ok) {
            cache_ok = v5_lvgl_remote_display_cache_blit(shell_page_cache_slot(page));
        }
        if (!cache_ok) {
            int target_dirty = g_v5_shell_page_cache_dirty[page];
            int target_valid = v5_lvgl_remote_display_cache_valid(shell_page_cache_slot(page));
            v5_lvgl_remote_display_cache_invalidate(shell_page_cache_slot(page));
            (void)shell_restore_previous_page_after_failed_prepare(previous_page);
            fprintf(stderr,
                    "V5_UI_NAV_FAIL stage=target_cache current=%u target=%u target_dirty=%d target_valid=%d\n",
                    (unsigned int)previous_page,
                    (unsigned int)page,
                    target_dirty,
                    target_valid);
            return 0;
        }
    }
    g_v5_shell_current_page = page;
    elapsed = shell_monotonic_ns() - t0;
    shell_log_navigation_perf(shell_page_name(page), cache_ok, elapsed);
    return 1;
}

void shell_navigate(void *user_data, V5MainPageActionKind action)
{
    (void)user_data;
    g_v5_shell_pending_navigation_action = action;
    g_v5_shell_navigation_pending = 1;
}

int shell_process_pending_navigation(void)
{
    V5MainPageActionKind action;
    if (!g_v5_shell_navigation_pending) {
        return 1;
    }
    action = g_v5_shell_pending_navigation_action;
    g_v5_shell_navigation_pending = 0;
    return shell_navigate_now(action);
}
