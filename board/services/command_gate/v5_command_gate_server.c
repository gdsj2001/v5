#include "v5_command_gate_ipc.h"
#include "v5_command_gate_server_io.h"
#include "v5_command_gate_server_response.h"
#include "v5_command_gate_validator.h"
#include "v5_estop_clean_state.h"
#include "v5_drive_write_window.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_jog_watchdog.h"
#include "v5_native_axis_zero_position.h"
#include "v5_native_first_point.h"
#include "v5_native_home.h"
#include "v5_native_home_runtime_owner.h"
#include "v5_native_hal_owner_client.h"
#include "v5_native_motion_parameters.h"
#include "v5_native_rtcp_control.h"
#include "v5_native_safety.h"
#include "v5_native_work_zero.h"
#include "v5_process_residency.h"
#include "v5_settings_apply.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define V5_COMMAND_GATE_CLIENT_IO_TIMEOUT_MS 250U

static volatile sig_atomic_t g_stop_requested;
static pthread_mutex_t g_linuxcncrsh_lock = PTHREAD_MUTEX_INITIALIZER;
static V5LinuxcncrshConfig g_linuxcncrsh_config = {"127.0.0.1", 5007U, "EMC", "v5_command_gate", 1000U};
static char g_host[64] = "127.0.0.1";
static char g_password[64] = "EMC";
static char g_ini_path[256];
static char g_settings_project_root[256] = "/opt/8ax/v5";
static char g_settings_runtime_path[256] = "/opt/8ax/phase0_bus5/settings_runtime.json";
static char g_pulse_contract_path[256] = "/opt/8ax/v5/linuxcnc/components/step_ip_v1_5.contract.json";
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = V5_COMMAND_GATE_SOCKET_PATH;
static V5NativeMotionParameters g_motion_parameters;
static V5JogWatchdog g_jog_watchdog;

static int project_native_home_config(
    const V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    unsigned int index;
    unsigned int expected_active_mask = 0U;
    unsigned int commit_seq;
    const unsigned int full_mask = (1U << V5_NATIVE_HOME_JOINT_COUNT) - 1U;
    V5NativeHomeConfigRecord records[V5_NATIVE_HOME_JOINT_COUNT];
    V5NativeHalOwnerResponse response;
    if (!parameters || parameters->driver_mode != V5_NATIVE_DRIVER_MODE_BUS ||
        !parameters->runtime_owner_loaded || !parameters->mapping_generation) {
        snprintf(code, code_cap, "%s", "NATIVE_HOME_BUS_CONFIG_NOT_RESIDENT");
        return 0;
    }
    memset(records, 0, sizeof(records));
    for (index = 0U; index < V5_NATIVE_HOME_JOINT_COUNT; ++index) {
        records[index].joint = index;
        records[index].status_slot = index;
        records[index].slave_position = UINT32_MAX;
    }
    for (index = 0U; index < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++index) {
        const V5NativeMotionAxisParameters *axis = &parameters->axes[index];
        if (!axis->active) continue;
        if (axis->status_slot >= V5_NATIVE_HOME_JOINT_COUNT ||
            !axis->bus_zero_evidence_known ||
            !axis->slave_mapping_known ||
            axis->slave_position >= V5_NATIVE_HOME_JOINT_COUNT ||
            (expected_active_mask & (1U << axis->status_slot))) {
            snprintf(code, code_cap, "NATIVE_HOME_AXIS_%c_CONFIG_FAILED", axis->axis ? axis->axis : '?');
            return 0;
        }
        records[axis->status_slot].active = 1U;
        records[axis->status_slot].axis_code = (unsigned int)(unsigned char)axis->axis;
        records[axis->status_slot].slave_position = axis->slave_position;
        records[axis->status_slot].mapping_generation = parameters->mapping_generation;
        records[axis->status_slot].zero_counts = axis->bus_zero_counts;
        records[axis->status_slot].counts_per_unit = axis->bus_counts_per_unit;
        expected_active_mask |= 1U << axis->status_slot;
    }
    if (expected_active_mask != full_mask ||
        v5_native_hal_owner_exchange(
            V5_NATIVE_HAL_OWNER_OP_HOME_STATUS, 0U, 100U,
            &response) != V5_NATIVE_HAL_OWNER_CLIENT_OK) {
        snprintf(code, code_cap, "%s", "NATIVE_HOME_CONFIG_TABLE_INCOMPLETE");
        return 0;
    }
    commit_seq = response.home_config_commit_seq + 1U;
    if (!commit_seq) commit_seq = 1U;
    for (index = 0U; index < V5_NATIVE_HOME_JOINT_COUNT; ++index) {
        records[index].expected_active_mask = expected_active_mask;
        records[index].commit_seq = commit_seq;
        if (v5_native_hal_owner_stage_home_joint(
                &records[index], index + 1U == V5_NATIVE_HOME_JOINT_COUNT,
                100U, &response) != V5_NATIVE_HAL_OWNER_CLIENT_OK) {
            snprintf(code, code_cap, "NATIVE_HOME_JOINT_%u_CONFIG_FAILED", index);
            return 0;
        }
    }
    if (!response.home_config_readback_valid ||
        response.home_config_mask != expected_active_mask ||
        response.home_config_active_mask != expected_active_mask ||
        response.home_mapping_generation != parameters->mapping_generation ||
        response.home_config_commit_seq != commit_seq ||
        !response.status_home_router_mapping_valid ||
        response.status_home_router_mapping_generation != parameters->mapping_generation ||
        response.status_home_router_active_mask != expected_active_mask ||
        response.status_home_router_commit_seq != commit_seq ||
        response.status_home_router_rejected_commit_seq == commit_seq) {
        snprintf(code, code_cap, "%s", "NATIVE_HOME_CONFIG_READBACK_MISMATCH");
        return 0;
    }
    snprintf(code, code_cap, "%s", "NATIVE_HOME_CONFIG_PROJECTED");
    return 1;
}

