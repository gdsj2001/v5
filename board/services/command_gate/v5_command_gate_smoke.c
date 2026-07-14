#include "v5_command_gate.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_native_home.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    V5LinuxcncrshConfig config;
    V5LinuxcncrshSendStatus send_status;
    V5NativeMotionAxisParameters axis;
    double still_previous[5] = {0.0, 10.0, -20.0, 45.0, 359.999};
    double still_current[5] = {0.0005, 9.9995, -20.0005, 45.0005, 359.9995};
    double moving_current[5] = {0.0, 10.0, -19.998, 45.0, 359.999};
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
        V5_COMMAND_RTCP_SET
    };
    unsigned int i;

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

    if (strcmp(line, "Set Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Mode Auto\nSet Run 0") != 0) {
        return 4;
    }
    if (!v5_linuxcncrsh_format_start_transaction(
            &prepared,
            &request,
            "Get Program\nPROGRAM /opt/8ax/v5/gcode/golden/cc-ac.ngc\n",
            line,
            sizeof(line)) ||
        strcmp(line, "Set Mode Auto\nSet Run 0") != 0) {
        return 5;
    }
    if (!v5_linuxcncrsh_format_start_transaction(
            &prepared,
            &request,
            "Get Program\nPROGRAM NONE\n",
            line,
            sizeof(line)) ||
        strcmp(line, "Set Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Mode Auto\nSet Run 0") != 0) {
        return 6;
    }
    if (!v5_linuxcncrsh_format_start_transaction(
            &prepared,
            &request,
            "Get Program\nPROGRAM /opt/8ax/v5/gcode/golden/cc-bc.ngc\n",
            line,
            sizeof(line)) ||
        strcmp(line, "Set Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Mode Auto\nSet Run 0") != 0) {
        return 7;
    }
    if (v5_linuxcncrsh_format_start_transaction(
            &prepared, &request, "Get Program\n", line, sizeof(line))) {
        return 8;
    }
    memset(&axis, 0, sizeof(axis));
    axis.axis = 'C';
    axis.status_slot = 4U;
    axis.max_velocity = 833.333333333;
    if (!v5_native_home_format_increment(&axis, -3599.971, line, sizeof(line)) ||
        strcmp(line, "Set Jog_Incr 4 -50000.000000 3599.971000") != 0) {
        return 9;
    }
    if (!v5_native_home_format_increment(&axis, 1.0, line, sizeof(line)) ||
        strcmp(line, "Set Jog_Incr 4 50000.000000 1.000000") != 0) {
        return 12;
    }
    if (fabs(v5_native_home_target_delta('C', -3599.999, 0.0) - (-0.001)) > 1.0e-6 ||
        fabs(v5_native_home_target_delta('C', 359.999, 0.0) - 0.001) > 1.0e-6 ||
        fabs(v5_native_home_target_delta('C', -1799.976, 0.024)) > 1.0e-6 ||
        fabs(v5_native_home_target_delta('X', -12.5, 0.0) - 12.5) > 1.0e-9) {
        return 16;
    }
    if (v5_native_home_joint_needs_sync(1, 1) != 0 ||
        v5_native_home_joint_needs_sync(1, 0) != 1 ||
        v5_native_home_joint_needs_sync(0, 0) != -1 ||
        v5_native_home_joint_needs_sync(0, 1) != -1) {
        return 13;
    }
    if (!v5_native_home_positions_still(still_previous, still_current, 5U) ||
        v5_native_home_positions_still(still_previous, moving_current, 5U) ||
        v5_native_home_positions_still(still_previous, still_current, 0U)) {
        return 14;
    }
    if (strcmp(v5_native_home_safety_reject_code(1, 1, 1, 0), "HOME_PRECONDITION_ESTOP") != 0 ||
        strcmp(v5_native_home_safety_reject_code(1, 0, 1, 0), "HOME_PRECONDITION_DISABLED") != 0 ||
        v5_native_home_safety_reject_code(1, 0, 1, 1) != 0 ||
        v5_native_home_safety_reject_code(0, 0, 0, 0) != 0) {
        return 15;
    }

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
