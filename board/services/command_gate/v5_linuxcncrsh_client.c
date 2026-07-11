#include "v5_linuxcncrsh_client.h"
#include "v5_linuxcncrsh_internal.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int v5_linuxcncrsh_format_home_sequence(char *out, size_t out_size)
{
    int rc;
    if (!out || out_size == 0U) {
        return 0;
    }
    rc = snprintf(out, out_size, "native_home_mode_gate active mode + active model + real motion + native readback");
    return v5_linuxcncrsh_format_ok(rc, out_size);
}


V5LinuxcncrshSendStatus v5_linuxcncrsh_send_line(
    const V5LinuxcncrshConfig *config,
    const char *line)
{
#ifdef _WIN32
    (void)config;
    (void)line;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    int fd;
    int needs_control;

    if (!line || !line[0]) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }

    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }

    needs_control = strncmp(line, "Set ", 4) == 0 && strncmp(line, "Set Enable ", 11) != 0;
    if (needs_control && !v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_send_fifo_commands(fd, line)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }

    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_on_sequence(
    const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    int enabled = 0;
    V5LinuxcncrshSendStatus status;

    if (v5_linuxcncrsh_probe_machine_enabled(config, &enabled, 0, 0) && enabled) {
        return V5_LINUXCNCRSH_SEND_SENT;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Machine On");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    return v5_linuxcncrsh_wait_machine_enabled_actual(config, 1, 40U, 25000U)
        ? V5_LINUXCNCRSH_SEND_SENT
        : V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_prepared(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request)
{
    char line[384];

    if (prepared && request && request->kind == V5_COMMAND_HOME) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (prepared && request &&
        (request->kind == V5_COMMAND_ESTOP_RESET || request->kind == V5_COMMAND_ESTOP_FORCE) &&
        strcmp(prepared->owner ? prepared->owner : "", "native_safety") == 0) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    if (!v5_linuxcncrsh_format_line(prepared, request, line, sizeof(line))) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }

    return v5_linuxcncrsh_send_line(config, line);
}
