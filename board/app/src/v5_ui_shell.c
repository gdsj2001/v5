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
int g_v5_shell_main_cache_dirty;
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

int shell_refresh_operator_error(int force)
{
    V5NativeOperatorErrorStatus status;
    unsigned long long now = shell_monotonic_ns();
    int changed = 0;
    if (!force && g_v5_shell_operator_error_last_probe_ns != 0ULL &&
        now - g_v5_shell_operator_error_last_probe_ns < V5_OPERATOR_ERROR_READ_MIN_NS) {
        return 0;
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
        g_v5_shell_operator_error_show_until_ns = now + V5_OPERATOR_ERROR_SHOW_NS;
        changed = 1;
    } else if (g_v5_shell_operator_error_show_until_ns != 0ULL && now >= g_v5_shell_operator_error_show_until_ns) {
        v5_native_operator_error_status_init(&g_v5_shell_operator_error_status);
        g_v5_shell_operator_error_show_until_ns = 0ULL;
        changed = 1;
    }
    if (changed) {
        shell_update_top_status_label();
    }
    return changed;
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

int shell_refresh_modal_line_readback(int force)
{
    V5NativeReadback before;
    V5NativeReadback readback;
    V5NativeReadback modal_tool_readback;
    unsigned long long now;
    int changed;

    now = shell_monotonic_ns();
    if (!force && g_v5_shell_modal_line_readback_last_probe_ns != 0ULL &&
        now - g_v5_shell_modal_line_readback_last_probe_ns < V5_MODAL_LINE_READBACK_MIN_NS) {
        return 0;
    }
    g_v5_shell_modal_line_readback_last_probe_ns = now;

    v5_native_readback_init(&modal_tool_readback);
    before = g_v5_shell_main_page.native_readback;
    readback = g_v5_shell_main_page.native_readback;
    v5_native_readback_clear_all_homed(&readback);
    if (v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool_readback)) {
        shell_apply_modal_tool_readback(&readback, &modal_tool_readback);
    } else {
        v5_native_readback_set_current_line(&readback, 0);
        v5_native_readback_set_motion_line(&readback, 0);
        v5_native_readback_set_mdi_run_actual(&readback, 0, 0, "");
    }
    v5_main_page_set_native_readback(&g_v5_shell_main_page, &readback);
    shell_update_top_status_label();
    changed = memcmp(&before, &readback, sizeof(readback)) != 0;
    return changed;
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
        return 1;
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
        v5_native_readback_g53_geometry_known(&g53_geometry_readback)) {
        v5_native_readback_set_g53_geometry(
            &readback,
            &g53_geometry_readback.g53_centers[0][0],
            V5_NATIVE_READBACK_G53_CENTER_COUNT,
            V5_NATIVE_READBACK_G53_AXIS_COUNT,
            g53_geometry_readback.g53_geometry_epoch);
        if (v5_native_readback_motion_model_known(&g53_geometry_readback)) {
            v5_native_readback_set_motion_model(&readback, g53_geometry_readback.motion_model);
        }
    }
    if (v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool_readback)) {
        shell_apply_modal_tool_readback(&readback, &modal_tool_readback);
    } else {
        v5_native_readback_set_current_line(&readback, 0);
        v5_native_readback_set_motion_line(&readback, 0);
        v5_native_readback_set_mdi_run_actual(&readback, 0, 0, "");
    }

    v5_main_page_set_native_readback(&g_v5_shell_main_page, &readback);
    (void)v5_settings_page_set_native_readback(&g_v5_shell_settings_page, &readback);
    shell_update_top_status_label();
    return 1;
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
        v5_main_page_set_native_readback(&g_v5_shell_main_page, &readback);
        shell_update_top_status_label();
        return 1;
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
    if (!g_v5_shell_main_page.root) {
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
