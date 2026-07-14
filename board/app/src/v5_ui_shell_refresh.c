#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_main_page_home_transaction.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_wcs_status.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_ui_page_cache_registry.h"
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

static int shell_refresh_due(unsigned long long now, unsigned long long *last, unsigned long long period_ns)
{
    if (!last || period_ns == 0ULL) {
        return 0;
    }
    if (*last == 0ULL || now < *last || now - *last >= period_ns) {
        *last = now;
        return 1;
    }
    return 0;
}

static long long shell_quantized_display_value(double value, double scale)
{
    double scaled;
    if (!isfinite(value)) {
        return 0LL;
    }
    scaled = value * scale;
    return (long long)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

static int shell_quantized_values_equal(double left, double right, double scale)
{
    return shell_quantized_display_value(left, scale) == shell_quantized_display_value(right, scale);
}

static int shell_axis_values_equal(const double left[V5_STATUS_AXIS_COUNT],
                                   const double right[V5_STATUS_AXIS_COUNT],
                                   double scale)
{
    unsigned int i;
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!shell_quantized_values_equal(left[i], right[i], scale)) {
            return 0;
        }
    }
    return 1;
}

static int shell_trajectory_equal(const V5UiStatusView *before, const V5UiStatusView *after)
{
    uint32_t i;
    if (before->trajectory_count != after->trajectory_count) {
        return 0;
    }
    for (i = 0U; i < before->trajectory_count && i < V5_STATUS_TRAJECTORY_POINT_COUNT; ++i) {
        if (!shell_axis_values_equal(before->trajectory[i].axis, after->trajectory[i].axis, 1000.0)) {
            return 0;
        }
    }
    return 1;
}

static int shell_status_display_equal(const V5UiStatusView *before, const V5UiStatusView *after)
{
    uint32_t changed_mask;
    if (!before || !after) {
        return 0;
    }
    if (before->valid_mask != after->valid_mask || before->frame_flags != after->frame_flags) {
        return 0;
    }
    changed_mask = before->valid_mask | after->valid_mask;
    if ((changed_mask & V5_STATUS_VALID_MCS) != 0U &&
        !shell_axis_values_equal(before->mcs, after->mcs, 1000.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_MCS) != 0U &&
        !shell_axis_values_equal(before->raw_mcs, after->raw_mcs, 1000.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_CMD_MCS) != 0U &&
        !shell_axis_values_equal(before->cmd_mcs, after->cmd_mcs, 1000.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_CMD_MCS) != 0U &&
        !shell_axis_values_equal(before->raw_cmd_mcs, after->raw_cmd_mcs, 1000.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_TRAJECTORY) != 0U && !shell_trajectory_equal(before, after)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_SPINDLE_SPEED) != 0U &&
        !shell_quantized_values_equal(before->spindle_speed_rpm, after->spindle_speed_rpm, 10.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_LINEAR_VELOCITY) != 0U &&
        !shell_quantized_values_equal(before->linear_velocity_mm_per_min, after->linear_velocity_mm_per_min, 10.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_FEED_OVERRIDE) != 0U &&
        !shell_quantized_values_equal(before->feedrate_override, after->feedrate_override, 10.0)) {
        return 0;
    }
    if ((changed_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) != 0U &&
        !shell_quantized_values_equal(before->spindle_override, after->spindle_override, 10.0)) {
        return 0;
    }
    return 1;
}

static int shell_settings_status_equal(const V5UiStatusView *before, const V5UiStatusView *after)
{
    int before_valid;
    int after_valid;
    if (!before || !after) {
        return 0;
    }
    before_valid = (before->valid_mask & V5_STATUS_VALID_MCS) != 0U;
    after_valid = (after->valid_mask & V5_STATUS_VALID_MCS) != 0U;
    if (before_valid != after_valid) {
        return 0;
    }
    return !before_valid || shell_axis_values_equal(before->mcs, after->mcs, 1000.0);
}

