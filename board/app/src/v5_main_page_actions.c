#include "v5_main_page_actions.h"

#include "v5_command_first_point.h"
#include "v5_command_motion.h"
#include "v5_command_rtcp.h"
#include "v5_command_safety.h"
#include "v5_command_start.h"
#include "v5_command_wcs.h"

#include <ctype.h>
#include <string.h>

const char *v5_main_page_action_label(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_NAV_MAIN:
        return "Main";
    case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        return "Tool";
    case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        return "Probe";
    case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        return "Offset";
    case V5_MAIN_PAGE_ACTION_NAV_IO:
        return "IO";
    case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        return "Settings";
    case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        return "Network";
    case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        return "Program";
    case V5_MAIN_PAGE_ACTION_NAV_MDI:
        return "MDI";
    case V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER:
        return "DNA Register";
    case V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD:
        return "Auth Download";
    case V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD:
        return "Server Download";
    case V5_MAIN_PAGE_ACTION_SETTINGS_SCAN:
        return "Scan Drives";
    case V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET:
        return "Drive Reset";
    case V5_MAIN_PAGE_ACTION_SETTINGS_READ:
        return "Drive Read";
    case V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET:
        return "Fault Reset";
    case V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE:
        return "Set Drive";
    case V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN:
        return "Save Restart";
    case V5_MAIN_PAGE_ACTION_START:
        return "Start";
    case V5_MAIN_PAGE_ACTION_PAUSE:
        return "Pause";
    case V5_MAIN_PAGE_ACTION_RESUME:
        return "Resume";
    case V5_MAIN_PAGE_ACTION_HOME:
        return "Home";
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
        return "Estop";
    case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
        return "Estop Reset";
    case V5_MAIN_PAGE_ACTION_WCS_G54:
        return "G54";
    case V5_MAIN_PAGE_ACTION_WCS_G55:
        return "G55";
    case V5_MAIN_PAGE_ACTION_WCS_G56:
        return "G56";
    case V5_MAIN_PAGE_ACTION_WCS_G57:
        return "G57";
    case V5_MAIN_PAGE_ACTION_WCS_G58:
        return "G58";
    case V5_MAIN_PAGE_ACTION_WCS_G59:
        return "G59";
    case V5_MAIN_PAGE_ACTION_WCS_G591:
        return "G59.1";
    case V5_MAIN_PAGE_ACTION_WCS_G592:
        return "G59.2";
    case V5_MAIN_PAGE_ACTION_WCS_G593:
        return "G59.3";
    case V5_MAIN_PAGE_ACTION_AXIS_ALL:
        return "All Axes";
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
        return "Zero X";
    case V5_MAIN_PAGE_ACTION_G92_CLEAR:
        return "G92 Clear";
    case V5_MAIN_PAGE_ACTION_RTCP_TOGGLE:
        return "RTCP";
    case V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET:
        return "Feed Override";
    case V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_SET:
        return "Spindle Override";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
        return "X1";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
        return "X10";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        return "X100";
    case V5_MAIN_PAGE_ACTION_JOG_PLUS:
        return "Jog+";
    case V5_MAIN_PAGE_ACTION_JOG_MINUS:
        return "Jog-";
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS:
        return "Jog Hold+";
    case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS:
        return "Jog Hold-";
    case V5_MAIN_PAGE_ACTION_JOG_STOP:
        return "Jog Stop";
    case V5_MAIN_PAGE_ACTION_VIEW_XY:
        return "XY";
    case V5_MAIN_PAGE_ACTION_VIEW_XZ:
        return "XZ";
    case V5_MAIN_PAGE_ACTION_VIEW_YZ:
        return "YZ";
    case V5_MAIN_PAGE_ACTION_VIEW_3D:
        return "3D";
    case V5_MAIN_PAGE_ACTION_FIRST_POINT:
        return "First Pt";
    case V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT:
        return "MDI Edit";
    default:
        return "Unknown";
    }
}

static int v5_main_page_local_action_prepare(
    V5MainPageActionKind action,
    const char *name,
    double value,
    V5MainPageActionReport *report)
{
    report->prepared = 1;
    report->local_only = 1;
    report->request.kind = V5_COMMAND_UI_LOCAL;
    report->request.axis_value = value;
    report->command.kind = V5_COMMAND_UI_LOCAL;
    report->command.name = name;
    report->command.owner = "ui_local";
    report->command.accepted = 1;
    report->action = action;
    return 1;
}


static int selection_single_axis(const V5MainPageSelection *selection, char *axis_out)
{
    char axis;
    if (!selection || selection->all_axes ||
        (selection->space != V5_MAIN_PAGE_SELECT_MCS && selection->space != V5_MAIN_PAGE_SELECT_WCS)) {
        return 0;
    }
    axis = (char)toupper((unsigned char)selection->axis);
    if (axis != 'X' && axis != 'Y' && axis != 'Z' && axis != 'A' && axis != 'B' && axis != 'C') {
        return 0;
    }
    if (axis_out) {
        *axis_out = axis;
    }
    return 1;
}

static int native_readback_requests_estop_reset(const V5NativeReadback *readback)
{
    if (v5_native_readback_safety_estop_known(readback) && readback->safety_estop_active) {
        return 1;
    }
    if (v5_native_readback_machine_enable_known(readback) && !readback->machine_enabled) {
        return 1;
    }
    return 0;
}

