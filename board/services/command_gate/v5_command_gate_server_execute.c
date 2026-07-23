#include "v5_command_gate_server_internal.h"
#include "v5_command_gate_server_response.h"
#include "v5_drive_write_window.h"
#include "v5_native_axis_zero_position.h"
#include "v5_native_axis_zero_live.h"
#include "v5_native_first_point.h"
#include "v5_native_home_mapping.h"
#include "v5_native_hal_owner_client.h"
#include "v5_native_rtcp_control.h"
#include "v5_native_work_zero.h"
#include "v5_settings_apply.h"

#include <math.h>
#include <string.h>

void execute_request(const V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcResponseFrame *response)
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
        memset(&native_result, 0, sizeof(native_result));
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
        v5_command_gate_response_copy_text(
            response->command_line,
            sizeof(response->command_line),
            "native_safety.estop_reset");
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
        if (g_axis_slave_mapping_applicable &&
            (!g_axis_slave_mapping_status_available ||
             !g_axis_slave_mapping_valid)) {
            linuxcncrsh_unlock();
            v5_command_gate_response_fill_safety(response);
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            response->executed = 0;
            response->machine_on_requested = 0;
            response->machine_on_status = V5_LINUXCNCRSH_SEND_INVALID;
            v5_command_gate_response_copy_text(
                response->readback_code,
                sizeof(response->readback_code),
                g_machine_on_axis_slave_mapping_invalid_code);
            return;
        }
        status = v5_native_safety_estop_reset_latch(&native_result);
        if (status == V5_NATIVE_SAFETY_SEND_SENT) {
            status = wait_estop_latch_cleared(&native_result, 100U, 10000U);
        }
        if (status == V5_NATIVE_SAFETY_SEND_SENT) {
            v5_command_gate_response_copy_text(
                response->command_line,
                sizeof(response->command_line),
                "native_safety.estop_reset | Set Machine On");
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
        int jog_keepalive_only = 0;
        if (!v5_jog_watchdog_prepare_request(
                &g_jog_watchdog, &g_motion_parameters,
                &request, &jog_keepalive_only,
                jog_code, sizeof(jog_code))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code),
                      jog_code[0] ? jog_code : "JOG_NATIVE_PARAMETERS_REJECTED");
            linuxcncrsh_unlock();
            return;
        }
        if (jog_keepalive_only) {
            snprintf(
                response->command_line,
                sizeof(response->command_line),
                "Jog Keepalive %c",
                request.text_value[0]);
            snprintf(
                jog_code,
                sizeof(jog_code),
                "%s",
                "JOG_KEEPALIVE_REFRESHED");
            status = V5_LINUXCNCRSH_SEND_SENT;
        } else if (request.kind == V5_COMMAND_START) {
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
void execute_settings_axis_commit(
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
    request.runtime_ini_path = g_ini_path;
    request.axis = frame->settings_axis;
    request.axis_index = frame->settings_axis_index;
    request.field_key = frame->settings_field_key;
    request.field_name = frame->settings_field_name;
    request.value_text = frame->settings_value_text;
    request.owner_generation = frame->settings_owner_generation;
    request.readback_token = frame->settings_readback_token;
    linuxcncrsh_lock();
    if (v5_drive_write_window_is_active()) {
        linuxcncrsh_unlock();
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        response->executed = 0;
        v5_command_gate_response_copy_text(
            response->readback_code,
            sizeof(response->readback_code),
            "SETTINGS_AXIS_COMMIT_DRIVE_WINDOW_ACTIVE");
        return;
    }
    ok = v5_settings_apply_commit_axis_value(&request, &result);
    linuxcncrsh_unlock();
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

void execute_settings_axis_zero_live_apply(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response)
{
    V5NativeAxisZeroLiveResult result;
    char reject_reason[64];
    char window_code[64];
    v5_native_axis_zero_live_result_init(&result);
    reject_reason[0] = '\0';
    window_code[0] = '\0';
    if (!v5_command_gate_validate_envelope(
            frame, V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_ZERO_LIVE_APPLY,
            reject_reason, sizeof(reject_reason)) ||
        !frame->text_value[0] ||
        !memchr(frame->settings_axis, '\0', sizeof(frame->settings_axis)) ||
        !frame->settings_axis[0] || frame->settings_axis[1] ||
        frame->index_value < 0 ||
        frame->index_value >= (int32_t)V5_NATIVE_MOTION_HOME_JOINT_COUNT) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(
            response->readback_code, sizeof(response->readback_code),
            reject_reason[0] ? reject_reason : "SETTINGS_AXIS_ZERO_LIVE_BAD_REQUEST");
        return;
    }
    linuxcncrsh_lock();
    if (!v5_drive_write_window_check_owner(
            frame->text_value, window_code, sizeof(window_code))) {
        linuxcncrsh_unlock();
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        v5_command_gate_response_copy_text(
            response->readback_code, sizeof(response->readback_code),
            window_code[0] ? window_code : "DRIVE_WRITE_WINDOW_OWNER_REQUIRED");
        return;
    }
    (void)v5_native_axis_zero_live_apply(
        &g_linuxcncrsh_config,
        g_ini_path,
        g_settings_project_root,
        g_settings_runtime_path,
        g_pulse_contract_path,
        &g_motion_parameters,
        frame->settings_axis[0],
        (unsigned int)frame->index_value,
        frame->axis_value,
        &result);
    linuxcncrsh_unlock();
    v5_command_gate_response_fill_safety(response);
    response->zero_commit_seq = result.commit_seq;
    response->zero_display_verified = result.display_verified ? 1 : 0;
    response->zero_mcs_position = result.mcs_position;
    response->zero_tolerance_units = result.tolerance_units;
    response->zero_previous_mcs_position = result.previous_mcs_position;
    response->settings_restart_pending = result.display_verified ? 1 : 0;
    response->send_status = result.display_verified
        ? V5_COMMAND_GATE_SEND_SENT
        : V5_COMMAND_GATE_SEND_INVALID;
    response->executed = result.display_verified ? 1 : 0;
    v5_command_gate_response_copy_text(
        response->readback_code, sizeof(response->readback_code), result.code);
}
