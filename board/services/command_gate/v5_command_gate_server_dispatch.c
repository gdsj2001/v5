#include "v5_command_gate_server_internal.h"
#include "v5_command_gate_server_response.h"

#include <string.h>

void handle_frame(const V5CommandGateIpcRequestFrame *request, V5CommandGateIpcResponseFrame *response)
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
    if (request->op == V5_COMMAND_GATE_IPC_OP_PROBE_AXIS_SLAVE_MAPPING) {
        if (!v5_command_gate_validate_envelope(
                request,
                V5_COMMAND_GATE_IPC_OP_PROBE_AXIS_SLAVE_MAPPING,
                reject_reason,
                sizeof(reject_reason))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            v5_command_gate_response_copy_text(
                response->readback_code,
                sizeof(response->readback_code),
                reject_reason);
            return;
        }
        response->axis_slave_mapping_status_available =
            g_axis_slave_mapping_status_available ? 1 : 0;
        response->axis_slave_mapping_applicable =
            g_axis_slave_mapping_applicable ? 1 : 0;
        response->axis_slave_mapping_valid =
            g_axis_slave_mapping_valid ? 1 : 0;
        response->axis_slave_mapping_generation =
            g_axis_slave_mapping_generation;
        v5_command_gate_response_copy_text(
            response->axis_slave_mapping_code,
            sizeof(response->axis_slave_mapping_code),
            g_axis_slave_mapping_code);
        response->send_status = g_axis_slave_mapping_status_available
            ? V5_COMMAND_GATE_SEND_SENT
            : V5_COMMAND_GATE_SEND_UNAVAILABLE;
        response->executed = 0;
        v5_command_gate_response_copy_text(
            response->readback_code,
            sizeof(response->readback_code),
            g_axis_slave_mapping_status_available
                ? "AXIS_SLAVE_MAPPING_STATUS_OK"
                : "AXIS_SLAVE_MAPPING_STATUS_UNAVAILABLE");
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
    if (request->op == V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_ZERO_LIVE_APPLY) {
        execute_settings_axis_zero_live_apply(request, response);
        return;
    }
    response->send_status = V5_COMMAND_GATE_SEND_INVALID;
    v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), "BAD_OPCODE");
}
