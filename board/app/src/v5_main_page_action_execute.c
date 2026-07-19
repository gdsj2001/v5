#include "v5_main_page.h"
#include "v5_main_page_home_transaction.h"

#include "v5_command_gate_ipc.h"
#include "v5_command_override.h"
#include "v5_button_visuals.h"
#include "v5_native_wcs_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_layout_icons.h"
#include "v5_lvgl_clock.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"
#include "v5_remote_metrics.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "v5_main_page_internal.h"

static void main_page_sleep_ms(unsigned int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    (void)nanosleep(&req, 0);
#endif
}

static int main_page_confirm_wcs_readback_once(
    V5MainPage *page,
    V5MainPageActionReport *report,
    V5NativeReadback *confirmed)
{
    V5NativeReadback readback;
    int expected_wcs;
    unsigned int before_epoch;

    if (!page || !report || !confirmed) {
        return 0;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, &readback) ||
        !v5_native_readback_wcs_table_known(&readback)) {
        return 0;
    }
    if (report->request.kind == V5_COMMAND_WCS_SELECT) {
        expected_wcs = report->request.index_value;
        if (expected_wcs >= 0 && expected_wcs < (int)V5_NATIVE_READBACK_WCS_COUNT &&
            readback.wcs_index == expected_wcs) {
            *confirmed = readback;
            return 1;
        }
        return 0;
    }
    if (report->request.kind != V5_COMMAND_WORK_ZERO) {
        return 0;
    }
    expected_wcs = report->request.index_value - 1;
    before_epoch = report->wcs_offsets_epoch;
    if (expected_wcs < 0 || expected_wcs >= (int)V5_NATIVE_READBACK_WCS_COUNT ||
        readback.wcs_index != expected_wcs ||
        !v5_native_readback_wcs_table_known(&readback) ||
        before_epoch == 0U || readback.wcs_offsets_epoch == before_epoch) {
        return 0;
    }
    *confirmed = readback;
    return 1;
}

static int main_page_confirm_wcs_readback_after_send(V5MainPage *page, V5MainPageActionReport *report)
{
    unsigned int attempt;
    V5NativeReadback confirmed;

    if (!page || !report ||
        (report->request.kind != V5_COMMAND_WCS_SELECT && report->request.kind != V5_COMMAND_WORK_ZERO)) {
        return 1;
    }
    report->pending_readback = 1;
    snprintf(report->readback_code, sizeof(report->readback_code), "pending_native_readback");
    for (attempt = 0U; attempt < 12U; ++attempt) {
        main_page_sleep_ms(100U);
        if (main_page_confirm_wcs_readback_once(page, report, &confirmed)) {
            page->native_readback = confirmed;
            report->readback_confirmed = 1;
            report->pending_readback = 0;
            report->wcs_offsets_epoch = confirmed.wcs_offsets_epoch;
            report->active_wcs = confirmed.wcs_index;
            snprintf(report->readback_code, sizeof(report->readback_code), "native_readback_confirmed");
            v5_main_page_internal_update_main_page_wcs_header(page);
            v5_main_page_internal_update_wcs_button_visuals(page);
            return 1;
        }
    }
    report->readback_confirmed = 0;
    report->pending_readback = 1;
    snprintf(report->readback_code, sizeof(report->readback_code), "native_readback_not_confirmed");
    return 0;
}

static int main_page_refresh_safety_readback_from_gate(
    V5MainPage *page,
    int fallback_machine_known,
    int fallback_machine_enabled)
{
    V5NativeReadback readback;
    V5CommandGateResult gate_result;
    int estop_ok;
    int machine_ok;

    if (!page) {
        return 0;
    }
    readback = page->native_readback;
    readback.safety_estop_available = 0;
    readback.machine_enable_available = 0;
    v5_command_gate_result_init(&gate_result);
    (void)v5_command_gate_probe_safety(&gate_result, 500U);
    estop_ok = gate_result.safety_estop_known;
    machine_ok = gate_result.machine_enable_known;
    if (estop_ok) {
        v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
    }
    if (machine_ok) {
        v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
    } else if (fallback_machine_known) {
        v5_native_readback_set_machine_enabled(&readback, fallback_machine_enabled);
        machine_ok = 1;
    }
    page->native_readback = readback;
    v5_main_page_internal_update_estop_button_text(page);
    return estop_ok || machine_ok;
}


