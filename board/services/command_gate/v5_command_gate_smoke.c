#include "v5_command_gate.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_native_home.h"
#include "v5_native_home_runtime_owner.h"
#include "v5_native_hal_owner_client.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    V5CommandRequest all_home_request;
    V5CommandPrepared all_home_prepared;
    V5LinuxcncrshConfig config;
    V5LinuxcncrshSendStatus send_status;
    char line[384];
    static const V5CommandKind home_required[] = {
        V5_COMMAND_START,
        V5_COMMAND_MDI_RUN,
        V5_COMMAND_RESUME,
        V5_COMMAND_JOG_INCREMENT,
        V5_COMMAND_JOG_CONTINUOUS,
        V5_COMMAND_WORK_ZERO,
        V5_COMMAND_FIRST_POINT,
        V5_COMMAND_AXIS_ZERO_POSITION
    };
    static const V5CommandKind home_not_required[] = {
        V5_COMMAND_HOME,
        V5_COMMAND_PAUSE,
        V5_COMMAND_JOG_STOP,
        V5_COMMAND_ESTOP_FORCE,
        V5_COMMAND_ESTOP_RESET,
        V5_COMMAND_WCS_SELECT,
        V5_COMMAND_RTCP_SET,
        V5_COMMAND_DRIVE_WRITE_BEGIN,
        V5_COMMAND_DRIVE_WRITE_FINISH,
        V5_COMMAND_DRIVE_WRITE_ABORT
    };
    unsigned int i;
    unsigned int wire_target;
    V5NativeWcheckpointSnapshot snapshot;
    V5NativeSafeZeroPlan plan;
    V5NativeMotionAxisParameters rotary;
    V5NativeHomeRuntimeState runtime_state;
    V5NativeHomeProgress progress;
    char owner_code[64];

    for (i = 0U; i < sizeof(home_required) / sizeof(home_required[0]); ++i) {
        if (!v5_command_gate_requires_power_on_home(home_required[i])) {
            return 10;
        }
    }
    for (i = 0U; i < sizeof(home_not_required) / sizeof(home_not_required[0]); ++i) {
        if (v5_command_gate_requires_power_on_home(home_not_required[i])) {
            return 11;
        }
    }

    request.kind = V5_COMMAND_START;
    request.index_value = 0;
    request.enabled_value = 0;
    request.axis_value = 0.0;
    request.text_value = "/opt/8ax/v5/gcode/golden/cc-ac.ngc";

    if (!v5_command_gate_prepare(&request, &prepared)) {
        return 1;
    }
    if (!v5_linuxcncrsh_format_line(&prepared, &request, line, sizeof(line))) {
        return 2;
    }

    config.host = "127.0.0.1";
    config.port = 5007U;
    config.connect_password = "";
    config.client_name = "v5_command_gate_smoke";
    config.timeout_ms = 50U;
    send_status = v5_linuxcncrsh_send_prepared(&config, &prepared, &request);

    if (strcmp(line, "Set Task_Plan_Init\nSet Mode Auto\nSet Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Run 0") != 0) {
        return 4;
    }
    if (!v5_linuxcncrsh_format_start_transaction(
            &prepared,
            &request,
            line,
            sizeof(line)) ||
        strcmp(line, "Set Task_Plan_Init\nSet Mode Auto\nSet Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Run 0") != 0) {
        return 5;
    }
    if (v5_linuxcncrsh_format_start_transaction(
            &prepared, &request, 0, 0U)) {
        return 8;
    }
    if (!v5_linuxcncrsh_estop_reset_ready(1, 0) ||
        v5_linuxcncrsh_estop_reset_ready(1, 1) ||
        v5_linuxcncrsh_estop_reset_ready(0, 0)) {
        return 31;
    }
    {
        V5CommandRequest drive_request;
        V5CommandPrepared drive_prepared;
        memset(&drive_request, 0, sizeof(drive_request));
        drive_request.kind = V5_COMMAND_DRIVE_WRITE_BEGIN;
        drive_request.text_value = "settings-42";
        if (!v5_command_gate_prepare(&drive_request, &drive_prepared) ||
            strcmp(drive_prepared.owner, "native_drive_write_window") != 0) {
            return 32;
        }
    }
    memset(&all_home_request, 0, sizeof(all_home_request));
    all_home_request.kind = V5_COMMAND_HOME;
    if (!v5_command_gate_prepare(&all_home_request, &all_home_prepared) ||
        strcmp(all_home_prepared.owner, "native_home_mode_gate") != 0 ||
        all_home_request.axis_mask != 0U || all_home_request.text_value ||
        all_home_request.secondary_text_value || all_home_request.mode_value ||
        all_home_request.axis_value != 0.0 || all_home_request.increment_value != 0.0 ||
        v5_linuxcncrsh_format_line(
            &all_home_prepared, &all_home_request, line, sizeof(line)) ||
        !v5_linuxcncrsh_format_all_home(line, sizeof(line)) ||
        strcmp(line, "Set Home -1") != 0) {
        return 9;
    }
    {
        V5LinuxcncrshTaskContext before;
        V5LinuxcncrshTaskContext after;
        if (!v5_linuxcncrsh_parse_task_context(
                "Get Mode\nMODE AUTO\n",
                "Get Program_Status\nPROGRAM_STATUS RUNNING\n",
                "Get Program\nPROGRAM /opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
                &before) ||
            v5_linuxcncrsh_home_entry_actions(&before) !=
                (V5_LINUXCNCRSH_HOME_ACTION_ABORT |
                 V5_LINUXCNCRSH_HOME_ACTION_MANUAL) ||
            !v5_linuxcncrsh_parse_task_context(
                "MODE MANUAL\n",
                "PROGRAM_STATUS IDLE\n",
                "PROGRAM /opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
                &after) ||
            !v5_linuxcncrsh_home_entry_context_preserved(&before, &after)) {
            return 37;
        }
        if (!v5_linuxcncrsh_parse_task_context(
                "MODE MANUAL\n",
                "PROGRAM_STATUS IDLE\n",
                "PROGRAM /opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
                &before) ||
            v5_linuxcncrsh_home_entry_actions(&before) != 0U) {
            return 38;
        }
        if (!v5_linuxcncrsh_parse_task_context(
                "MODE MANUAL\n",
                "PROGRAM_STATUS PAUSED\n",
                "PROGRAM /opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
                &before) ||
            v5_linuxcncrsh_home_entry_actions(&before) !=
                V5_LINUXCNCRSH_HOME_ACTION_ABORT) {
            return 39;
        }
        if (!v5_linuxcncrsh_parse_task_context(
                "MODE MANUAL\n",
                "PROGRAM_STATUS IDLE\n",
                "PROGRAM NONE\n",
                &after) ||
            v5_linuxcncrsh_home_entry_context_preserved(&before, &after) ||
            v5_linuxcncrsh_parse_task_context(
                "MODE AUTO\n", "PROGRAM_STATUS IDLE\n", "Get Program\n", &after)) {
            return 40;
        }
        {
            int teleop_enabled = 0;
            if (!v5_linuxcncrsh_parse_teleop_enabled_response(
                    "Get Teleop_Enable\nTELEOP_ENABLE YES\n",
                    &teleop_enabled) ||
                !teleop_enabled ||
                v5_linuxcncrsh_home_entry_joint_mode_ready(1, teleop_enabled) ||
                !v5_linuxcncrsh_parse_teleop_enabled_response(
                    "TELEOP_ENABLE YES\nTELEOP_ENABLE NO\n",
                    &teleop_enabled) ||
                teleop_enabled ||
                !v5_linuxcncrsh_home_entry_joint_mode_ready(1, teleop_enabled) ||
                v5_linuxcncrsh_home_entry_joint_mode_ready(0, 0) ||
                v5_linuxcncrsh_parse_teleop_enabled_response(
                    "TELEOP_ENABLE UNKNOWN\n", &teleop_enabled)) {
                return 41;
            }
        }
    }
    {
        V5LinuxcncrshJointState joint_state;
        if (!v5_linuxcncrsh_parse_joint_state_response(
                "HELLO ACK\nJOINT_STATE 2 0.106700000 YES 42 17\n",
                2U, &joint_state) ||
            fabs(joint_state.actual - 0.1067) > 1.0e-12 ||
            !joint_state.in_position || joint_state.heartbeat != 42U ||
            joint_state.echo_serial != 17 ||
            v5_linuxcncrsh_parse_joint_state_response(
                "JOINT_STATE 1 0.106700000 YES 42 17\n", 2U, &joint_state)) {
            return 36;
        }
    }
    {
        V5LinuxcncrshTaskState task_state;
        const char *program = "/opt/8ax/v5/gcode/golden/cc-ac.ngc";
        if (!v5_linuxcncrsh_parse_task_state_response(
                "Get Task_State\n"
                "TASK_STATE state=1 mode=2 interp=1 exec=2 paused=0 "
                "motion_queue=0 motion_inpos=1 current_line=20 motion_line=20 "
                "read_line=21 echo=44 heartbeat=99 "
                "program=/opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
                &task_state) ||
            task_state.state != V5_LINUXCNCRSH_TASK_STATE_ESTOP ||
            task_state.mode != V5_LINUXCNCRSH_TASK_MODE_AUTO ||
            task_state.interp != V5_LINUXCNCRSH_TASK_INTERP_IDLE ||
            task_state.exec != V5_LINUXCNCRSH_TASK_EXEC_DONE ||
            task_state.paused != 0 || task_state.motion_queue != 0 ||
            task_state.motion_inpos != 1 || task_state.current_line != 20 ||
            task_state.motion_line != 20 || task_state.read_line != 21 ||
            task_state.echo != 44 || task_state.heartbeat != 99U ||
            strcmp(task_state.program, program) != 0 ||
            !v5_linuxcncrsh_task_state_clean_after_estop(
                &task_state, program)) {
            return 90;
        }
        task_state.motion_queue = 1;
        if (v5_linuxcncrsh_task_state_clean_after_estop(
                &task_state, program)) {
            return 91;
        }
        task_state.motion_queue = 0;
        task_state.motion_inpos = 0;
        if (v5_linuxcncrsh_task_state_clean_after_estop(
                &task_state, program)) {
            return 92;
        }
        task_state.motion_inpos = 1;
        task_state.state = V5_LINUXCNCRSH_TASK_STATE_ON;
        if (v5_linuxcncrsh_task_state_clean_after_estop(
                &task_state, program)) {
            return 93;
        }
        task_state.state = V5_LINUXCNCRSH_TASK_STATE_ESTOP;
        if (v5_linuxcncrsh_task_state_clean_after_estop(
                &task_state, "/opt/8ax/v5/gcode/golden/cc-bc.ngc") ||
            v5_linuxcncrsh_parse_task_state_response(
                "TASK_STATE state=1 mode=2 interp=1 exec=2 paused=0 "
                "motion_queue=0 motion_inpos=1 current_line=20\n",
                &task_state)) {
            return 94;
        }
    }
    if (!v5_native_hal_owner_request_target(V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS, 0U, &wire_target) || wire_target != 0U ||
        !v5_native_hal_owner_request_target(V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS, 1U, &wire_target) || wire_target != 1U ||
        !v5_native_hal_owner_request_target(V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS, 2U, &wire_target) || wire_target != 2U ||
        v5_native_hal_owner_request_target(V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS, 3U, &wire_target)) {
        return 17;
    }
    memset(&rotary, 0, sizeof(rotary));
    rotary.axis = 'C';
    rotary.status_slot = 4U;
    rotary.bus_zero_counts = 24.0;
    rotary.bus_counts_per_unit = 1000.0;
    rotary.positioning_resolution_units = 0.001;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1;
    snapshot.generation = 7U;
    snapshot.base_counts = 360000;
    if (!v5_native_home_wcheckpoint_bind_runtime_actual(
            &rotary, -2159.954, &snapshot, owner_code, sizeof(owner_code)) ||
        snapshot.logical_counts != -1799954 || snapshot.runtime_counts != -2159954) {
        return 24;
    }
    if (v5_native_home_wcheckpoint_bind_runtime_actual(
            &rotary, -2159.9544, &snapshot, owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "HOME_WCHECKPOINT_RUNTIME_ACTUAL_INVALID") != 0) {
        return 25;
    }
    if (!v5_native_home_safe_zero_plan(&rotary, &snapshot, &plan, owner_code, sizeof(owner_code)) ||
        plan.delta_counts != -22 || plan.logical_start_counts != -1799954 ||
        plan.logical_target_counts != -1799976 ||
        plan.runtime_target_counts != -2159976) {
        return 18;
    }
    snapshot.generation = 8U;
    snapshot.base_counts = 720000;
    snapshot.logical_counts = -1799976;
    snapshot.runtime_counts = -2519976;
    if (!v5_native_home_safe_zero_remap(&plan, &snapshot, owner_code, sizeof(owner_code)) ||
        plan.logical_start_counts != -1799954 ||
        plan.logical_target_counts != -1799976 || plan.runtime_target_counts != -2519976 ||
        !v5_native_home_safe_zero_arrived(&plan, &snapshot, 0, owner_code, sizeof(owner_code))) {
        return 19;
    }
    snapshot.generation = 9U;
    snapshot.logical_counts = -1799975;
    snapshot.base_counts = 720000;
    snapshot.runtime_counts = -2519975;
    if (!v5_native_home_safe_zero_plan(
            &rotary, &snapshot, &plan, owner_code, sizeof(owner_code)) ||
        plan.delta_counts != -1 || plan.logical_start_counts != -1799975 ||
        plan.logical_target_counts != -1799976 ||
        v5_native_home_safe_zero_arrived(
            &plan, &snapshot, 0, owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "HOME_SAFE_ZERO_LOGICAL_TARGET_COUNT_MISMATCH") != 0) {
        return 40;
    }
    snapshot.logical_counts = -1799976;
    snapshot.runtime_counts = -2519976;
    if (!v5_native_home_safe_zero_arrived(
            &plan, &snapshot, 0, owner_code, sizeof(owner_code))) return 40;
    snapshot.logical_counts = -1439976;
    snapshot.runtime_counts = -2159976;
    if (v5_native_home_safe_zero_arrived(&plan, &snapshot, 0, owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "HOME_SAFE_ZERO_LOGICAL_TARGET_COUNT_MISMATCH") != 0) {
        return 23;
    }
    if (!v5_native_home_runtime_begin(0x1234ULL, 9U, "all")) return 20;
    memset(&progress, 0, sizeof(progress));
    progress.run_id = 0x1234ULL;
    progress.generation = 9U;
    progress.phase = V5_NATIVE_HOME_PHASE_HOMED_SYNC;
    snprintf(progress.current_axes, sizeof(progress.current_axes), "%s", "ALL");
    v5_native_home_runtime_publish(&progress);
    if (!v5_native_home_runtime_snapshot(0x1234ULL, 9U, &runtime_state) ||
        runtime_state.progress.current_axis_mask != 0U ||
        strcmp(runtime_state.progress.current_axes, "ALL") != 0) return 22;
    v5_native_home_runtime_finish(0x1234ULL, 9U, V5_NATIVE_HOME_PHASE_COMPLETE, "HOME_OK", 0);

    printf(
        "v5 command gate prepared: kind=%d name=%s owner=%s accepted=%d line=%s send_status=%d\n",
        (int)prepared.kind,
        prepared.name,
        prepared.owner,
        prepared.accepted,
        line,
        (int)send_status);
    return (prepared.accepted && send_status != V5_LINUXCNCRSH_SEND_SENT) ? 0 : 3;
}