static void publish_home_progress(const V5NativeHomeProgress *progress, void *user_data)
{
    (void)user_data;
    v5_native_home_runtime_publish(progress);
}

static int begin_home_runtime(
    const V5CommandGateIpcRequestFrame *frame,
    const char *kind,
    V5CommandGateIpcResponseFrame *response)
{
    if (!frame || !frame->home_run_id || !frame->home_generation ||
        !v5_native_home_runtime_begin(
            (unsigned long long)frame->home_run_id, frame->home_generation, kind)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "HOME_TRANSACTION_INVALID_OR_ACTIVE");
        return 0;
    }
    return 1;
}

static void copy_home_status(
    V5CommandGateIpcResponseFrame *response,
    const V5NativeHomeRuntimeState *state)
{
    const V5NativeHomeProgress *p;
    if (!response || !state) return;
    p = &state->progress;
    response->home_run_id = (uint64_t)p->run_id;
    response->home_generation = p->generation;
    response->home_phase = p->phase;
    response->home_failure_phase = p->failure_phase;
    response->home_current_axis_mask = p->current_axis_mask;
    response->home_active = state->active;
    response->home_terminal = p->terminal;
    response->home_cancelled = p->cancelled;
    response->home_detail_valid = p->detail_valid;
    response->home_actual = p->actual;
    response->home_target = p->target;
    v5_command_gate_response_copy_text(response->home_mode, sizeof(response->home_mode), p->mode);
    v5_command_gate_response_copy_text(response->home_current_axes, sizeof(response->home_current_axes), p->current_axes);
    v5_command_gate_response_copy_text(response->home_direct_reason, sizeof(response->home_direct_reason), p->direct_reason);
}

static void copy_estop_clean_status(
    V5CommandGateIpcResponseFrame *response,
    const V5EstopCleanStatus *status)
{
    if (!response || !status) return;
    response->estop_clean_generation = status->generation;
    response->estop_clean_active = status->active;
    response->estop_clean_terminal = status->terminal;
    response->estop_clean_ok = status->ok;
    v5_command_gate_response_copy_text(
        response->estop_clean_code,
        sizeof(response->estop_clean_code),
        status->code);
}

static void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}


static void linuxcncrsh_lock(void)
{
    (void)pthread_mutex_lock(&g_linuxcncrsh_lock);
}

static void linuxcncrsh_unlock(void)
{
    (void)pthread_mutex_unlock(&g_linuxcncrsh_lock);
}

static int drive_window_read_safety(void *context, V5DriveWriteSafetyActual *actual)
{
    V5NativeSafetyResult native_result;
    (void)context;
    if (!actual || v5_native_safety_read_status(&native_result) != V5_NATIVE_SAFETY_SEND_SENT) {
        return 0;
    }
    actual->safety_estop_known = native_result.safety_estop_known;
    actual->safety_estop_active = native_result.safety_estop_active;
    actual->machine_enable_known = native_result.machine_enable_known;
    actual->machine_enabled = native_result.machine_enabled;
    return 1;
}