int v5_ui_shell_refresh_once(void)
{
    unsigned long long now;
    unsigned int flags = 0U;
    int main_cache_changed = 0;
    int settings_cache_changed = 0;
    int settings_projection_changed = 0;
    int overlay_active;
    unsigned int dirty_mask;
    unsigned int page;

    if (!g_v5_shell_ui_ready || !g_v5_shell_main_page.root) {
        return 0;
    }
    if (v5_main_page_home_transaction_poll(&g_v5_shell_main_page)) {
        V5CommandGateHomeStatus home_status;
        shell_update_top_status_label();
        if (v5_main_page_home_transaction_status(&home_status)) {
            shell_show_home_failure_popup(&home_status);
        }
    } else if (v5_main_page_home_transaction_active()) {
        shell_update_top_status_label();
    }
    now = shell_monotonic_ns();
    if (shell_refresh_due(now, &g_v5_shell_ui_dynamic_last_refresh_ns, V5_UI_DYNAMIC_REFRESH_NS)) {
        V5UiStatusView before = g_v5_shell_model.status_view;
        (void)v5_ui_model_refresh_status_from_shm(&g_v5_shell_model, V5_STATUS_SHM_PATH);
        if (shell_refresh_modal_line_readback(0)) {
            flags |= V5_MAIN_PAGE_REFRESH_DYNAMIC;
            main_cache_changed = 1;
        }
        if (shell_refresh_operator_error(0)) {
            flags |= V5_MAIN_PAGE_REFRESH_DYNAMIC;
        }
        if (!shell_status_display_equal(&before, &g_v5_shell_model.status_view)) {
            flags |= V5_MAIN_PAGE_REFRESH_DYNAMIC;
            main_cache_changed = 1;
        }
        if (!shell_settings_status_equal(&before, &g_v5_shell_model.status_view)) {
            settings_cache_changed = 1;
            settings_projection_changed = 1;
        }
    }
    if (shell_refresh_due(now, &g_v5_shell_ui_estop_last_refresh_ns, V5_UI_ESTOP_REFRESH_NS)) {
        if (shell_refresh_safety_readback(0)) {
            main_cache_changed = 1;
        }
        flags |= V5_MAIN_PAGE_REFRESH_ESTOP;
    }
    if (shell_refresh_due(now, &g_v5_shell_ui_button_last_refresh_ns, V5_UI_BUTTON_REFRESH_NS)) {
        flags |= V5_MAIN_PAGE_REFRESH_BUTTONS;
    }
    if (shell_refresh_due(now, &g_v5_shell_ui_slow_last_refresh_ns, V5_UI_SLOW_REFRESH_NS)) {
        if (shell_refresh_native_readback(0)) {
            main_cache_changed = 1;
        }
        flags |= V5_MAIN_PAGE_REFRESH_SLOW;
    }

    overlay_active = v5_ui_first_frame_guard_overlay_active();
    dirty_mask = v5_ui_page_cache_affected_mask(
        (unsigned int)g_v5_shell_current_page,
        (unsigned int)V5_SHELL_PAGE_MAIN,
        (unsigned int)V5_SHELL_PAGE_SETTINGS,
        overlay_active,
        main_cache_changed,
        settings_cache_changed);
    for (page = 0U; page < (unsigned int)V5_SHELL_PAGE_COUNT; ++page) {
        if ((dirty_mask & (1U << page)) != 0U) {
            shell_mark_page_cache_dirty((V5ShellPageKind)page);
        }
    }

    if (!overlay_active && flags != 0U) {
        if (g_v5_shell_current_page == V5_SHELL_PAGE_MAIN) {
            (void)v5_main_page_apply_status_flags(&g_v5_shell_main_page, &g_v5_shell_model.status_view, flags);
        }
        if (g_v5_shell_current_page == V5_SHELL_PAGE_SETTINGS &&
            (settings_projection_changed || settings_cache_changed)) {
            (void)v5_settings_page_apply_status(&g_v5_shell_settings_page, &g_v5_shell_model.status_view);
        }
        lv_timer_handler();
    } else if (overlay_active && (flags & V5_MAIN_PAGE_REFRESH_ESTOP) != 0U) {
        /* The emergency button lives above the modal mask and remains live at 10 Hz. */
        lv_timer_handler();
    }
    if (!overlay_active && shell_sync_current_page_cache_if_dirty() < 0) {
        fprintf(stderr, "V5_UI_CACHE_SYNC_FAIL page=%u\n", (unsigned int)g_v5_shell_current_page);
        return 0;
    }
    return 1;
}

#ifdef V5_UI_SHELL_TEST_HOOKS
int v5_ui_shell_test_program_selected_index(void)
{
    return g_v5_shell_program_selected_index;
}

int v5_ui_shell_test_program_loaded(void)
{
    const V5ProgramRuntime *runtime = v5_program_controller_runtime(&g_v5_shell_program_controller);
    return v5_program_runtime_has_open_program(runtime);
}

const char *v5_ui_shell_test_program_loaded_path(void)
{
    const V5ProgramRuntime *runtime = v5_program_controller_runtime(&g_v5_shell_program_controller);
    return v5_program_runtime_source_path(runtime);
}

int v5_ui_shell_test_click_program_name(unsigned int idx)
{
    if (idx >= V5_PROGRAM_ROWS_MAX || !g_v5_shell_program_row_name_labels[idx]) {
        return 0;
    }
    lv_event_send(g_v5_shell_program_row_name_labels[idx], LV_EVENT_CLICKED, 0);
    lv_timer_handler();
    return 1;
}

int v5_ui_shell_test_click_program_edit(void)
{
    if (!g_v5_shell_program_edit_button) {
        return 0;
    }
    lv_event_send(g_v5_shell_program_edit_button, LV_EVENT_CLICKED, 0);
    lv_timer_handler();
    return 1;
}

int v5_ui_shell_test_double_click_main_program_area(void)
{
    if (!g_v5_shell_main_page.program_edit_hit_area) {
        return 0;
    }
    lv_tick_inc(1);
    lv_event_send(g_v5_shell_main_page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
    lv_tick_inc(100);
    lv_event_send(g_v5_shell_main_page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
    lv_timer_handler();
    return 1;
}

int v5_ui_shell_test_current_page_is_mdi(void)
{
    return g_v5_shell_current_page == V5_SHELL_PAGE_MDI;
}

const char *v5_ui_shell_test_mdi_text(void)
{
    return g_v5_shell_mdi_line;
}

const char *v5_ui_shell_test_top_status_text(void)
{
    return g_v5_shell_top_status_label ? lv_label_get_text(g_v5_shell_top_status_label) : "";
}
#endif