static void execute_prepared_command_if_enabled(V5MainPage *page, V5MainPageActionReport *report)
{
    V5CommandGateResult gate_result;
    unsigned int gate_timeout_ms = 1000U;
    int home_button_transaction = 0;

    if (!page || !report || report->local_only || !report->prepared || !report->command.accepted) {
        return;
    }
    if (strcmp(report->command.owner ? report->command.owner : "", "native_linuxcncrsh") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_home_mode_gate") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_safety") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_first_point") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_axis_zero_position") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_work_zero") != 0 &&
        strcmp(report->command.owner ? report->command.owner : "", "native_rtcp_control") != 0) {
        report->send_status = 0;
        return;
    }
    if (!page->command_execution_enabled && report->request.kind != V5_COMMAND_ESTOP_FORCE &&
        report->request.kind != V5_COMMAND_JOG_STOP) {
        report->send_status = 0;
        return;
    }

    v5_command_gate_result_init(&gate_result);
    if (report->request.kind == V5_COMMAND_ESTOP_RESET || report->request.kind == V5_COMMAND_ESTOP_FORCE) {
        gate_timeout_ms = 3000U;
    } else if (report->request.kind == V5_COMMAND_RTCP_SET) {
        gate_timeout_ms = 2500U;
    } else if (report->request.kind == V5_COMMAND_HOME) {
        gate_timeout_ms = 120000U;
    } else if (report->request.kind == V5_COMMAND_AXIS_ZERO_POSITION) {
        gate_timeout_ms = 120000U;
    }
    home_button_transaction = report->action == V5_MAIN_PAGE_ACTION_HOME &&
        (report->request.kind == V5_COMMAND_HOME || report->request.kind == V5_COMMAND_AXIS_ZERO_POSITION);
    if (home_button_transaction) {
        (void)v5_main_page_home_transaction_start(page, report, gate_timeout_ms);
        return;
    }
    if (!v5_command_gate_send_prepared(&report->command, &report->request, &gate_result, gate_timeout_ms)) {
        v5_lvgl_clock_advance();
        report->send_status = gate_result.send_status;
        return;
    }
    report->send_status = gate_result.send_status;
    report->executed = gate_result.executed && gate_result.send_status == V5_COMMAND_GATE_SEND_SENT;
    report->machine_on_status = gate_result.machine_on_status;
    report->machine_on_requested = gate_result.machine_on_requested;
    snprintf(report->command_line, sizeof(report->command_line), "%.*s",
             (int)sizeof(report->command_line) - 1, gate_result.command_line);
    snprintf(report->readback_code, sizeof(report->readback_code), "%.*s",
             (int)sizeof(report->readback_code) - 1, gate_result.readback_code);
    if (strcmp(report->readback_code, "POWER_ON_HOME_REQUIRED") == 0 ||
        strcmp(report->readback_code, "POWER_ON_HOME_STATUS_UNAVAILABLE") == 0 ||
        strcmp(report->readback_code, "HOME_PRECONDITION_ESTOP") == 0 ||
        strcmp(report->readback_code, "HOME_PRECONDITION_DISABLED") == 0) {
        v5_main_page_internal_show_home_precondition_popup(page, report->readback_code);
    }
    v5_lvgl_clock_advance();
    if (report->executed &&
        (report->request.kind == V5_COMMAND_WCS_SELECT || report->request.kind == V5_COMMAND_WORK_ZERO)) {
        report->executed = main_page_confirm_wcs_readback_after_send(page, report);
    }
    if (report->executed) {
        v5_main_page_internal_sync_program_preview_after_execution(page, report->request.kind);
    }
    if (report->executed && strcmp(report->command.owner ? report->command.owner : "", "native_safety") == 0) {
        V5NativeReadback readback = page->native_readback;
        if (report->request.kind == V5_COMMAND_ESTOP_FORCE) {
            v5_main_page_home_transaction_reset_after_estop(page);
        }
        if (gate_result.safety_estop_known) {
            v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
        }
        if (gate_result.machine_enable_known) {
            v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
        }
        page->native_readback = readback;
        v5_main_page_internal_update_estop_button_text(page);
        if (!gate_result.safety_estop_known && !gate_result.machine_enable_known) {
            if (report->request.kind == V5_COMMAND_ESTOP_FORCE) {
                (void)main_page_refresh_safety_readback_from_gate(page, 1, 0);
            } else if (report->request.kind == V5_COMMAND_ESTOP_RESET) {
                (void)main_page_refresh_safety_readback_from_gate(page, 0, 0);
            }
        }
    }
    if (report->executed && report->request.kind == V5_COMMAND_RTCP_SET) {
        if (page->native_readback_refresh_cb) {
            page->native_readback_refresh_cb(page->native_readback_refresh_user_data, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE);
        } else {
            v5_main_page_internal_update_main_page_modal_label(page);
            v5_main_page_internal_update_main_page_state_button_visuals(page);
        }
    }
}