static int drive_window_set_machine_off(void *context)
{
    unsigned int attempt;
    (void)context;
    if (v5_linuxcncrsh_send_machine_off_sequence(&g_linuxcncrsh_config) !=
        V5_LINUXCNCRSH_SEND_SENT) {
        return 0;
    }
    for (attempt = 0U; attempt < 40U; ++attempt) {
        V5NativeSafetyResult actual;
        if (v5_native_safety_read_status(&actual) == V5_NATIVE_SAFETY_SEND_SENT &&
            actual.machine_enable_known && !actual.machine_enabled) {
            return 1;
        }
        usleep(25000U);
    }
    return 0;
}

static int drive_window_set_machine_on(void *context)
{
    unsigned int attempt;
    (void)context;
    if (v5_linuxcncrsh_send_machine_on_sequence(&g_linuxcncrsh_config) !=
        V5_LINUXCNCRSH_SEND_SENT) {
        return 0;
    }
    for (attempt = 0U; attempt < 40U; ++attempt) {
        V5NativeSafetyResult actual;
        if (v5_native_safety_read_status(&actual) == V5_NATIVE_SAFETY_SEND_SENT &&
            actual.machine_enable_known && actual.machine_enabled) {
            return 1;
        }
        usleep(25000U);
    }
    return 0;
}

static const V5DriveWriteWindowOps g_drive_window_ops = {
    0,
    drive_window_read_safety,
    drive_window_set_machine_off,
    drive_window_set_machine_on,
};

static void copy_drive_window_result(
    V5CommandGateIpcResponseFrame *response,
    const V5DriveWriteWindowResult *result)
{
    if (!response || !result) {
        return;
    }
    /* Drive-window EXECUTE reuses the existing response fields to keep IPC v4 ABI stable. */
    response->machine_on_requested = result->initial_machine_enabled ? 1 : 0;
    response->machine_enable_known = result->final_machine_enable_known ? 1 : 0;
    response->machine_enabled = result->final_machine_enabled ? 1 : 0;
    v5_command_gate_response_copy_text(
        response->readback_code, sizeof(response->readback_code), result->code);
}

static void execute_drive_write_window(
    const V5CommandRequest *request,
    V5CommandGateIpcResponseFrame *response)
{
    V5DriveWriteWindowResult result;
    int ok = 0;
    if (!request || !response || !request->text_value) {
        return;
    }
    linuxcncrsh_lock();
    if (request->kind == V5_COMMAND_DRIVE_WRITE_BEGIN) {
        ok = v5_drive_write_window_begin(request->text_value, &g_drive_window_ops, &result);
        v5_command_gate_response_copy_text(
            response->command_line, sizeof(response->command_line), "native_drive_write_window.begin");
    } else if (request->kind == V5_COMMAND_DRIVE_WRITE_FINISH) {
        ok = v5_drive_write_window_finish(
            request->text_value, request->enabled_value, &g_drive_window_ops, &result);
        v5_command_gate_response_copy_text(
            response->command_line, sizeof(response->command_line), "native_drive_write_window.finish");
    } else {
        ok = v5_drive_write_window_abort(request->text_value, &g_drive_window_ops, &result);
        v5_command_gate_response_copy_text(
            response->command_line, sizeof(response->command_line), "native_drive_write_window.abort");
    }
    linuxcncrsh_unlock();
    copy_drive_window_result(response, &result);
    response->send_status = ok ? V5_COMMAND_GATE_SEND_SENT : V5_COMMAND_GATE_SEND_INVALID;
    response->executed = ok ? 1 : 0;
}

static void estop_clean_linuxcncrsh_lock(void *context)
{
    (void)context;
    linuxcncrsh_lock();
}

static void estop_clean_linuxcncrsh_unlock(void *context)
{
    (void)context;
    linuxcncrsh_unlock();
}

