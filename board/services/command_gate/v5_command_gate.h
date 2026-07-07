#ifndef V5_COMMAND_GATE_H
#define V5_COMMAND_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#define V5_COMMAND_AXIS_COUNT 5u
#define V5_COMMAND_AXIS_X_MASK 0x01u
#define V5_COMMAND_AXIS_Y_MASK 0x02u
#define V5_COMMAND_AXIS_Z_MASK 0x04u
#define V5_COMMAND_AXIS_A_MASK 0x08u
#define V5_COMMAND_AXIS_C_MASK 0x10u

typedef enum V5CommandKind {
    V5_COMMAND_UI_LOCAL = 0,
    V5_COMMAND_PROGRAM_OPEN = 1,
    V5_COMMAND_START,
    V5_COMMAND_MDI_RUN,
    V5_COMMAND_PAUSE,
    V5_COMMAND_RESUME,
    V5_COMMAND_HOME,
    V5_COMMAND_JOG_INCREMENT,
    V5_COMMAND_JOG_CONTINUOUS,
    V5_COMMAND_JOG_STOP,
    V5_COMMAND_ESTOP_FORCE,
    V5_COMMAND_ESTOP_RESET,
    V5_COMMAND_WCS_SELECT,
    V5_COMMAND_WORK_ZERO,
    V5_COMMAND_G92_CLEAR,
    V5_COMMAND_RTCP_SET,
    V5_COMMAND_FEED_OVERRIDE_SET,
    V5_COMMAND_SPINDLE_OVERRIDE_SET,
    V5_COMMAND_FIRST_POINT,
} V5CommandKind;

typedef struct V5CommandRequest {
    V5CommandKind kind;
    int index_value;
    int enabled_value;
    double axis_value;
    double increment_value;
    unsigned int axis_mask;
    double point_axis[V5_COMMAND_AXIS_COUNT];
    const char *text_value;
    const char *secondary_text_value;
    const char *mode_value;
} V5CommandRequest;

typedef struct V5CommandPrepared {
    V5CommandKind kind;
    const char *name;
    const char *owner;
    int accepted;
} V5CommandPrepared;

int v5_command_gate_prepare(const V5CommandRequest *request, V5CommandPrepared *prepared);

#ifdef __cplusplus
}
#endif

#endif
