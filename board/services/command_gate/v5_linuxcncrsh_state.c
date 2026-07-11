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