static int restore_machine_on_after_estop_reset_locked(V5NativeSafetyResult *native_result)
{
    V5LinuxcncrshSendStatus machine_on_status;

    if (native_result) {
        native_result->machine_on_requested = 1;
        native_result->machine_on_status = (int)V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }

    machine_on_status = v5_linuxcncrsh_send_machine_on_after_estop_reset(&g_linuxcncrsh_config);
    if (native_result) {
        native_result->machine_on_status = (int)machine_on_status;
    }
    if (machine_on_status != V5_LINUXCNCRSH_SEND_SENT) {
        int confirm_status = v5_native_safety_wait_reset_confirmed(native_result, 100U, 50000U);
        if (confirm_status == V5_NATIVE_SAFETY_SEND_SENT) {
            if (native_result) {
                native_result->machine_on_status = (int)V5_LINUXCNCRSH_SEND_SENT;
            }
            return V5_NATIVE_SAFETY_SEND_SENT;
        }
        v5_command_gate_response_copy_text(native_result ? native_result->code : 0,
                  native_result ? sizeof(native_result->code) : 0,
                  "NATIVE_SAFETY_MACHINE_ON_FAILED");
        return V5_NATIVE_SAFETY_SEND_IO_ERROR;
    }
    return v5_native_safety_wait_reset_confirmed(native_result, 100U, 50000U);
}

static int power_on_home_gate_accepts(
    const V5CommandRequest *request,
    V5CommandGateIpcResponseFrame *response)
{
    int all_homed = 0;
    if (!request || !response || !v5_command_gate_requires_power_on_home(request->kind)) {
        return 1;
    }
    if (!v5_linuxcncrsh_get_all_homed(
            &g_linuxcncrsh_config,
            g_motion_parameters.active_axis_count,
            &all_homed)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(
            response->readback_code,
            sizeof(response->readback_code),
            "POWER_ON_HOME_STATUS_UNAVAILABLE");
        return 0;
    }
    if (!all_homed) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(
            response->readback_code,
            sizeof(response->readback_code),
            "POWER_ON_HOME_REQUIRED");
        return 0;
    }
    return 1;
}

