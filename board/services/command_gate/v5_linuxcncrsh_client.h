#ifndef V5_LINUXCNCRSH_CLIENT_H
#define V5_LINUXCNCRSH_CLIENT_H

#include "v5_command_gate.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5LinuxcncrshSendStatus {
    V5_LINUXCNCRSH_SEND_UNAVAILABLE = 0,
    V5_LINUXCNCRSH_SEND_SENT = 1,
    V5_LINUXCNCRSH_SEND_INVALID = -1,
    V5_LINUXCNCRSH_SEND_IO_ERROR = -2,
} V5LinuxcncrshSendStatus;

typedef struct V5LinuxcncrshConfig {
    const char *host;
    unsigned short port;
    const char *connect_password;
    const char *client_name;
    unsigned int timeout_ms;
} V5LinuxcncrshConfig;

typedef enum V5LinuxcncrshTaskMode {
    V5_LINUXCNCRSH_TASK_MODE_UNKNOWN = 0,
    V5_LINUXCNCRSH_TASK_MODE_MANUAL,
    V5_LINUXCNCRSH_TASK_MODE_AUTO,
    V5_LINUXCNCRSH_TASK_MODE_MDI,
} V5LinuxcncrshTaskMode;

typedef enum V5LinuxcncrshProgramStatus {
    V5_LINUXCNCRSH_PROGRAM_UNKNOWN = 0,
    V5_LINUXCNCRSH_PROGRAM_IDLE,
    V5_LINUXCNCRSH_PROGRAM_RUNNING,
    V5_LINUXCNCRSH_PROGRAM_PAUSED,
} V5LinuxcncrshProgramStatus;

typedef struct V5LinuxcncrshTaskContext {
    V5LinuxcncrshTaskMode mode;
    V5LinuxcncrshProgramStatus program_status;
    char program[384];
} V5LinuxcncrshTaskContext;

enum {
    V5_LINUXCNCRSH_TASK_STATE_ESTOP = 1,
    V5_LINUXCNCRSH_TASK_STATE_ESTOP_RESET = 2,
    V5_LINUXCNCRSH_TASK_STATE_OFF = 3,
    V5_LINUXCNCRSH_TASK_STATE_ON = 4,
    V5_LINUXCNCRSH_TASK_INTERP_IDLE = 1,
    V5_LINUXCNCRSH_TASK_EXEC_DONE = 2,
};

typedef struct V5LinuxcncrshTaskState {
    int state;
    int mode;
    int interp;
    int exec;
    int paused;
    int motion_queue;
    int motion_inpos;
    int current_line;
    int motion_line;
    int read_line;
    int echo;
    unsigned int heartbeat;
    char program[384];
} V5LinuxcncrshTaskState;

enum {
    V5_LINUXCNCRSH_HOME_ACTION_ABORT = 1U << 0,
    V5_LINUXCNCRSH_HOME_ACTION_MANUAL = 1U << 1,
};

typedef enum V5LinuxcncrshHomeEntryStatus {
    V5_LINUXCNCRSH_HOME_ENTRY_READY = 0,
    V5_LINUXCNCRSH_HOME_ENTRY_UNAVAILABLE,
    V5_LINUXCNCRSH_HOME_ENTRY_CONTEXT_UNAVAILABLE,
    V5_LINUXCNCRSH_HOME_ENTRY_ABORT_NOT_CONFIRMED,
    V5_LINUXCNCRSH_HOME_ENTRY_MANUAL_NOT_CONFIRMED,
    V5_LINUXCNCRSH_HOME_ENTRY_JOINT_MODE_NOT_CONFIRMED,
    V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED,
} V5LinuxcncrshHomeEntryStatus;

int v5_linuxcncrsh_format_line(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_format_start_transaction(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_format_all_home(char *out, size_t out_size);
int v5_linuxcncrsh_estop_reset_ready(int probe_ok, int estop_active);
int v5_linuxcncrsh_parse_task_context(
    const char *mode_response,
    const char *program_status_response,
    const char *program_response,
    V5LinuxcncrshTaskContext *context_out);
int v5_linuxcncrsh_parse_task_state_response(
    const char *response,
    V5LinuxcncrshTaskState *state_out);
int v5_linuxcncrsh_task_state_clean_after_estop(
    const V5LinuxcncrshTaskState *state,
    const char *expected_program);
int v5_linuxcncrsh_clean_execution_after_estop(
    const V5LinuxcncrshConfig *config,
    V5LinuxcncrshTaskState *state_out,
    char *code,
    size_t code_cap);
unsigned int v5_linuxcncrsh_home_entry_actions(
    const V5LinuxcncrshTaskContext *context);
int v5_linuxcncrsh_home_entry_context_preserved(
    const V5LinuxcncrshTaskContext *before,
    const V5LinuxcncrshTaskContext *after);
int v5_linuxcncrsh_parse_teleop_enabled_response(
    const char *response,
    int *enabled_out);
int v5_linuxcncrsh_home_entry_joint_mode_ready(
    int probe_ok,
    int teleop_enabled);
V5LinuxcncrshHomeEntryStatus v5_linuxcncrsh_prepare_home_entry(
    const V5LinuxcncrshConfig *config);

int v5_linuxcncrsh_probe_machine(
    const V5LinuxcncrshConfig *config,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_probe_machine_enabled(
    const V5LinuxcncrshConfig *config,
    int *enabled_out,
    char *out,
    size_t out_size);
int v5_native_probe_machine_enabled_actual(int *enabled_out);
int v5_linuxcncrsh_probe_estop(
    const V5LinuxcncrshConfig *config,
    int *active_out,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_probe_active_driver_mode(char *out, size_t out_size);
int v5_linuxcncrsh_gate_preconnect(const V5LinuxcncrshConfig *config);
int v5_linuxcncrsh_format_axis_position_query(
    char axis,
    int relative,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_get_axis_position(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double *position_out);
int v5_linuxcncrsh_get_joint_position(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    double *position_out);
typedef struct V5LinuxcncrshJointState {
    double actual;
    int in_position;
    unsigned int heartbeat;
    int echo_serial;
} V5LinuxcncrshJointState;

int v5_linuxcncrsh_parse_joint_state_response(
    const char *response,
    unsigned int expected_joint,
    V5LinuxcncrshJointState *state_out);
int v5_linuxcncrsh_get_joint_state(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    V5LinuxcncrshJointState *state_out);
int v5_linuxcncrsh_get_joint_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    int *homed_out);
int v5_linuxcncrsh_get_teleop_enabled(
    const V5LinuxcncrshConfig *config,
    int *enabled_out);
int v5_linuxcncrsh_get_all_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int expected_joint_count,
    int *all_homed_out);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_line(
    const V5LinuxcncrshConfig *config,
    const char *line);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_on_sequence(
    const V5LinuxcncrshConfig *config);
V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_off_sequence(
    const V5LinuxcncrshConfig *config);
V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_on_after_estop_reset(
    const V5LinuxcncrshConfig *config);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_prepared(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request);
V5LinuxcncrshSendStatus v5_linuxcncrsh_send_start_transaction(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *command_line,
    size_t command_line_size);

#ifdef __cplusplus
}
#endif

#endif
