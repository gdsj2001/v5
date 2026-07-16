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
#include "v5_ui_first_frame_guard.h"
#include "v5_ui_page_cache_registry.h"
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

V5MainPage g_v5_shell_main_page;
V5SettingsPage g_v5_shell_settings_page;
V5ProgramController g_v5_shell_program_controller;
V5UiModel g_v5_shell_model;
lv_obj_t *g_v5_shell_shell_pages[V5_SHELL_PAGE_COUNT];
lv_obj_t *g_v5_shell_program_count_label;
lv_obj_t *g_v5_shell_program_empty_label;
lv_obj_t *g_v5_shell_program_source_label;
lv_obj_t *g_v5_shell_program_row_layers[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_program_row_name_labels[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_program_row_size_labels[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_program_row_created_labels[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_program_row_modified_labels[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_program_edit_button;
int g_v5_shell_program_row_indices[V5_PROGRAM_ROWS_MAX];
lv_obj_t *g_v5_shell_mdi_line_label;
lv_obj_t *g_v5_shell_mdi_status_label;
char g_v5_shell_project_root[256];
V5ProgramRow g_v5_shell_program_rows[V5_PROGRAM_ROWS_MAX];
V5ProgramRow g_v5_shell_program_scan_rows[V5_PROGRAM_SCAN_MAX];
unsigned int g_v5_shell_program_row_count;
int g_v5_shell_program_selected_index = -1;
int g_v5_shell_program_confirm_selected_index = -1;
char g_v5_shell_program_confirm_selected_path[384];
int g_v5_shell_program_last_click_index = -1;
unsigned long long g_v5_shell_program_last_click_ns;
char g_v5_shell_program_last_click_path[384];
char g_v5_shell_mdi_line[V5_MDI_TEXT_CAP] = "";
char g_v5_shell_mdi_edit_program_name[168];
char g_v5_shell_mdi_edit_program_path[384];
int g_v5_shell_mdi_edit_prepared;
int g_v5_shell_ui_ready;
int g_v5_shell_page_cache_dirty[V5_SHELL_PAGE_COUNT];
int g_v5_shell_remote_display_active;
V5ShellPageKind g_v5_shell_current_page = V5_SHELL_PAGE_MAIN;
lv_obj_t *g_v5_shell_top_status_layer;
lv_obj_t *g_v5_shell_top_status_label;
V5NativeOperatorErrorStatus g_v5_shell_operator_error_status;
uint64_t g_v5_shell_operator_error_generation_seen;
unsigned long long g_v5_shell_operator_error_show_until_ns;

unsigned long long g_v5_shell_native_readback_last_probe_ns;
unsigned long long g_v5_shell_modal_line_readback_last_probe_ns;
unsigned long long g_v5_shell_safety_readback_last_probe_ns;
unsigned long long g_v5_shell_operator_error_last_probe_ns;
unsigned long long g_v5_shell_ui_dynamic_last_refresh_ns;
unsigned long long g_v5_shell_ui_button_last_refresh_ns;
unsigned long long g_v5_shell_ui_estop_last_refresh_ns;
unsigned long long g_v5_shell_ui_slow_last_refresh_ns;
static int g_v5_shell_operator_error_projection_deferred;

static void shell_project_operator_error_status(unsigned long long now)
{
    int show_top_status =
        g_v5_shell_operator_error_status.display_mode == V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS &&
        g_v5_shell_operator_error_show_until_ns != 0ULL &&
        now < g_v5_shell_operator_error_show_until_ns;

    if (g_v5_shell_top_status_layer) {
        if (show_top_status && !shell_operator_error_popup_visible()) {
            lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_v5_shell_top_status_layer);
        } else if (g_v5_shell_current_page != V5_SHELL_PAGE_MAIN) {
            lv_obj_add_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
    }
    shell_update_top_status_label();
}

int shell_refresh_operator_error(int force)
{
    V5NativeOperatorErrorStatus status;
    unsigned long long now = shell_monotonic_ns();
    int changed = 0;
    int projected = 0;
    int overlay_active = v5_ui_first_frame_guard_overlay_active();

    if (g_v5_shell_operator_error_projection_deferred && !overlay_active) {
        shell_project_operator_error_status(now);
        g_v5_shell_operator_error_projection_deferred = 0;
        projected = 1;
    }
    if (!force && g_v5_shell_operator_error_last_probe_ns != 0ULL &&
        now - g_v5_shell_operator_error_last_probe_ns < V5_OPERATOR_ERROR_READ_MIN_NS) {
        return projected;
    }
    g_v5_shell_operator_error_last_probe_ns = now;
    v5_native_operator_error_status_init(&status);
    if (v5_native_operator_error_status_read(
            0,
            V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_MAX_AGE_MS,
            &status) &&
        status.generation != g_v5_shell_operator_error_generation_seen) {
        g_v5_shell_operator_error_status = status;
        g_v5_shell_operator_error_generation_seen = status.generation;
        if (status.display_mode == V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP) {
            g_v5_shell_operator_error_show_until_ns = 0ULL;
            if (!overlay_active && g_v5_shell_top_status_layer &&
                g_v5_shell_current_page != V5_SHELL_PAGE_MAIN) {
                lv_obj_add_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_HIDDEN);
            }
            shell_show_operator_error_popup(&status);
        } else if (status.display_mode == V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS) {
            g_v5_shell_operator_error_show_until_ns = now + V5_OPERATOR_ERROR_SHOW_NS;
        } else {
            g_v5_shell_operator_error_show_until_ns = 0ULL;
        }
        changed = 1;
    } else if (g_v5_shell_operator_error_status.display_mode ==
            V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS &&
        g_v5_shell_operator_error_show_until_ns != 0ULL &&
        now >= g_v5_shell_operator_error_show_until_ns) {
        v5_native_operator_error_status_init(&g_v5_shell_operator_error_status);
        g_v5_shell_operator_error_show_until_ns = 0ULL;
        changed = 1;
    }
    if (changed) {
        if (v5_ui_first_frame_guard_overlay_active()) {
            g_v5_shell_operator_error_projection_deferred = 1;
        } else {
            shell_project_operator_error_status(now);
            g_v5_shell_operator_error_projection_deferred = 0;
            projected = 1;
        }
    }
    return changed || projected;
}

static void shell_apply_modal_tool_readback(V5NativeReadback *readback, const V5NativeReadback *modal_tool_readback)
{
    if (!readback || !modal_tool_readback) {
        return;
    }
    if (v5_native_readback_modal_known(modal_tool_readback)) {
        v5_native_readback_set_modal_actual(readback, modal_tool_readback->modal_text);
    }
    if (v5_native_readback_tool_known(modal_tool_readback)) {
        v5_native_readback_set_tool_actual(
            readback,
            modal_tool_readback->tool_number,
            v5_native_readback_tool_length_known(modal_tool_readback),
            modal_tool_readback->tool_length_mm);
    }
    if (v5_native_readback_interpreter_idle_known(modal_tool_readback)) {
        v5_native_readback_set_interpreter_idle(readback, modal_tool_readback->interpreter_idle);
    }
    if (v5_native_readback_interpreter_known(modal_tool_readback)) {
        v5_native_readback_set_interpreter_paused(readback, modal_tool_readback->interpreter_paused);
    }
    if (v5_native_readback_current_line_known(modal_tool_readback)) {
        v5_native_readback_set_current_line(readback, modal_tool_readback->current_line);
    } else {
        v5_native_readback_set_current_line(readback, 0);
    }
    if (v5_native_readback_motion_line_known(modal_tool_readback)) {
        v5_native_readback_set_motion_line(readback, modal_tool_readback->motion_line);
    } else {
        v5_native_readback_set_motion_line(readback, 0);
    }
    if (v5_native_readback_mdi_run_known(modal_tool_readback)) {
        v5_native_readback_set_mdi_run_actual(
            readback,
            modal_tool_readback->mdi_run_active,
            modal_tool_readback->mdi_run_line,
            modal_tool_readback->mdi_run_command);
    } else {
        v5_native_readback_set_mdi_run_actual(readback, 0, 0, "");
    }
    if (v5_native_readback_all_homed_known(modal_tool_readback)) {
        v5_native_readback_set_all_homed(readback, modal_tool_readback->all_homed);
    }
}

static lv_obj_t *g_v5_shell_projected_native_root;
static int g_v5_shell_projected_native_valid;

static int shell_store_or_project_native_readback(const V5NativeReadback *readback)
{
    int storage_changed;
    int projection_changed;
    int settings_changed;
    int main_visible;
    int settings_visible;
    unsigned int native_change_flags;
    unsigned int projection_flags = 0U;
    if (!readback) {
        return 0;
    }
    main_visible =
        g_v5_shell_current_page == V5_SHELL_PAGE_MAIN &&
        g_v5_shell_main_page.root &&
        !lv_obj_has_flag(g_v5_shell_main_page.root, LV_OBJ_FLAG_HIDDEN);
    settings_visible =
        g_v5_shell_current_page == V5_SHELL_PAGE_SETTINGS &&
        g_v5_shell_settings_page.root &&
        !lv_obj_has_flag(g_v5_shell_settings_page.root, LV_OBJ_FLAG_HIDDEN);
    native_change_flags = v5_main_page_native_readback_change_flags(
        &g_v5_shell_main_page.native_readback,
        readback);
    settings_changed =
        (native_change_flags & V5_MAIN_PAGE_NATIVE_READBACK_MODEL) != 0U;
    storage_changed = native_change_flags != 0U;
    if (v5_ui_first_frame_guard_overlay_active()) {
        if (storage_changed) {
            v5_main_page_store_native_readback_during_modal(&g_v5_shell_main_page, readback);
        }
        return storage_changed;
    }

    if (!main_visible && storage_changed) {
        /* Hidden Main stores the resident snapshot only.  Its projector and
         * Fit are run once by navigation when Main becomes visible again. */
        v5_main_page_set_native_readback_flags(
            &g_v5_shell_main_page,
            readback,
            native_change_flags);
    }
    if (!main_visible && !settings_visible) {
        if (settings_changed) {
            shell_mark_page_cache_dirty(V5_SHELL_PAGE_SETTINGS);
        }
        return storage_changed;
    }

    if (settings_visible) {
        projection_changed =
            !g_v5_shell_projected_native_valid ||
            g_v5_shell_projected_native_root != g_v5_shell_settings_page.root ||
            settings_changed;
    } else {
        projection_changed =
            !g_v5_shell_projected_native_valid ||
            g_v5_shell_projected_native_root != g_v5_shell_main_page.root ||
            storage_changed;
    }
    if (!projection_changed) {
        if (main_visible && storage_changed) {
            v5_main_page_set_native_readback_flags(
                &g_v5_shell_main_page,
                readback,
                native_change_flags);
        }
        return storage_changed;
    }
    if (main_visible) {
        projection_flags =
            !g_v5_shell_projected_native_valid ||
            g_v5_shell_projected_native_root != g_v5_shell_main_page.root
                ? V5_MAIN_PAGE_NATIVE_READBACK_ALL
                : native_change_flags;
        v5_main_page_set_native_readback_flags(
            &g_v5_shell_main_page,
            readback,
            projection_flags);
    } else {
        (void)v5_settings_page_set_native_readback(&g_v5_shell_settings_page, readback);
    }
    if (main_visible && settings_changed) {
        shell_mark_page_cache_dirty(V5_SHELL_PAGE_SETTINGS);
    }
    if (main_visible) {
        shell_update_top_status_label();
    }
    g_v5_shell_projected_native_root =
        main_visible ? g_v5_shell_main_page.root : g_v5_shell_settings_page.root;
    g_v5_shell_projected_native_valid = 1;
    return 1;
}

int shell_refresh_modal_line_readback(int force)
{
    V5NativeReadback readback;
    V5NativeReadback modal_tool_readback;
    unsigned long long now;

    now = shell_monotonic_ns();
    if (!force && g_v5_shell_modal_line_readback_last_probe_ns != 0ULL &&
        now - g_v5_shell_modal_line_readback_last_probe_ns < V5_MODAL_LINE_READBACK_MIN_NS) {
        return 0;
    }
    g_v5_shell_modal_line_readback_last_probe_ns = now;

    v5_native_readback_init(&modal_tool_readback);
    readback = g_v5_shell_main_page.native_readback;
    v5_native_readback_clear_all_homed(&readback);
    if (v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool_readback)) {
        shell_apply_modal_tool_readback(&readback, &modal_tool_readback);
    } else {
        v5_native_readback_set_current_line(&readback, 0);
        v5_native_readback_set_motion_line(&readback, 0);
        v5_native_readback_set_mdi_run_actual(&readback, 0, 0, "");
    }
    return shell_store_or_project_native_readback(&readback);
}

int shell_refresh_native_readback(int force)
{
    V5NativeReadback readback;
    V5NativeReadback rtcp_readback;
    V5NativeReadback wcs_readback;
    V5NativeReadback g53_geometry_readback;
    V5NativeReadback modal_tool_readback;
    unsigned long long now;

    now = shell_monotonic_ns();
    if (!force && g_v5_shell_native_readback_last_probe_ns != 0ULL &&
        now - g_v5_shell_native_readback_last_probe_ns < V5_NATIVE_READBACK_MIN_NS) {
        return 0;
    }
    g_v5_shell_native_readback_last_probe_ns = now;

    readback = g_v5_shell_main_page.native_readback;
    v5_native_readback_init(&rtcp_readback);
    v5_native_readback_init(&wcs_readback);
    v5_native_readback_init(&g53_geometry_readback);
    v5_native_readback_init(&modal_tool_readback);
    v5_native_readback_clear_all_homed(&readback);
    if (v5_native_rtcp_status_read(0, V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS, &rtcp_readback) &&
        v5_native_readback_rtcp_known(&rtcp_readback)) {
        v5_native_readback_set_rtcp_actual(&readback, rtcp_readback.rtcp_enabled);
    }
    if (v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, &wcs_readback) &&
        v5_native_readback_wcs_known(&wcs_readback)) {
        if (v5_native_readback_wcs_table_known(&wcs_readback)) {
            v5_native_readback_set_wcs_table(
                &readback,
                wcs_readback.wcs_index,
                &wcs_readback.wcs_offsets[0][0],
                V5_NATIVE_READBACK_WCS_COUNT,
                V5_NATIVE_READBACK_WCS_AXIS_COUNT,
                wcs_readback.wcs_offsets_epoch);
        } else {
            v5_native_readback_set_wcs_actual(&readback, wcs_readback.wcs_index);
        }
    }
    if (v5_native_g53_geometry_status_read(0, V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS, &g53_geometry_readback) &&
        v5_native_readback_g53_geometry_known(&g53_geometry_readback) &&
        v5_native_readback_motion_model_known(&g53_geometry_readback)) {
        v5_native_readback_set_g53_geometry(
            &readback,
            &g53_geometry_readback.g53_centers[0][0],
            V5_NATIVE_READBACK_G53_CENTER_COUNT,
            V5_NATIVE_READBACK_G53_AXIS_COUNT,
            g53_geometry_readback.g53_geometry_epoch);
        v5_native_readback_set_motion_model(&readback, g53_geometry_readback.motion_model);
    } else {
        v5_native_readback_set_g53_geometry_stale(&readback);
    }
    if (v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool_readback)) {
        shell_apply_modal_tool_readback(&readback, &modal_tool_readback);
    } else {
        v5_native_readback_set_current_line(&readback, 0);
        v5_native_readback_set_motion_line(&readback, 0);
        v5_native_readback_set_mdi_run_actual(&readback, 0, 0, "");
    }

    return shell_store_or_project_native_readback(&readback);
}

int shell_refresh_safety_readback(int force)
{
    V5NativeReadback readback;
    V5CommandGateResult gate_result;
    unsigned long long now;
    int estop_ok;
    int machine_ok;

    now = shell_monotonic_ns();
    if (!force && g_v5_shell_safety_readback_last_probe_ns != 0ULL &&
        now - g_v5_shell_safety_readback_last_probe_ns < V5_SAFETY_READBACK_MIN_NS) {
        return 1;
    }
    g_v5_shell_safety_readback_last_probe_ns = now;

    readback = g_v5_shell_main_page.native_readback;
    v5_command_gate_result_init(&gate_result);
    (void)v5_command_gate_probe_safety(&gate_result, force ? 1000U : V5_SAFETY_READBACK_TIMEOUT_MS);
    estop_ok = gate_result.safety_estop_known;
    machine_ok = gate_result.machine_enable_known;
    if (estop_ok) {
        v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
    }
    if (machine_ok) {
        v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
    }
    if (estop_ok || machine_ok) {
        return shell_store_or_project_native_readback(&readback);
    }
    return 0;
}


void shell_refresh_native_readback_for_action(void *user_data, V5MainPageActionKind action)
{
    int reset_semantics;
    (void)user_data;
    if (action == V5_MAIN_PAGE_ACTION_ESTOP_FORCE) {
        (void)shell_refresh_safety_readback(1);
        reset_semantics =
            (v5_native_readback_safety_estop_known(&g_v5_shell_main_page.native_readback) &&
             g_v5_shell_main_page.native_readback.safety_estop_active) ||
            (v5_native_readback_machine_enable_known(&g_v5_shell_main_page.native_readback) &&
             !g_v5_shell_main_page.native_readback.machine_enabled);
        if (!reset_semantics) {
            return;
        }
    }
    (void)shell_refresh_native_readback(1);
    (void)shell_refresh_safety_readback(1);
}

int shell_toolpath_touch_points(const lv_point_t *points, int count, int pressed, int *changed, void *user_data)
{
    int local_changed = 0;
    int consumed;
    (void)user_data;
    if (changed) {
        *changed = 0;
    }
    if (!g_v5_shell_ui_ready || !g_v5_shell_main_page.root ||
        g_v5_shell_current_page != V5_SHELL_PAGE_MAIN ||
        lv_obj_has_flag(g_v5_shell_main_page.root, LV_OBJ_FLAG_HIDDEN) ||
        v5_ui_first_frame_guard_overlay_active()) {
        return 0;
    }
    consumed = v5_main_page_handle_touch_points(&g_v5_shell_main_page, points, count, pressed, &local_changed);
    if (local_changed) {
        (void)v5_main_page_apply_status_flags(&g_v5_shell_main_page, &g_v5_shell_model.status_view, V5_MAIN_PAGE_REFRESH_DYNAMIC);
        lv_timer_handler();
        if (changed) {
            *changed = 1;
        }
    }
    return consumed;
}