static void execute_request(const V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcResponseFrame *response)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    int status = V5_COMMAND_GATE_SEND_INVALID;
    int home_runtime_started = 0;
    char reject_reason[64];
    v5_command_gate_response_init(response);
    reject_reason[0] = '\0';
    if (!v5_command_gate_validate_execute_frame(frame, &request, reject_reason, sizeof(reject_reason)) ||
        !v5_command_gate_prepare(&request, &prepared) || !prepared.accepted || !v5_command_gate_response_owner_is_allowed(prepared.owner)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code),
                  reject_reason[0] ? reject_reason : "COMMAND_GATE_REJECTED");
        return;
    }

    if (request.kind == V5_COMMAND_ESTOP_FORCE && strcmp(prepared.owner, "native_safety") == 0) {
        V5NativeSafetyResult native_result;
        V5EstopCleanStatus clean_status;
        unsigned int clean_generation = 0U;
        v5_command_gate_response_copy_text(response->command_line, sizeof(response->command_line), "native_safety.estop_force");
        status = v5_native_safety_estop_force(&native_result);
        if (status == V5_NATIVE_SAFETY_SEND_SENT) {
            (void)v5_estop_clean_state_start(
                &g_linuxcncrsh_config,
                estop_clean_linuxcncrsh_lock,
                estop_clean_linuxcncrsh_unlock,
                0,
                &clean_generation);
            if (clean_generation != 0U &&
                v5_estop_clean_state_snapshot(clean_generation, &clean_status)) {
                copy_estop_clean_status(response, &clean_status);
            }
        }
        v5_command_gate_response_copy_native_safety(response, &native_result);
        response->send_status = (int32_t)status;
        response->executed = status == V5_NATIVE_SAFETY_SEND_SENT ? 1 : 0;
        return;
    }
    if ((request.kind == V5_COMMAND_DRIVE_WRITE_BEGIN ||
         request.kind == V5_COMMAND_DRIVE_WRITE_FINISH ||
         request.kind == V5_COMMAND_DRIVE_WRITE_ABORT) &&
        strcmp(prepared.owner, "native_drive_write_window") == 0) {
        execute_drive_write_window(&request, response);
        return;
    }
    if (request.kind == V5_COMMAND_ESTOP_RESET && strcmp(prepared.owner, "native_safety") == 0) {
        V5NativeSafetyResult native_result;
        V5EstopCleanStatus clean_status;
        if (!v5_estop_clean_state_wait_latest_terminal(1000U, &clean_status) ||
            !clean_status.terminal || !clean_status.ok) {
            copy_estop_clean_status(response, &clean_status);
            v5_command_gate_response_fill_safety(response);
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            response->executed = 0;
            v5_command_gate_response_copy_text(
                response->readback_code,
                sizeof(response->readback_code),
                clean_status.terminal && clean_status.code[0]
                    ? clean_status.code
                    : "ESTOP_CLEAN_PENDING");
            return;
        }
        copy_estop_clean_status(response, &clean_status);
        v5_command_gate_response_copy_text(response->command_line, sizeof(response->command_line), "native_safety.estop_reset | Set Machine On");
        linuxcncrsh_lock();
        if (v5_drive_write_window_blocks_kind(V5_COMMAND_ESTOP_RESET)) {
            linuxcncrsh_unlock();
            v5_command_gate_response_fill_safety(response);
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            response->executed = 0;
            v5_command_gate_response_copy_text(
                response->readback_code, sizeof(response->readback_code), "DRIVE_WRITE_WINDOW_ACTIVE");
            return;
        }
        status = v5_native_safety_estop_reset_latch(&native_result);
        if (status == V5_NATIVE_SAFETY_SEND_SENT) {
            status = restore_machine_on_after_estop_reset_locked(&native_result);
        }
        linuxcncrsh_unlock();
        v5_command_gate_response_copy_native_safety(response, &native_result);
        response->send_status = (int32_t)status;
        response->executed = status == V5_NATIVE_SAFETY_SEND_SENT ? 1 : 0;
        return;
    }
    if (request.kind == V5_COMMAND_RTCP_SET && strcmp(prepared.owner, "native_rtcp_control") == 0) {
        V5NativeRtcpControlResult native_result;
        v5_command_gate_response_copy_text(
            response->command_line,
            sizeof(response->command_line),
            request.enabled_value ? "native_rtcp_control.set ON" : "native_rtcp_control.set OFF");
        status = v5_native_rtcp_control_set(request.enabled_value, &native_result);
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), native_result.code);
        response->send_status = (int32_t)status;
        response->executed = status == V5_NATIVE_RTCP_CONTROL_SEND_SENT ? 1 : 0;
        return;
    }

    linuxcncrsh_lock();
    if (v5_drive_write_window_blocks_kind(request.kind)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        response->executed = 0;
        v5_command_gate_response_copy_text(
            response->readback_code, sizeof(response->readback_code), "DRIVE_WRITE_WINDOW_ACTIVE");
        linuxcncrsh_unlock();
        return;
    }

    if (request.kind == V5_COMMAND_AXIS_ZERO_POSITION &&
        strcmp(prepared.owner, "native_axis_zero_position") == 0) {
        if (!begin_home_runtime(frame, request.mode_value, response)) {
            linuxcncrsh_unlock();
            return;
        }
        home_runtime_started = 1;
    } else if (request.kind == V5_COMMAND_HOME &&
               strcmp(prepared.owner, "native_home_mode_gate") == 0) {
        if (!begin_home_runtime(frame, "all", response)) {
            linuxcncrsh_unlock();
            return;
        }
        home_runtime_started = 1;
    }

    if (!power_on_home_gate_accepts(&request, response)) {
        if (home_runtime_started) {
            v5_native_home_runtime_finish(
                (unsigned long long)frame->home_run_id,
                frame->home_generation,
                V5_NATIVE_HOME_PHASE_FAILED,
                response->readback_code[0] ? response->readback_code : "HOME_PRECONDITION_FAILED",
                0);
        }
        linuxcncrsh_unlock();
        return;
    }
    if (request.kind == V5_COMMAND_FIRST_POINT && strcmp(prepared.owner, "native_first_point") == 0) {
        if (!v5_native_first_point_format_report(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            linuxcncrsh_unlock();
            return;
        }
        status = v5_native_first_point_send(&g_linuxcncrsh_config, &prepared, &request);
    } else if (request.kind == V5_COMMAND_AXIS_ZERO_POSITION &&
               strcmp(prepared.owner, "native_axis_zero_position") == 0) {
        V5NativeAxisZeroPositionResult native_result;
        if (!v5_native_axis_zero_position_format_report(
                &prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_native_home_runtime_finish(
                (unsigned long long)frame->home_run_id,
                frame->home_generation,
                V5_NATIVE_HOME_PHASE_FAILED,
                "AXIS_ZERO_REPORT_INVALID",
                0);
            linuxcncrsh_unlock();
            return;
        }
        status = v5_native_axis_zero_position_send(
            &g_linuxcncrsh_config,
            &g_motion_parameters,
            &prepared,
            &request,
            &native_result,
            (unsigned long long)frame->home_run_id,
            frame->home_generation,
            publish_home_progress,
            0);
        v5_native_home_runtime_finish(
            (unsigned long long)frame->home_run_id,
            frame->home_generation,
            status == V5_LINUXCNCRSH_SEND_SENT ? V5_NATIVE_HOME_PHASE_COMPLETE : V5_NATIVE_HOME_PHASE_FAILED,
            native_result.code,
            0);
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), native_result.code);
    } else if (request.kind == V5_COMMAND_WORK_ZERO && strcmp(prepared.owner, "native_work_zero") == 0) {
        V5NativeWorkZeroResult native_result;
        if (!v5_linuxcncrsh_format_line(
                &prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            linuxcncrsh_unlock();
            return;
        }
        status = v5_native_work_zero_send(
            &g_linuxcncrsh_config,
            &g_motion_parameters,
            &prepared,
            &request,
            &native_result);
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), native_result.code);
    } else if (request.kind == V5_COMMAND_HOME && strcmp(prepared.owner, "native_home_mode_gate") == 0) {
        V5NativeHomeResult native_result;
        v5_command_gate_response_copy_text(
            response->command_line,
            sizeof(response->command_line),
            "ALL_HOME -> native_home_owner");
        status = v5_native_home_send(
            &g_linuxcncrsh_config,
            &native_result,
            (unsigned long long)frame->home_run_id,
            frame->home_generation,
            publish_home_progress,
            0);
        v5_native_home_runtime_finish(
            (unsigned long long)frame->home_run_id,
            frame->home_generation,
            status == V5_LINUXCNCRSH_SEND_SENT ? V5_NATIVE_HOME_PHASE_COMPLETE : V5_NATIVE_HOME_PHASE_FAILED,
            native_result.code,
            0);
        v5_command_gate_response_copy_text(
            response->readback_code,
            sizeof(response->readback_code),
            native_result.code[0] ? native_result.code : "HOME_RESULT_MISSING");
    } else {
        char jog_code[64];
        if (!v5_jog_watchdog_prepare_request(
                &g_jog_watchdog, &g_motion_parameters,
                &request, jog_code, sizeof(jog_code))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code),
                      jog_code[0] ? jog_code : "JOG_NATIVE_PARAMETERS_REJECTED");
            linuxcncrsh_unlock();
            return;
        }
        if (request.kind == V5_COMMAND_START) {
            status = v5_linuxcncrsh_send_start_transaction(
                &g_linuxcncrsh_config,
                &prepared,
                &request,
                response->command_line,
                sizeof(response->command_line));
        } else {
            if (!v5_linuxcncrsh_format_line(
                    &prepared, &request, response->command_line, sizeof(response->command_line))) {
                v5_jog_watchdog_complete_request(&g_jog_watchdog, &request, V5_COMMAND_GATE_SEND_INVALID);
                response->send_status = V5_COMMAND_GATE_SEND_INVALID;
                linuxcncrsh_unlock();
                return;
            }
            status = v5_linuxcncrsh_send_line(&g_linuxcncrsh_config, response->command_line);
        }
        v5_jog_watchdog_complete_request(&g_jog_watchdog, &request, status);
        if (request.kind == V5_COMMAND_JOG_INCREMENT ||
            request.kind == V5_COMMAND_JOG_CONTINUOUS ||
            request.kind == V5_COMMAND_JOG_STOP) {
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), jog_code);
        }
    }
    response->send_status = (int32_t)status;
    response->executed = status == V5_LINUXCNCRSH_SEND_SENT ? 1 : 0;
    linuxcncrsh_unlock();
}

