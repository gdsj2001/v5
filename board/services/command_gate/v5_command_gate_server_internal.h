#ifndef V5_COMMAND_GATE_SERVER_INTERNAL_H
#define V5_COMMAND_GATE_SERVER_INTERNAL_H

#include "v5_command_gate_ipc.h"
#include "v5_command_gate_validator.h"
#include "v5_estop_clean_state.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_jog_watchdog.h"
#include "v5_native_home.h"
#include "v5_native_home_runtime_owner.h"
#include "v5_native_motion_parameters.h"
#include "v5_native_safety.h"

#include <pthread.h>
#include <signal.h>
#include <stddef.h>

extern volatile sig_atomic_t g_stop_requested;
extern pthread_mutex_t g_linuxcncrsh_lock;
extern V5LinuxcncrshConfig g_linuxcncrsh_config;
extern char g_ini_path[256];
extern char g_settings_project_root[256];
extern char g_settings_runtime_path[256];
extern char g_pulse_contract_path[256];
extern V5NativeMotionParameters g_motion_parameters;
extern V5JogWatchdog g_jog_watchdog;
extern int g_axis_slave_mapping_status_available;
extern int g_axis_slave_mapping_applicable;
extern int g_axis_slave_mapping_valid;
extern unsigned int g_axis_slave_mapping_generation;
extern char g_axis_slave_mapping_code[64];
extern const char g_machine_on_axis_slave_mapping_invalid_code[];

void load_axis_slave_mapping_status(void);
void publish_home_progress(
    const V5NativeHomeProgress *progress,
    void *user_data);
int begin_home_runtime(
    const V5CommandGateIpcRequestFrame *frame,
    const char *kind,
    V5CommandGateIpcResponseFrame *response);
void copy_home_status(
    V5CommandGateIpcResponseFrame *response,
    const V5NativeHomeRuntimeState *state);
void copy_estop_clean_status(
    V5CommandGateIpcResponseFrame *response,
    const V5EstopCleanStatus *status);
void on_signal(int signo);
void linuxcncrsh_lock(void);
void linuxcncrsh_unlock(void);
void execute_drive_write_window(
    const V5CommandRequest *request,
    V5CommandGateIpcResponseFrame *response);
void estop_clean_linuxcncrsh_lock(void *context);
void estop_clean_linuxcncrsh_unlock(void *context);
int restore_machine_on_after_estop_reset_locked(
    V5NativeSafetyResult *native_result);
int wait_estop_latch_cleared(
    V5NativeSafetyResult *native_result,
    unsigned int attempts,
    unsigned int delay_us);
int power_on_home_gate_accepts(
    const V5CommandRequest *request,
    V5CommandGateIpcResponseFrame *response);

void execute_request(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response);
void execute_settings_axis_commit(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response);
void execute_settings_axis_zero_live_apply(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response);
void handle_frame(
    const V5CommandGateIpcRequestFrame *request,
    V5CommandGateIpcResponseFrame *response);

#endif
