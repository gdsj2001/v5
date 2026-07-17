#include "v5_linuxcncrsh_client.h"
#include "v5_linuxcncrsh_internal.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static void v5_linuxcncrsh_set_code(
    char *code,
    size_t code_cap,
    const char *value)
{
    if (code && code_cap > 0U) {
        snprintf(code, code_cap, "%s", value ? value : "");
    }
}

int v5_linuxcncrsh_format_all_home(char *out, size_t out_size)
{
    int rc;
    if (!out || out_size == 0U) {
        return 0;
    }
    rc = snprintf(
        out,
        out_size,
        "Set Home -1");
    return v5_linuxcncrsh_format_ok(rc, out_size);
}

int v5_linuxcncrsh_estop_reset_ready(int probe_ok, int estop_active)
{
    return probe_ok && !estop_active;
}

#ifndef _WIN32
static int v5_linuxcncrsh_read_task_context_fd(
    int fd,
    V5LinuxcncrshTaskContext *context_out)
{
    char mode_response[256];
    char program_status_response[256];
    char program_response[768];

    return context_out &&
           v5_linuxcncrsh_send_request_text(
               fd, "Get Mode", mode_response, sizeof(mode_response)) &&
           v5_linuxcncrsh_send_request_text(
               fd, "Get Program_Status",
               program_status_response, sizeof(program_status_response)) &&
           v5_linuxcncrsh_send_request_text(
               fd, "Get Program", program_response, sizeof(program_response)) &&
           v5_linuxcncrsh_parse_task_context(
               mode_response, program_status_response, program_response, context_out);
}

static int v5_linuxcncrsh_read_task_state_fd(
    int fd,
    V5LinuxcncrshTaskState *state_out)
{
    char response[1536];

    return state_out &&
           v5_linuxcncrsh_send_request_text(
               fd, "Get Task_State", response, sizeof(response)) &&
           v5_linuxcncrsh_parse_task_state_response(response, state_out);
}

static int v5_linuxcncrsh_read_teleop_enabled_fd(
    int fd,
    int *enabled_out)
{
    char response[256];

    return enabled_out &&
           v5_linuxcncrsh_send_request_text(
               fd, "Get Teleop_Enable", response, sizeof(response)) &&
           v5_linuxcncrsh_parse_teleop_enabled_response(response, enabled_out);
}
#endif