static void apply_local_action_state(V5MainPage *page, const V5MainPageActionReport *report)
{
    if (!page || !report || !report->local_only) {
        return;
    }
    switch (report->action) {
    case V5_MAIN_PAGE_ACTION_AXIS_ALL:
        v5_main_page_select_all_axes(page);
        break;
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        page->jog_step = report->request.axis_value;
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_XY:
        page->view_plane = V5_TOOLPATH_DISPLAY_XY;
        v5_main_page_internal_reset_toolpath_view(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_XZ:
        page->view_plane = V5_TOOLPATH_DISPLAY_XZ;
        v5_main_page_internal_reset_toolpath_view(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_YZ:
        page->view_plane = V5_TOOLPATH_DISPLAY_YZ;
        v5_main_page_internal_reset_toolpath_view(page);
        break;
    case V5_MAIN_PAGE_ACTION_VIEW_3D:
        page->view_plane = V5_TOOLPATH_DISPLAY_3D;
        v5_main_page_internal_reset_toolpath_view(page);
        break;
    default:
        break;
    }
    v5_main_page_internal_update_main_page_state_button_visuals(page);
    if (page->navigation_cb) {
        switch (report->action) {
        case V5_MAIN_PAGE_ACTION_NAV_MAIN:
        case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        case V5_MAIN_PAGE_ACTION_NAV_IO:
        case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        case V5_MAIN_PAGE_ACTION_NAV_MDI:
        case V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT:
            page->navigation_cb(page->navigation_user_data, report->action);
            break;
        default:
            break;
        }
    }
}

static int action_keeps_axis_selection_active(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_HOME:
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
    case V5_MAIN_PAGE_ACTION_JOG_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_MINUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS:
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS:
    case V5_MAIN_PAGE_ACTION_JOG_STOP:
        return 1;
    default:
        return 0;
    }
}

int v5_main_page_trigger_action(V5MainPage *page, V5MainPageActionKind action, V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    const V5ProgramRuntime *runtime = 0;

    if (!page) {
        return 0;
    }
    if (v5_main_page_internal_action_needs_native_readback_refresh(action) && page->native_readback_refresh_cb) {
        page->native_readback_refresh_cb(page->native_readback_refresh_user_data, action);
    }
    if (v5_main_page_internal_action_requires_power_on_home(page, action) &&
        (!v5_native_readback_all_homed_known(&page->native_readback) ||
         !page->native_readback.all_homed)) {
        v5_main_page_internal_block_action_for_power_on_home(page, action, out);
        return 1;
    }
    if (page->program_controller) {
        runtime = v5_program_controller_runtime(page->program_controller);
    }
    if (!v5_main_page_action_prepare(action, runtime, &page->native_readback, &page->selection, page->jog_step, out)) {
        memset(out, 0, sizeof(*out));
        out->action = action;
        page->last_action = *out;
        return 0;
    }
    if (out->request.kind == V5_COMMAND_WORK_ZERO &&
        v5_native_readback_wcs_table_known(&page->native_readback)) {
        out->wcs_offsets_epoch = page->native_readback.wcs_offsets_epoch;
    }
    execute_prepared_command_if_enabled(page, out);
    apply_local_action_state(page, out);
    if (!page->selection.all_axes && action_keeps_axis_selection_active(action)) {
        v5_main_page_internal_reset_selection_idle_timer(page);
    }
    page->last_action = *out;
    return 1;
}

int v5_main_page_trigger_override(
    V5MainPage *page,
    int spindle,
    int percent,
    V5MainPageActionReport *report)
{
    V5MainPageActionReport local_report;
    V5MainPageActionReport *out = report ? report : &local_report;
    V5CommandPrepared prepared;
    V5CommandRequest request;
    int ok;

    if (!page) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    memset(&prepared, 0, sizeof(prepared));
    memset(&request, 0, sizeof(request));
    out->action = spindle
        ? V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_SET
        : V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET;
    ok = spindle
        ? v5_command_spindle_override_prepare(percent, &prepared, &request)
        : v5_command_feed_override_prepare(percent, &prepared, &request);
    if (!ok) {
        page->last_action = *out;
        return 0;
    }
    out->prepared = 1;
    out->request = request;
    out->command = prepared;
    execute_prepared_command_if_enabled(page, out);
    page->last_action = *out;
    return 1;
}