static void execute_settings_axis_commit(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response)
{
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult result;
    int ok;
    v5_command_gate_response_init(response);
    if (!v5_command_gate_response_fixed_text_has_nul(frame->settings_project_root, sizeof(frame->settings_project_root)) ||
        !v5_command_gate_response_fixed_text_has_nul(frame->settings_axis, sizeof(frame->settings_axis)) ||
        !v5_command_gate_response_fixed_text_has_nul(frame->settings_field_key, sizeof(frame->settings_field_key)) ||
        !v5_command_gate_response_fixed_text_has_nul(frame->settings_field_name, sizeof(frame->settings_field_name)) ||
        !v5_command_gate_response_fixed_text_has_nul(frame->settings_value_text, sizeof(frame->settings_value_text)) ||
        !frame->settings_project_root[0] ||
        !frame->settings_axis[0] ||
        !frame->settings_field_key[0] ||
        !frame->settings_field_name[0] ||
        !frame->settings_value_text[0]) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "SETTINGS_AXIS_COMMIT_BAD_REQUEST");
        return;
    }
    memset(&request, 0, sizeof(request));
    request.project_root = frame->settings_project_root;
    request.axis = frame->settings_axis;
    request.axis_index = frame->settings_axis_index;
    request.field_key = frame->settings_field_key;
    request.field_name = frame->settings_field_name;
    request.value_text = frame->settings_value_text;
    request.owner_generation = frame->settings_owner_generation;
    request.readback_token = frame->settings_readback_token;
    ok = v5_settings_apply_commit_axis_value(&request, &result);
    v5_command_gate_response_copy_settings(response, &result);
    if (ok && result.source_readback_confirmed) {
        response->send_status = V5_COMMAND_GATE_SEND_SENT;
        response->executed = 1;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "SETTINGS_AXIS_COMMIT_OK");
    } else {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        response->executed = 0;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code),
                  result.scale_chain.code[0] ? result.scale_chain.code : "SETTINGS_AXIS_COMMIT_REJECTED");
    }
}