V5LinuxcncrshHomeEntryStatus v5_linuxcncrsh_prepare_home_entry(
    const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return V5_LINUXCNCRSH_HOME_ENTRY_UNAVAILABLE;
#else
    enum { V5_HOME_ENTRY_WAIT_ATTEMPTS = 12 };
    V5LinuxcncrshTaskContext before;
    V5LinuxcncrshTaskContext current;
    V5LinuxcncrshTaskContext after;
    unsigned int actions;
    unsigned int attempt;
    int teleop_enabled = 0;
    int fd = v5_linuxcncrsh_gate_connect(config);

    if (fd < 0) {
        return V5_LINUXCNCRSH_HOME_ENTRY_UNAVAILABLE;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U) ||
        !v5_linuxcncrsh_read_task_context_fd(fd, &before)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_HOME_ENTRY_CONTEXT_UNAVAILABLE;
    }
    current = before;
    actions = v5_linuxcncrsh_home_entry_actions(&before);

    if ((actions & V5_LINUXCNCRSH_HOME_ACTION_ABORT) != 0U) {
        if (!v5_linuxcncrsh_send_request_text(fd, "Set Abort", 0, 0U)) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_ABORT_NOT_CONFIRMED;
        }
        for (attempt = 0U; attempt < V5_HOME_ENTRY_WAIT_ATTEMPTS; ++attempt) {
            if (v5_linuxcncrsh_read_task_context_fd(fd, &current)) {
                if (strcmp(before.program, current.program) != 0) {
                    v5_linuxcncrsh_gate_close();
                    return V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED;
                }
                if (current.program_status == V5_LINUXCNCRSH_PROGRAM_IDLE) {
                    break;
                }
            }
            usleep(25000U);
        }
        if (attempt == V5_HOME_ENTRY_WAIT_ATTEMPTS) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_ABORT_NOT_CONFIRMED;
        }
    }

    if ((actions & V5_LINUXCNCRSH_HOME_ACTION_MANUAL) != 0U) {
        if (!v5_linuxcncrsh_send_request_text(fd, "Set Mode Manual", 0, 0U)) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_MANUAL_NOT_CONFIRMED;
        }
        for (attempt = 0U; attempt < V5_HOME_ENTRY_WAIT_ATTEMPTS; ++attempt) {
            if (v5_linuxcncrsh_read_task_context_fd(fd, &current)) {
                if (strcmp(before.program, current.program) != 0) {
                    v5_linuxcncrsh_gate_close();
                    return V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED;
                }
                if (current.mode == V5_LINUXCNCRSH_TASK_MODE_MANUAL &&
                    current.program_status == V5_LINUXCNCRSH_PROGRAM_IDLE) {
                    break;
                }
            }
            usleep(25000U);
        }
        if (attempt == V5_HOME_ENTRY_WAIT_ATTEMPTS) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_MANUAL_NOT_CONFIRMED;
        }
    }

    if (!v5_linuxcncrsh_read_teleop_enabled_fd(fd, &teleop_enabled)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_HOME_ENTRY_JOINT_MODE_NOT_CONFIRMED;
    }
    if (!v5_linuxcncrsh_home_entry_joint_mode_ready(1, teleop_enabled)) {
        if (!v5_linuxcncrsh_send_request_text(
                fd, "Set Teleop_Enable Off", 0, 0U)) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_JOINT_MODE_NOT_CONFIRMED;
        }
        for (attempt = 0U; attempt < V5_HOME_ENTRY_WAIT_ATTEMPTS; ++attempt) {
            int probe_ok = v5_linuxcncrsh_read_teleop_enabled_fd(
                fd, &teleop_enabled);
            if (v5_linuxcncrsh_home_entry_joint_mode_ready(
                    probe_ok, teleop_enabled)) {
                break;
            }
            usleep(25000U);
        }
        if (attempt == V5_HOME_ENTRY_WAIT_ATTEMPTS) {
            v5_linuxcncrsh_gate_close();
            return V5_LINUXCNCRSH_HOME_ENTRY_JOINT_MODE_NOT_CONFIRMED;
        }
    }

    if (!v5_linuxcncrsh_read_task_context_fd(fd, &after)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_HOME_ENTRY_CONTEXT_UNAVAILABLE;
    }
    if (strcmp(before.program, after.program) != 0) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_HOME_ENTRY_PROGRAM_CHANGED;
    }
    if (!v5_linuxcncrsh_home_entry_context_preserved(&before, &after)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_HOME_ENTRY_MANUAL_NOT_CONFIRMED;
    }
    return V5_LINUXCNCRSH_HOME_ENTRY_READY;
#endif
}

