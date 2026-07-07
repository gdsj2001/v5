#ifndef V5_MAIN_PAGE_ACTIONS_H
#define V5_MAIN_PAGE_ACTIONS_H

#include "v5_command_gate.h"
#include "v5_native_readback.h"
#include "v5_program_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5MainPageActionKind {
    V5_MAIN_PAGE_ACTION_NAV_MAIN = 1,
    V5_MAIN_PAGE_ACTION_NAV_TOOL,
    V5_MAIN_PAGE_ACTION_NAV_PROBE,
    V5_MAIN_PAGE_ACTION_NAV_OFFSET,
    V5_MAIN_PAGE_ACTION_NAV_IO,
    V5_MAIN_PAGE_ACTION_NAV_SETTINGS,
    V5_MAIN_PAGE_ACTION_NAV_NETWORK,
    V5_MAIN_PAGE_ACTION_NAV_PROGRAM,
    V5_MAIN_PAGE_ACTION_NAV_MDI,
    V5_MAIN_PAGE_ACTION_SETTINGS_DNA_REGISTER,
    V5_MAIN_PAGE_ACTION_SETTINGS_AUTH_DOWNLOAD,
    V5_MAIN_PAGE_ACTION_SETTINGS_SERVER_DOWNLOAD,
    V5_MAIN_PAGE_ACTION_SETTINGS_SCAN,
    V5_MAIN_PAGE_ACTION_SETTINGS_DRIVE_RESET,
    V5_MAIN_PAGE_ACTION_SETTINGS_READ,
    V5_MAIN_PAGE_ACTION_SETTINGS_FAULT_RESET,
    V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE,
    V5_MAIN_PAGE_ACTION_SETTINGS_RETURN_HOME,
    V5_MAIN_PAGE_ACTION_SETTINGS_SAVE_RETURN,
    V5_MAIN_PAGE_ACTION_START,
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
    V5_MAIN_PAGE_ACTION_AXIS_ALL,
    V5_MAIN_PAGE_ACTION_WORK_ZERO_X,
    V5_MAIN_PAGE_ACTION_G92_CLEAR,
    V5_MAIN_PAGE_ACTION_RTCP_TOGGLE,
    V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_100,
    V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_100,
    V5_MAIN_PAGE_ACTION_JOG_STEP_1,
    V5_MAIN_PAGE_ACTION_JOG_STEP_10,
    V5_MAIN_PAGE_ACTION_JOG_STEP_100,
    V5_MAIN_PAGE_ACTION_JOG_PLUS,
    V5_MAIN_PAGE_ACTION_JOG_MINUS,
    V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_PLUS,
    V5_MAIN_PAGE_ACTION_JOG_CONTINUOUS_MINUS,
    V5_MAIN_PAGE_ACTION_JOG_STOP,
    V5_MAIN_PAGE_ACTION_VIEW_XY,
    V5_MAIN_PAGE_ACTION_VIEW_XZ,
    V5_MAIN_PAGE_ACTION_VIEW_YZ,
    V5_MAIN_PAGE_ACTION_VIEW_3D,
    V5_MAIN_PAGE_ACTION_FIRST_POINT
} V5MainPageActionKind;

typedef enum V5MainPageSelectionSpace {
    V5_MAIN_PAGE_SELECT_NONE = 0,
    V5_MAIN_PAGE_SELECT_MCS,
    V5_MAIN_PAGE_SELECT_WCS
} V5MainPageSelectionSpace;

typedef struct V5MainPageSelection {
    V5MainPageSelectionSpace space;
    char axis;
    int all_axes;
} V5MainPageSelection;

typedef struct V5MainPageActionReport {
    V5MainPageActionKind action;
    int prepared;
    int local_only;
    int executed;
    int send_status;
    int machine_on_requested;
    int machine_on_status;
    int readback_confirmed;
    int pending_readback;
    unsigned int wcs_offsets_epoch;
    int active_wcs;
    char readback_code[64];
    char command_line[256];
    V5CommandRequest request;
    V5CommandPrepared command;
} V5MainPageActionReport;

int v5_main_page_action_prepare(
    V5MainPageActionKind action,
    const V5ProgramRuntime *program_runtime,
    const V5NativeReadback *native_readback,
    const V5MainPageSelection *selection,
    double jog_step,
    V5MainPageActionReport *report);
const char *v5_main_page_action_label(V5MainPageActionKind action);

#ifdef __cplusplus
}
#endif

#endif
