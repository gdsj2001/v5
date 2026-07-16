#include "v5_linuxcncrsh_client.h"
#include "v5_linuxcncrsh_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int v5_linuxcncrsh_latest_prefixed_value(
    const char *text,
    const char *prefix,
    char *value_out,
    size_t value_size)
{
    const char *p;
    size_t prefix_len;
    int found = 0;

    if (!text || !prefix || !prefix[0] || !value_out || value_size == 0U) {
        return 0;
    }
    value_out[0] = '\0';
    prefix_len = strlen(prefix);
    p = text;
    while (*p) {
        const char *start;
        const char *end;
        const char *value;
        size_t value_len;

        while (*p == '\r' || *p == '\n') {
            ++p;
        }
        start = p;
        while (*p && *p != '\r' && *p != '\n') {
            ++p;
        }
        end = p;
        while (start < end && isspace((unsigned char)*start)) {
            ++start;
        }
        while (end > start && isspace((unsigned char)*(end - 1))) {
            --end;
        }
        if ((size_t)(end - start) <= prefix_len ||
            strncmp(start, prefix, prefix_len) != 0) {
            continue;
        }
        value = start + prefix_len;
        while (value < end && isspace((unsigned char)*value)) {
            ++value;
        }
        value_len = (size_t)(end - value);
        if (value_len == 0U || value_len >= value_size) {
            return 0;
        }
        memcpy(value_out, value, value_len);
        value_out[value_len] = '\0';
        found = 1;
    }
    return found;
}