static void handle_frame(const V5CommandGateIpcRequestFrame *request, V5CommandGateIpcResponseFrame *response)
{
    char reject_reason[64];
    v5_command_gate_response_init(response);
    reject_reason[0] = '\0';
    if (!request || request->magic != V5_COMMAND_GATE_IPC_MAGIC || request->version != V5_COMMAND_GATE_IPC_VERSION ||
        request->size != (uint32_t)sizeof(*request)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "BAD_ENVELOPE");
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY) {
        if (!v5_command_gate_validate_envelope(request, V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY, reject_reason, sizeof(reject_reason))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), reject_reason);
            return;
        }
        v5_command_gate_response_fill_safety(response);
        response->send_status = (response->safety_estop_known || response->machine_enable_known) ? V5_COMMAND_GATE_SEND_SENT : V5_COMMAND_GATE_SEND_UNAVAILABLE;
        response->executed = 0;
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_PROBE_HOME_STATUS) {
        V5NativeHomeRuntimeState state;
        if (!request->home_run_id || !request->home_generation ||
            !v5_native_home_runtime_snapshot(
                (unsigned long long)request->home_run_id,
                request->home_generation,
                &state)) {
            response->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "HOME_STATUS_NOT_FOUND");
            return;
        }
        copy_home_status(response, &state);
        response->send_status = V5_COMMAND_GATE_SEND_SENT;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "HOME_STATUS_OK");
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_PROBE_ESTOP_CLEAN) {
        V5EstopCleanStatus clean_status;
        if (!v5_command_gate_validate_envelope(
                request,
                V5_COMMAND_GATE_IPC_OP_PROBE_ESTOP_CLEAN,
                reject_reason,
                sizeof(reject_reason))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), reject_reason);
            return;
        }
        if (!v5_estop_clean_state_snapshot(request->estop_clean_generation, &clean_status)) {
            response->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
            v5_command_gate_response_copy_text(
                response->readback_code,
                sizeof(response->readback_code),
                "ESTOP_CLEAN_STATUS_NOT_FOUND");
            return;
        }
        copy_estop_clean_status(response, &clean_status);
        response->send_status = V5_COMMAND_GATE_SEND_SENT;
        v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "ESTOP_CLEAN_STATUS_OK");
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_EXECUTE) {
        execute_request(request, response);
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_COMMIT) {
        execute_settings_axis_commit(request, response);
        return;
    }
    response->send_status = V5_COMMAND_GATE_SEND_INVALID;
    v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "BAD_OPCODE");
}

static void serve_client(int client_fd)
{
    V5CommandGateIpcRequestFrame request;
    V5CommandGateIpcResponseFrame response;
    if (g_stop_requested) {
        return;
    }
    if (!v5_command_gate_server_read_all(
            client_fd, &request, sizeof(request), &g_stop_requested)) {
        return;
    }
    handle_frame(&request, &response);
    if (!v5_command_gate_server_write_all(
            client_fd, &response, sizeof(response), &g_stop_requested)) {
        return;
    }
}

