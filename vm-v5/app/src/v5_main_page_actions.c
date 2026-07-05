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
    case V5_MAIN_PAGE_ACTION_PAUSE:
        return "Pause";
    case V5_MAIN_PAGE_ACTION_RESUME:
        return "Resume";
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
        return "Estop";
    case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
        return "Estop Reset";
    case V5_MAIN_PAGE_ACTION_WCS_G54:
        return "G54";
    case V5_MAIN_PAGE_ACTION_WCS_G55:
        return "G55";
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
    case V5_MAIN_PAGE_ACTION_START:
        return "Start";
    default:
        return "Unknown";
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

    if (!report) {
        return 0;
    }
    memset(report, 0, sizeof(*report));
    memset(&request, 0, sizeof(request));
    memset(&prepared, 0, sizeof(prepared));
    report->action = action;

    switch (action) {
    case V5_MAIN_PAGE_ACTION_PAUSE:
        ok = v5_command_pause_prepare(&prepared, &request);
        break;
    case V5_MAIN_PAGE_ACTION_RESUME:
        ok = v5_command_resume_prepare(&prepared, &request);
        break;
    case V5_MAIN_PAGE_ACTION_ESTOP_FORCE:
        ok = v5_command_estop_force_prepare(&prepared, &request);
        break;
    case V5_MAIN_PAGE_ACTION_ESTOP_RESET:
        ok = v5_command_estop_reset_prepare(&prepared, &request);
        break;
    case V5_MAIN_PAGE_ACTION_WCS_G54:
        ok = v5_command_wcs_select_prepare(0, &prepared, &request);
        break;
    case V5_MAIN_PAGE_ACTION_WCS_G55:
        ok = v5_command_wcs_select_prepare(1, &prepared, &request);
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
    case V5_MAIN_PAGE_ACTION_START:
        ok = v5_program_runtime_prepare_start(program_runtime, &request);
        if (ok) {
            ok = v5_command_start_prepare(program_runtime, &prepared);
        }
        break;
    default:
        ok = 0;
        break;
    }

    report->prepared = ok;
    if (ok) {
        report->request = request;
        report->command = prepared;
    }
    return ok;
}