int v5_linuxcncrsh_parse_task_state_response(
    const char *response,
    V5LinuxcncrshTaskState *state_out)
{
    char payload[1024];
    V5LinuxcncrshTaskState parsed;
    int program_offset = -1;
    const char *program;
    size_t program_len;

    if (!state_out ||
        !v5_linuxcncrsh_latest_prefixed_value(
            response, "TASK_STATE ", payload, sizeof(payload))) {
        return 0;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (sscanf(
            payload,
            "state=%d mode=%d interp=%d exec=%d paused=%d "
            "motion_queue=%d motion_inpos=%d current_line=%d motion_line=%d "
            "read_line=%d echo=%d heartbeat=%u program=%n",
            &parsed.state,
            &parsed.mode,
            &parsed.interp,
            &parsed.exec,
            &parsed.paused,
            &parsed.motion_queue,
            &parsed.motion_inpos,
            &parsed.current_line,
            &parsed.motion_line,
            &parsed.read_line,
            &parsed.echo,
            &parsed.heartbeat,
            &program_offset) != 12 ||
        program_offset < 0 ||
        parsed.state < V5_LINUXCNCRSH_TASK_STATE_ESTOP ||
        parsed.state > V5_LINUXCNCRSH_TASK_STATE_ON ||
        parsed.mode < V5_LINUXCNCRSH_TASK_MODE_MANUAL ||
        parsed.mode > V5_LINUXCNCRSH_TASK_MODE_MDI ||
        parsed.interp < V5_LINUXCNCRSH_TASK_INTERP_IDLE ||
        parsed.interp > 4 ||
        parsed.exec < 1 || parsed.exec > 10 || parsed.exec == 6 ||
        parsed.paused < 0 || parsed.paused > 1 ||
        parsed.motion_queue < 0 ||
        parsed.motion_inpos < 0 || parsed.motion_inpos > 1) {
        return 0;
    }
    program = payload + program_offset;
    program_len = strlen(program);
    if (program_len == 0U || program_len >= sizeof(parsed.program)) {
        return 0;
    }
    memcpy(parsed.program, program, program_len + 1U);
    *state_out = parsed;
    return 1;
}

int v5_linuxcncrsh_task_state_clean_after_estop(
    const V5LinuxcncrshTaskState *state,
    const char *expected_program)
{
    return state && expected_program && expected_program[0] &&
           (state->state == V5_LINUXCNCRSH_TASK_STATE_ESTOP ||
            state->state == V5_LINUXCNCRSH_TASK_STATE_ESTOP_RESET ||
            state->state == V5_LINUXCNCRSH_TASK_STATE_OFF) &&
           state->interp == V5_LINUXCNCRSH_TASK_INTERP_IDLE &&
           state->exec == V5_LINUXCNCRSH_TASK_EXEC_DONE &&
           state->paused == 0 &&
           state->motion_queue == 0 &&
           state->motion_inpos == 1 &&
           strcmp(state->program, expected_program) == 0;
}

int v5_linuxcncrsh_parse_task_context(
    const char *mode_response,
    const char *program_status_response,
    const char *program_response,
    V5LinuxcncrshTaskContext *context_out)
{
    char mode[16];
    char program_status[16];
    char program[sizeof(context_out->program)];
    V5LinuxcncrshTaskContext parsed;

    if (!context_out ||
        !v5_linuxcncrsh_latest_prefixed_value(
            mode_response, "MODE ", mode, sizeof(mode)) ||
        !v5_linuxcncrsh_latest_prefixed_value(
            program_status_response, "PROGRAM_STATUS ",
            program_status, sizeof(program_status)) ||
        !v5_linuxcncrsh_latest_prefixed_value(
            program_response, "PROGRAM ", program, sizeof(program))) {
        return 0;
    }

    memset(&parsed, 0, sizeof(parsed));
    if (strcmp(mode, "MANUAL") == 0) {
        parsed.mode = V5_LINUXCNCRSH_TASK_MODE_MANUAL;
    } else if (strcmp(mode, "AUTO") == 0) {
        parsed.mode = V5_LINUXCNCRSH_TASK_MODE_AUTO;
    } else if (strcmp(mode, "MDI") == 0) {
        parsed.mode = V5_LINUXCNCRSH_TASK_MODE_MDI;
    } else {
        return 0;
    }
    if (strcmp(program_status, "IDLE") == 0) {
        parsed.program_status = V5_LINUXCNCRSH_PROGRAM_IDLE;
    } else if (strcmp(program_status, "RUNNING") == 0) {
        parsed.program_status = V5_LINUXCNCRSH_PROGRAM_RUNNING;
    } else if (strcmp(program_status, "PAUSED") == 0) {
        parsed.program_status = V5_LINUXCNCRSH_PROGRAM_PAUSED;
    } else {
        return 0;
    }
    snprintf(parsed.program, sizeof(parsed.program), "%s", program);
    *context_out = parsed;
    return 1;
}

unsigned int v5_linuxcncrsh_home_entry_actions(
    const V5LinuxcncrshTaskContext *context)
{
    unsigned int actions = 0U;
    if (!context || context->mode == V5_LINUXCNCRSH_TASK_MODE_UNKNOWN ||
        context->program_status == V5_LINUXCNCRSH_PROGRAM_UNKNOWN ||
        !context->program[0]) {
        return 0U;
    }
    if (context->mode != V5_LINUXCNCRSH_TASK_MODE_MANUAL ||
        context->program_status != V5_LINUXCNCRSH_PROGRAM_IDLE) {
        actions |= V5_LINUXCNCRSH_HOME_ACTION_ABORT;
    }
    if (context->mode != V5_LINUXCNCRSH_TASK_MODE_MANUAL) {
        actions |= V5_LINUXCNCRSH_HOME_ACTION_MANUAL;
    }
    return actions;
}

int v5_linuxcncrsh_home_entry_context_preserved(
    const V5LinuxcncrshTaskContext *before,
    const V5LinuxcncrshTaskContext *after)
{
    return before && after && before->program[0] &&
           after->mode == V5_LINUXCNCRSH_TASK_MODE_MANUAL &&
           after->program_status == V5_LINUXCNCRSH_PROGRAM_IDLE &&
           strcmp(before->program, after->program) == 0;
}

int v5_linuxcncrsh_parse_teleop_enabled_response(
    const char *response,
    int *enabled_out)
{
    char state[16];

    if (enabled_out) {
        *enabled_out = 0;
    }
    if (!v5_linuxcncrsh_latest_prefixed_value(
            response, "TELEOP_ENABLE ", state, sizeof(state))) {
        return 0;
    }
    if (strcmp(state, "YES") == 0) {
        if (enabled_out) {
            *enabled_out = 1;
        }
        return 1;
    }
    if (strcmp(state, "NO") == 0) {
        return 1;
    }
    return 0;
}

int v5_linuxcncrsh_home_entry_joint_mode_ready(
    int probe_ok,
    int teleop_enabled)
{
    return probe_ok && !teleop_enabled;
}

#ifndef _WIN32
static int v5_linuxcncrsh_parse_binary_state_latest(
    const char *text,
    const char *on_line,
    const char *off_line,
    int *enabled_out)
{
    const char *p;
    int found = 0;
    int enabled = 0;

    if (!text || !on_line || !off_line) {
        return 0;
    }
    p = text;
    while (*p) {
        const char *start;
        const char *end;

        while (*p == '\r' || *p == '\n') {
            ++p;
        }
        start = p;
        while (*p && *p != '\r' && *p != '\n') {
            ++p;
        }
        end = p;
        if (v5_linuxcncrsh_line_span_equals(start, end, on_line)) {
            enabled = 1;
            found = 1;
        } else if (v5_linuxcncrsh_line_span_equals(start, end, off_line)) {
            enabled = 0;
            found = 1;
        }
        while (*p == '\r' || *p == '\n') {
            ++p;
        }
    }
    if (found && enabled_out) {
        *enabled_out = enabled;
    }
    return found;
}

static int v5_linuxcncrsh_parse_machine_state(const char *text, int *enabled_out)
{
    return v5_linuxcncrsh_parse_binary_state_latest(text, "MACHINE ON", "MACHINE OFF", enabled_out);
}

static int v5_linuxcncrsh_contains_machine_state(const char *text)
{
    return v5_linuxcncrsh_parse_machine_state(text, 0);
}

static int v5_linuxcncrsh_contains_estop_state(const char *text, int *active_out)
{
    return v5_linuxcncrsh_parse_binary_state_latest(text, "ESTOP ON", "ESTOP OFF", active_out);
}

static int v5_linuxcncrsh_recv_until_machine(int fd, char *out, size_t out_size)
{
    size_t used = 0U;
    int found = 0;
    if (!out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';
    while (used + 1U < out_size) {
        ssize_t rc = recv(fd, out + used, out_size - used - 1U, 0);
        if (rc <= 0) {
            break;
        }
        used += (size_t)rc;
        out[used] = '\0';
        if (v5_linuxcncrsh_contains_machine_state(out)) {
            found = 1;
            usleep(20000U);
            v5_linuxcncrsh_recv_available(fd, out, &used, out_size);
            break;
        }
    }
    return found || (used > 0U && v5_linuxcncrsh_contains_machine_state(out));
}

static int v5_linuxcncrsh_recv_until_estop(int fd, char *out, size_t out_size, int *active_out)
{
    size_t used = 0U;
    int active = 0;
    int found = 0;
    if (!out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';
    while (used + 1U < out_size) {
        ssize_t rc = recv(fd, out + used, out_size - used - 1U, 0);
        if (rc <= 0) {
            break;
        }
        used += (size_t)rc;
        out[used] = '\0';
        if (v5_linuxcncrsh_contains_estop_state(out, &active)) {
            found = 1;
            usleep(20000U);
            v5_linuxcncrsh_recv_available(fd, out, &used, out_size);
            (void)v5_linuxcncrsh_contains_estop_state(out, &active);
            break;
        }
    }
    if (!found && used > 0U) {
        found = v5_linuxcncrsh_contains_estop_state(out, &active);
    }
    if (found && active_out) {
        *active_out = active;
    }
    return found;
}
#endif

int v5_linuxcncrsh_probe_machine(
    const V5LinuxcncrshConfig *config,
    char *out,
    size_t out_size)
{
#ifdef _WIN32
    (void)config;
    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    return 0;
#else
    int fd;
    char transcript[1024];
    int ok;

    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }

    v5_linuxcncrsh_drain_pending(fd);
    ok = v5_linuxcncrsh_send_all(fd, "Get Machine\n") &&
         v5_linuxcncrsh_recv_until_machine(fd, transcript, sizeof(transcript)) &&
         v5_linuxcncrsh_contains_machine_state(transcript);
    if (!ok) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    if (out && out_size > 0U) {
        snprintf(out, out_size, "%s", transcript);
    }
    return 1;
#endif
}

int v5_linuxcncrsh_probe_machine_enabled(
    const V5LinuxcncrshConfig *config,
    int *enabled_out,
    char *out,
    size_t out_size)
{
    char transcript[1024];
    if (enabled_out) {
        *enabled_out = 0;
    }
    if (!v5_linuxcncrsh_probe_machine(config, transcript, sizeof(transcript))) {
        if (out && out_size > 0U) {
            out[0] = '\0';
        }
        return 0;
    }
    if (out && out_size > 0U) {
        snprintf(out, out_size, "%s", transcript);
    }
#ifdef _WIN32
    return 0;
#else
    return v5_linuxcncrsh_parse_machine_state(transcript, enabled_out);
#endif
}

#ifndef _WIN32
int v5_linuxcncrsh_wait_machine_enabled_actual(
    const V5LinuxcncrshConfig *config,
    int expected_enabled,
    unsigned int attempts,
    unsigned int delay_us)
{
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < attempts; ++attempt) {
        int enabled = 0;
        int ok;
        ok = v5_linuxcncrsh_probe_machine_enabled(config, &enabled, 0, 0);
        if (ok && enabled == (expected_enabled ? 1 : 0)) {
            ++stable;
        } else {
            stable = 0U;
        }
        if (stable >= 2U) {
            return 1;
        }
        if (delay_us > 0U) {
            usleep(delay_us);
        }
    }
    return 0;
}

#endif

int v5_native_probe_machine_enabled_actual(int *enabled_out)
{
    if (enabled_out) {
        *enabled_out = 0;
    }
    return 0;
}


int v5_linuxcncrsh_probe_estop(
    const V5LinuxcncrshConfig *config,
    int *active_out,
    char *out,
    size_t out_size)
{
#ifdef _WIN32
    (void)config;
    if (active_out) {
        *active_out = 0;
    }
    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    return 0;
#else
    int fd;
    char transcript[1024];
    int active = 0;
    int ok;

    if (active_out) {
        *active_out = 0;
    }
    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }

    v5_linuxcncrsh_drain_pending(fd);
    ok = v5_linuxcncrsh_send_all(fd, "Get Estop\n") &&
         v5_linuxcncrsh_recv_until_estop(fd, transcript, sizeof(transcript), &active);
    if (!ok) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    if (active_out) {
        *active_out = active;
    }
    if (out && out_size > 0U) {
        snprintf(out, out_size, "%s", transcript);
    }
    return 1;
#endif
}

int v5_linuxcncrsh_probe_active_driver_mode(char *out, size_t out_size)
{
    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    return 0;
}