static int v5_main_page_wcs_index_for_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_WCS_G54:
        return 0;
    case V5_MAIN_PAGE_ACTION_WCS_G55:
        return 1;
    case V5_MAIN_PAGE_ACTION_WCS_G56:
        return 2;
    case V5_MAIN_PAGE_ACTION_WCS_G57:
        return 3;
    case V5_MAIN_PAGE_ACTION_WCS_G58:
        return 4;
    case V5_MAIN_PAGE_ACTION_WCS_G59:
        return 5;
    case V5_MAIN_PAGE_ACTION_WCS_G591:
        return 6;
    case V5_MAIN_PAGE_ACTION_WCS_G592:
        return 7;
    case V5_MAIN_PAGE_ACTION_WCS_G593:
        return 8;
    default:
        return -1;
    }
}

int v5_main_page_action_prepare(
    V5MainPageActionKind action,
    const V5ProgramRuntime *program_runtime,
    const V5NativeReadback *native_readback,
    const V5MainPageSelection *selection,
    double jog_step,
    V5MainPageActionReport *report)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    int ok = 0;
    int wcs_index;
    char selected_axis = 0;

    if (!report) {
        return 0;
    }
    memset(report, 0, sizeof(*report));
    memset(&request, 0, sizeof(request));
    memset(&prepared, 0, sizeof(prepared));
    report->action = action;

    switch (action) {
    case V5_MAIN_PAGE_ACTION_NAV_MAIN:
        return v5_main_page_local_action_prepare(action, "nav_main", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        return v5_main_page_local_action_prepare(action, "nav_tool", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        return v5_main_page_local_action_prepare(action, "nav_probe", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        return v5_main_page_local_action_prepare(action, "nav_offset", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_IO:
        return v5_main_page_local_action_prepare(action, "nav_io", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        return v5_main_page_local_action_prepare(action, "nav_settings", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        return v5_main_page_local_action_prepare(action, "nav_network", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        return v5_main_page_local_action_prepare(action, "nav_program", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_MDI:
        return v5_main_page_local_action_prepare(action, "nav_mdi", 0.0, report);
    case V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT:
        return v5_main_page_local_action_prepare(action, "nav_mdi_edit", 0.0, report);
    case V5_MAIN_PAGE_ACTION_AXIS_ALL:
        return v5_main_page_local_action_prepare(action, "axis_all", 0.0, report);
    default:
        break;
    }

    wcs_index = v5_main_page_wcs_index_for_action(action);
    if (wcs_index >= 0) {
        ok = v5_command_wcs_select_prepare(wcs_index, &prepared, &request);
    } else {
        switch (action) {
        case V5_MAIN_PAGE_ACTION_START:
            ok = v5_program_runtime_prepare_start(program_runtime, &request);
            if (ok) {
                ok = v5_command_start_prepare(program_runtime, &prepared);
            }
            break;
        case V5_MAIN_PAGE_ACTION_PAUSE:
            if (v5_native_readback_interpreter_known(native_readback)) {
                ok = native_readback->interpreter_paused
                    ? v5_command_resume_prepare(&prepared, &request)
                    : v5_command_pause_prepare(&prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_RESUME:
            ok = v5_command_resume_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_HOME:
            if (selection && selection->space == V5_MAIN_PAGE_SELECT_MCS && selection->all_axes) {
                ok = v5_command_home_prepare(&prepared, &request);
            } else if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_axis_zero_position_prepare(
                    selected_axis,
                    selection->space == V5_MAIN_PAGE_SELECT_WCS ? "wcs" : "mcs",
                    &prepared,
                    &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
            if (native_readback_requests_estop_reset(native_readback)) {
                ok = v5_command_estop_reset_prepare(&prepared, &request);
            } else {
                ok = v5_command_estop_force_prepare(&prepared, &request);
            }
            break;
        case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
            ok = v5_command_estop_reset_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
            if (selection_single_axis(selection, &selected_axis) &&
                native_readback && v5_native_readback_wcs_table_known(native_readback)) {
                ok = v5_command_work_zero_prepare(native_readback->wcs_index, selected_axis, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_G92_CLEAR:
            ok = v5_command_g92_clear_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_RTCP_TOGGLE:
            ok = v5_command_rtcp_toggle_prepare(native_readback, &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
            return v5_main_page_local_action_prepare(action, "jog_step", 0.001, report);
        case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
            return v5_main_page_local_action_prepare(action, "jog_step", 0.01, report);
        case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
            return v5_main_page_local_action_prepare(action, "jog_step", 0.1, report);
        case V5_MAIN_PAGE_ACTION_JOG_PLUS:
            if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_jog_increment_prepare(selected_axis, jog_step, 1, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_JOG_MINUS:
            if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_jog_increment_prepare(selected_axis, jog_step, 0, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS:
            if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_jog_continuous_prepare(selected_axis, 1, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS:
            if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_jog_continuous_prepare(selected_axis, 0, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_JOG_STOP:
            if (selection_single_axis(selection, &selected_axis)) {
                ok = v5_command_jog_stop_prepare(selected_axis, &prepared, &request);
            } else {
                ok = 0;
            }
            break;
        case V5_MAIN_PAGE_ACTION_VIEW_XY:
            return v5_main_page_local_action_prepare(action, "view_xy", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_XZ:
            return v5_main_page_local_action_prepare(action, "view_xz", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_YZ:
            return v5_main_page_local_action_prepare(action, "view_yz", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_3D:
            return v5_main_page_local_action_prepare(action, "view_3d", 0.0, report);
        case V5_MAIN_PAGE_ACTION_FIRST_POINT:
            ok = v5_command_first_point_prepare(program_runtime, &prepared, &request);
            break;
        default:
            ok = 0;
            break;
        }
    }

    report->prepared = ok;
    if (ok) {
        report->request = request;
        report->command = prepared;
    }
    return ok;
}