static void *serve_client_thread(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    serve_client(client_fd);
    close(client_fd);
    return 0;
}

static int make_listener(void)
{
    struct sockaddr_un addr;
    int fd;
    mkdir("/run/8ax_v5_product_ui", 0755);
    unlink(g_socket_path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_socket_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    chmod(g_socket_path, 0660);
    if (listen(fd, 16) != 0) {
        close(fd);
        unlink(g_socket_path);
        return -1;
    }
    return fd;
}

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_socket_path, sizeof(g_socket_path), argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_host, sizeof(g_host), argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.port = (unsigned short)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_password, sizeof(g_password), argv[++i]);
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.timeout_ms = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ini") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(g_ini_path, sizeof(g_ini_path), argv[++i]);
        } else if (strcmp(argv[i], "--settings-project-root") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_settings_project_root, sizeof(g_settings_project_root), argv[++i]);
        } else if (strcmp(argv[i], "--settings-runtime") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_settings_runtime_path, sizeof(g_settings_runtime_path), argv[++i]);
        } else if (strcmp(argv[i], "--pulse-contract") == 0 && i + 1 < argc) {
            v5_command_gate_response_copy_text(
                g_pulse_contract_path, sizeof(g_pulse_contract_path), argv[++i]);
        }
    }
    g_linuxcncrsh_config.host = g_host;
    g_linuxcncrsh_config.connect_password = g_password;
    g_linuxcncrsh_config.client_name = "v5_command_gate";
    if (g_linuxcncrsh_config.timeout_ms == 0U) {
        g_linuxcncrsh_config.timeout_ms = 1000U;
    }
}

int main(int argc, char **argv)
{
    int listen_fd;
    char motion_code[64];
    parse_args(argc, argv);
    if (!v5_process_residency_lock("v5_command_gate_server")) {
        return 3;
    }
    if (!v5_native_motion_parameters_load(
            g_ini_path, &g_motion_parameters, motion_code, sizeof(motion_code))) {
        fprintf(stderr, "v5_command_gate_server motion parameter preload failed: %s ini=%s\n",
                motion_code, g_ini_path);
        return 5;
    }
    if (!v5_native_motion_parameters_load_runtime_owner(
            g_settings_project_root,
            g_settings_runtime_path,
            g_pulse_contract_path,
            &g_motion_parameters,
            motion_code,
            sizeof(motion_code))) {
        fprintf(stderr,
                "v5_command_gate_server Home runtime owner unavailable: %s; "
                "dependent Home actions remain fail-closed\n",
                motion_code);
    } else if (!project_native_home_config(&g_motion_parameters, motion_code, sizeof(motion_code))) {
        fprintf(stderr,
                "v5_command_gate_server native Home config projection failed: %s; "
                "ALL_HOME remains fail-closed\n",
                motion_code);
    }
    if (!v5_linuxcncrsh_gate_preconnect(&g_linuxcncrsh_config)) {
        fprintf(stderr, "v5_command_gate_server linuxcncrsh preconnect failed: %s:%u\n",
                g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
        return 4;
    }
    if (!v5_jog_watchdog_start(
            &g_jog_watchdog, &g_linuxcncrsh_config,
            &g_linuxcncrsh_lock, &g_stop_requested)) {
        fprintf(stderr, "v5_command_gate_server Jog watchdog failed to start\n");
        return 7;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    listen_fd = make_listener();
    if (listen_fd < 0) {
        perror("v5_command_gate_server listen");
        return 1;
    }
    printf("v5_command_gate_server running socket=%s linuxcncrsh=%s:%u\n", g_socket_path, g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
    fflush(stdout);
    while (!g_stop_requested) {
        int client_fd = accept(listen_fd, 0, 0);
        pthread_t thread;
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        (void)v5_command_gate_server_set_timeout(client_fd, V5_COMMAND_GATE_CLIENT_IO_TIMEOUT_MS);
        if (pthread_create(&thread, 0, serve_client_thread, (void *)(intptr_t)client_fd) != 0) {
            close(client_fd);
            continue;
        }
        (void)pthread_detach(thread);
    }
    close(listen_fd);
    g_stop_requested = 1;
    v5_jog_watchdog_join(&g_jog_watchdog);
    unlink(g_socket_path);
    return 0;
}
