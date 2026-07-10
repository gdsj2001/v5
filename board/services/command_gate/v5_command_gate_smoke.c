#include "v5_command_gate.h"
#include "v5_linuxcncrsh_client.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
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

    if (strcmp(line, "Set Open /opt/8ax/v5/gcode/golden/cc-ac.ngc\nSet Mode Auto\nSet Run 0\nSet Resume") != 0) {
        return 4;
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