int v5_linuxcncrsh_clean_execution_after_estop(
    const V5LinuxcncrshConfig *config,
    V5LinuxcncrshTaskState *state_out,
    char *code,
    size_t code_cap)
{
#ifdef _WIN32
    (void)config;
    if (state_out) {
        memset(state_out, 0, sizeof(*state_out));
    }
    v5_linuxcncrsh_set_code(
        code, code_cap, "ESTOP_CLEAN_LINUXCNCRSH_UNAVAILABLE");
    return 0;
#else
    enum { V5_ESTOP_CLEAN_WAIT_ATTEMPTS = 40 };
    V5LinuxcncrshTaskState before;
    V5LinuxcncrshTaskState current;
    unsigned int attempt;
    int have_current = 0;
    int fd = v5_linuxcncrsh_gate_connect(config);

    if (state_out) {
        memset(state_out, 0, sizeof(*state_out));
    }
    if (fd < 0) {
        v5_linuxcncrsh_set_code(
            code, code_cap, "ESTOP_CLEAN_LINUXCNCRSH_UNAVAILABLE");
        return 0;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U) ||
        !v5_linuxcncrsh_read_task_state_fd(fd, &before)) {
        v5_linuxcncrsh_gate_close();
        v5_linuxcncrsh_set_code(
            code, code_cap, "ESTOP_CLEAN_TASK_STATE_UNAVAILABLE");
        return 0;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Abort", 0, 0U)) {
        v5_linuxcncrsh_gate_close();
        v5_linuxcncrsh_set_code(
            code, code_cap, "ESTOP_CLEAN_ABORT_NOT_ACCEPTED");
        return 0;
    }
    current = before;
    for (attempt = 0U; attempt < V5_ESTOP_CLEAN_WAIT_ATTEMPTS; ++attempt) {
        if (v5_linuxcncrsh_read_task_state_fd(fd, &current)) {
            int fresh = current.echo != before.echo ||
                        current.heartbeat != before.heartbeat;
            have_current = 1;
            if (strcmp(current.program, before.program) != 0) {
                if (state_out) {
                    *state_out = current;
                }
                v5_linuxcncrsh_gate_close();
                v5_linuxcncrsh_set_code(
                    code, code_cap, "ESTOP_CLEAN_PROGRAM_CHANGED");
                return 0;
            }
            if (fresh && v5_linuxcncrsh_task_state_clean_after_estop(
                             &current, before.program)) {
                if (state_out) {
                    *state_out = current;
                }
                v5_linuxcncrsh_gate_close();
                v5_linuxcncrsh_set_code(code, code_cap, "ESTOP_CLEAN_OK");
                return 1;
            }
        }
        usleep(25000U);
    }
    if (state_out) {
        *state_out = have_current ? current : before;
    }
    v5_linuxcncrsh_gate_close();
    v5_linuxcncrsh_set_code(
        code, code_cap,
        have_current ? "ESTOP_CLEAN_TERMINAL_NOT_CONFIRMED"
                     : "ESTOP_CLEAN_TASK_STATE_UNAVAILABLE");
    return 0;
#endif
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

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_off_sequence(
    const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    int enabled = 1;
    V5LinuxcncrshSendStatus status;

    if (v5_linuxcncrsh_probe_machine_enabled(config, &enabled, 0, 0) && !enabled) {
        return V5_LINUXCNCRSH_SEND_SENT;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Machine Off");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    return v5_linuxcncrsh_wait_machine_enabled_actual(config, 0, 40U, 25000U)
        ? V5_LINUXCNCRSH_SEND_SENT
        : V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_on_after_estop_reset(
    const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    unsigned int attempt;
    int estop_active = 1;
    V5LinuxcncrshSendStatus status;

    for (attempt = 0U; attempt < 20U; ++attempt) {
        int probe_ok = v5_linuxcncrsh_probe_estop(config, &estop_active, 0, 0);
        if (v5_linuxcncrsh_estop_reset_ready(probe_ok, estop_active)) {
            break;
        }
        usleep(10000U);
    }
    if (attempt == 20U) {
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Machine On");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    return v5_linuxcncrsh_wait_machine_enabled_actual(config, 1, 30U, 25000U)
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
    if (prepared && request && request->kind == V5_COMMAND_START) {
        return v5_linuxcncrsh_send_start_transaction(
            config, prepared, request, line, sizeof(line));
    }
    if (!v5_linuxcncrsh_format_line(prepared, request, line, sizeof(line))) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }

    return v5_linuxcncrsh_send_line(config, line);
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_start_transaction(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *command_line,
    size_t command_line_size)
{
#ifdef _WIN32
    (void)config;
    (void)prepared;
    (void)request;
    if (command_line && command_line_size > 0U) {
        command_line[0] = '\0';
    }
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    int fd;
    if (!prepared || !request || !command_line || command_line_size == 0U ||
        !prepared->accepted || prepared->kind != V5_COMMAND_START ||
        request->kind != V5_COMMAND_START ||
        !request->text_value || !request->text_value[0]) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }
    command_line[0] = '\0';
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U) ||
        !v5_linuxcncrsh_format_start_transaction(
            prepared, request, command_line, command_line_size) ||
        !v5_linuxcncrsh_send_fifo_commands(fd, command_line)) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}
