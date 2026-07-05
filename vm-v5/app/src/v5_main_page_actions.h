#ifndef V5_MAIN_PAGE_ACTIONS_H
#define V5_MAIN_PAGE_ACTIONS_H

#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5MainPageActionKind {
    V5_MAIN_PAGE_ACTION_START = 1,
    V5_MAIN_PAGE_ACTION_PAUSE,
    V5_MAIN_PAGE_ACTION_RESUME,
    V5_MAIN_PAGE_ACTION_HOME,
    V5_MAIN_PAGE_ACTION_ESTOP_FORCE,
    V5_MAIN_PAGE_ACTION_ESTOP_RESET,
    V5_MAIN_PAGE_ACTION_WCS_G54,
    V5_MAIN_PAGE_ACTION_WCS_G55,
    V5_MAIN_PAGE_ACTION_WCS_G56,
    V5_MAIN_PAGE_ACTION_WCS_G57,
    V5_MAIN_PAGE_ACTION_WCS_G58,
    V5_MAIN_PAGE_ACTION_WCS_G59,
    V5_MAIN_PAGE_ACTION_WCS_G591,
    V5_MAIN_PAGE_ACTION_WCS_G592,
    V5_MAIN_PAGE_ACTION_WCS_G593,
    V5_MAIN_PAGE_ACTION_WORK_ZERO_X,
    V5_MAIN_PAGE_ACTION_G92_CLEAR,
    V5_MAIN_PAGE_ACTION_RTCP_ON,
    V5_MAIN_PAGE_ACTION_RTCP_OFF,
    V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100,
    V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100,
    V5_MAIN_PAGE_ACTION_JOG_STEP_1,
    V5_MAIN_PAGE_ACTION_JOG_STEP_10,
    V5_MAIN_PAGE_ACTION_JOG_STEP_100,
    V5_MAIN_PAGE_ACTION_VIEW_XY,
    V5_MAIN_PAGE_ACTION_VIEW_XZ,
    V5_MAIN_PAGE_ACTION_VIEW_YZ,
    V5_MAIN_PAGE_ACTION_VIEW_3D
} V5MainPageActionKind;

typedef struct V5MainPageActionReport {
    V5MainPageActionKind action;
    int prepared;
    int local_only;
    V5CommandRequest request;
    V5CommandPrepared command;
} V5MainPageActionReport;

int v5_main_page_action_prepare(
    V5MainPageActionKind action,
    const V5ProgramRuntime *program_runtime,
    V5MainPageActionReport *report);
const char *v5_main_page_action_label(V5MainPageActionKind action);

#ifdef __cplusplus
}
#endif

#endif
