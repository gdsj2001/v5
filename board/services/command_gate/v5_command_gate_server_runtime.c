#include "v5_command_gate_server_internal.h"
#include "v5_command_gate_server_response.h"
#include "v5_drive_write_window.h"
#include "v5_native_home_mapping.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void load_axis_slave_mapping_status(void)
{
    V5NativeMotionParameters mapping_parameters;
    char code[sizeof(g_axis_slave_mapping_code)];
    g_axis_slave_mapping_status_available = 1;
    g_axis_slave_mapping_applicable =
        g_motion_parameters.driver_mode == V5_NATIVE_DRIVER_MODE_BUS;
    g_axis_slave_mapping_valid = 1;
    g_axis_slave_mapping_generation = 0U;
    snprintf(
        g_axis_slave_mapping_code,
        sizeof(g_axis_slave_mapping_code),
        "%s",
        g_axis_slave_mapping_applicable
            ? "BUS_AXIS_SLAVE_MAPPING_NOT_CHECKED"
            : "AXIS_SLAVE_MAPPING_NOT_APPLICABLE");
    if (!g_axis_slave_mapping_applicable) {
        return;
    }
    mapping_parameters = g_motion_parameters;
    code[0] = '\0';
    g_axis_slave_mapping_valid = v5_native_home_mapping_load(
        g_settings_project_root,
        &mapping_parameters,
        code,
        sizeof(code));
    g_axis_slave_mapping_generation = g_axis_slave_mapping_valid
        ? mapping_parameters.mapping_generation
        : 0U;
    snprintf(
        g_axis_slave_mapping_code,
        sizeof(g_axis_slave_mapping_code),
        "%s",
        code[0]
            ? code
            : (g_axis_slave_mapping_valid
                ? "BUS_HOME_MAPPING_VALID"
                : "BUS_HOME_MAPPING_INVALID"));
}
void publish_home_progress(const V5NativeHomeProgress *progress, void *user_data)
{
    (void)user_data;
    v5_native_home_runtime_publish(progress);
}

int begin_home_runtime(
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

void copy_home_status(
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

void copy_estop_clean_status(
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

void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}


void linuxcncrsh_lock(void)
{
    (void)pthread_mutex_lock(&g_linuxcncrsh_lock);
}

void linuxcncrsh_unlock(void)
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

void execute_drive_write_window(
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

void estop_clean_linuxcncrsh_lock(void *context)
{
    (void)context;
    linuxcncrsh_lock();
}

void estop_clean_linuxcncrsh_unlock(void *context)
{
    (void)context;
    linuxcncrsh_unlock();
}

int restore_machine_on_after_estop_reset_locked(V5NativeSafetyResult *native_result)
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

int power_on_home_gate_accepts(
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
