#include "v5_main_page_actions.h"

#include "v5_command_motion.h"
#include "v5_command_override.h"
#include "v5_command_rtcp.h"
#include "v5_command_safety.h"
#include "v5_command_start.h"
#include "v5_command_wcs.h"

#include <string.h>

const char *v5_main_page_action_label(V5MainPageActionKind action)
{
    switch (action) {
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
    case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
        return "Zero X";
    case V5_MAIN_PAGE_ACTION_G92_CLEAR:
        return "G92 Clear";
    case V5_MAIN_PAGE_ACTION_RTCP_ON:
        return "RTCP On";
    case V5_MAIN_PAGE_ACTION_RTCP_OFF:
        return "RTCP Off";
    case V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100:
        return "F 100%";
    case V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100:
        return "S 100%";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
        return "X1";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
        return "X10";
    case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
        return "X100";
    case V5_MAIN_PAGE_ACTION_VIEW_XY:
        return "XY";
    case V5_MAIN_PAGE_ACTION_VIEW_XZ:
        return "XZ";
    case V5_MAIN_PAGE_ACTION_VIEW_YZ:
        return "YZ";
    case V5_MAIN_PAGE_ACTION_VIEW_3D:
        return "3D";
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
    V5MainPageActionReport *report)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    int ok = 0;
    int wcs_index;

    if (!report) {
        return 0;
    }
    memset(report, 0, sizeof(*report));
    memset(&request, 0, sizeof(request));
    memset(&prepared, 0, sizeof(prepared));
    report->action = action;

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
            ok = v5_command_pause_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_RESUME:
            ok = v5_command_resume_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_HOME:
            ok = v5_command_home_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
            ok = v5_command_estop_force_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
            ok = v5_command_estop_reset_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_WORK_ZERO_X:
            ok = v5_command_work_zero_prepare(1, 'X', &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_G92_CLEAR:
            ok = v5_command_g92_clear_prepare(&prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_RTCP_ON:
            ok = v5_command_rtcp_prepare(1, &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_RTCP_OFF:
            ok = v5_command_rtcp_prepare(0, &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100:
            ok = v5_command_feed_override_prepare(100, &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100:
            ok = v5_command_spindle_override_prepare(100, &prepared, &request);
            break;
        case V5_MAIN_PAGE_ACTION_JOG_STEP_1:
            return v5_main_page_local_action_prepare(action, "jog_step", 1.0, report);
        case V5_MAIN_PAGE_ACTION_JOG_STEP_10:
            return v5_main_page_local_action_prepare(action, "jog_step", 10.0, report);
        case V5_MAIN_PAGE_ACTION_JOG_STEP_100:
            return v5_main_page_local_action_prepare(action, "jog_step", 100.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_XY:
            return v5_main_page_local_action_prepare(action, "view_xy", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_XZ:
            return v5_main_page_local_action_prepare(action, "view_xz", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_YZ:
            return v5_main_page_local_action_prepare(action, "view_yz", 0.0, report);
        case V5_MAIN_PAGE_ACTION_VIEW_3D:
            return v5_main_page_local_action_prepare(action, "view_3d", 0.0, report);
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
