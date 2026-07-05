#ifndef V5_MAIN_PAGE_ACTIONS_H
#define V5_MAIN_PAGE_ACTIONS_H

#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5MainPageActionKind {
    V5_MAIN_PAGE_ACTION_PAUSE = 1,
    V5_MAIN_PAGE_ACTION_RESUME = 2,
    V5_MAIN_PAGE_ACTION_ESTOP_FORCE = 3,
    V5_MAIN_PAGE_ACTION_ESTOP_RESET = 4,
    V5_MAIN_PAGE_ACTION_WCS_G54 = 5,
    V5_MAIN_PAGE_ACTION_WCS_G55 = 6,
    V5_MAIN_PAGE_ACTION_WORK_ZERO_X = 7,
    V5_MAIN_PAGE_ACTION_G92_CLEAR = 8,
    V5_MAIN_PAGE_ACTION_RTCP_ON = 9,
    V5_MAIN_PAGE_ACTION_RTCP_OFF = 10,
    V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100 = 11,
    V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100 = 12,
    V5_MAIN_PAGE_ACTION_START = 13
} V5MainPageActionKind;

typedef struct V5MainPageActionReport {
    V5MainPageActionKind action;
    int prepared;
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
