#include "v5_linuxcncrsh_client.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

static const char *v5_linuxcncrsh_host(const V5LinuxcncrshConfig *config)
{
    return (config && config->host && config->host[0]) ? config->host : "127.0.0.1";
}

static unsigned short v5_linuxcncrsh_port(const V5LinuxcncrshConfig *config)
{
    return (config && config->port) ? config->port : 5007U;
}

static const char *v5_linuxcncrsh_password(const V5LinuxcncrshConfig *config)
{
    return (config && config->connect_password) ? config->connect_password : "";
}

static const char *v5_linuxcncrsh_client_name(const V5LinuxcncrshConfig *config)
{
    return (config && config->client_name && config->client_name[0]) ? config->client_name : "v5_ui";
}

static int v5_linuxcncrsh_format_ok(int rc, size_t out_size)
{
    return rc > 0 && (size_t)rc < out_size;
}

int v5_linuxcncrsh_format_home_sequence(char *out, size_t out_size)
{
    int rc;
    if (!out || out_size == 0U) {
        return 0;
    }
    rc = snprintf(out, out_size, "Set Mode Manual | Set Home -1");
    return v5_linuxcncrsh_format_ok(rc, out_size);
}

#ifndef _WIN32
static int v5_linuxcncrsh_send_all(int fd, const char *text)
{
    size_t len = strlen(text);
    size_t sent = 0U;

    while (sent < len) {
        ssize_t rc = send(fd, text + sent, len - sent, 0);
        if (rc <= 0) {
            return 0;
        }
        sent += (size_t)rc;
    }

    return 1;
}
#endif

#ifndef _WIN32
static int v5_linuxcncrsh_configure_timeout(int fd, const V5LinuxcncrshConfig *config)
{
    struct timeval timeout;
    timeout.tv_sec = (config && config->timeout_ms) ? (long)(config->timeout_ms / 1000U) : 1L;
    timeout.tv_usec = (config && config->timeout_ms) ? (long)((config->timeout_ms % 1000U) * 1000U) : 0L;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

static int v5_linuxcncrsh_open_socket(const V5LinuxcncrshConfig *config)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    (void)v5_linuxcncrsh_configure_timeout(fd, config);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(v5_linuxcncrsh_port(config));
    if (inet_pton(AF_INET, v5_linuxcncrsh_host(config), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int v5_linuxcncrsh_recv_text(int fd, char *out, size_t out_size)
{
    size_t used = 0U;
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
        if ((size_t)rc < out_size - used - 1U) {
            break;
        }
    }
    return used > 0U;
}

static int v5_linuxcncrsh_contains_machine_state(const char *text);
static int v5_linuxcncrsh_contains_machine_off(const char *text);
static int v5_linuxcncrsh_contains_estop_state(const char *text, int *active_out);

static int v5_linuxcncrsh_recv_until_machine(int fd, char *out, size_t out_size)
{
    size_t used = 0U;
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
            return 1;
        }
    }
    return used > 0U && v5_linuxcncrsh_contains_machine_state(out);
}

static int v5_linuxcncrsh_recv_until_estop(int fd, char *out, size_t out_size, int *active_out)
{
    size_t used = 0U;
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
        if (v5_linuxcncrsh_contains_estop_state(out, active_out)) {
            return 1;
        }
    }
    return used > 0U && v5_linuxcncrsh_contains_estop_state(out, active_out);
}

static int v5_linuxcncrsh_line_equals(const char *text, const char *wanted)
{
    const char *p;
    size_t wanted_len;

    if (!text || !wanted) {
        return 0;
    }
    wanted_len = strlen(wanted);
    p = text;
    while (*p) {
        const char *start;
        const char *end;
        size_t len;
        size_t i;

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
        len = (size_t)(end - start);
        if (len == wanted_len) {
            int match = 1;
            for (i = 0U; i < len; ++i) {
                if (toupper((unsigned char)start[i]) != toupper((unsigned char)wanted[i])) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                return 1;
            }
        }
        while (*p == '\r' || *p == '\n') {
            ++p;
        }
    }
    return 0;
}

static int v5_linuxcncrsh_parse_machine_state(const char *text, int *enabled_out)
{
    if (v5_linuxcncrsh_line_equals(text, "MACHINE ON")) {
        if (enabled_out) {
            *enabled_out = 1;
        }
        return 1;
    }
    if (v5_linuxcncrsh_line_equals(text, "MACHINE OFF")) {
        if (enabled_out) {
            *enabled_out = 0;
        }
        return 1;
    }
    return 0;
}

static int v5_linuxcncrsh_contains_machine_state(const char *text)
{
    return v5_linuxcncrsh_parse_machine_state(text, 0);
}

static int v5_linuxcncrsh_contains_machine_off(const char *text)
{
    int enabled = 1;
    return v5_linuxcncrsh_parse_machine_state(text, &enabled) && !enabled;
}

static int v5_linuxcncrsh_contains_estop_state(const char *text, int *active_out)
{
    if (v5_linuxcncrsh_line_equals(text, "ESTOP ON")) {
        if (active_out) {
            *active_out = 1;
        }
        return 1;
    }
    if (v5_linuxcncrsh_line_equals(text, "ESTOP OFF")) {
        if (active_out) {
            *active_out = 0;
        }
        return 1;
    }
    return 0;
}

static int g_v5_linuxcncrsh_fd = -1;
static char g_v5_linuxcncrsh_host[64];
static unsigned short g_v5_linuxcncrsh_port;
static char g_v5_linuxcncrsh_password[64];

static void v5_linuxcncrsh_gate_close(void)
{
    if (g_v5_linuxcncrsh_fd >= 0) {
        close(g_v5_linuxcncrsh_fd);
    }
    g_v5_linuxcncrsh_fd = -1;
    g_v5_linuxcncrsh_host[0] = '\0';
    g_v5_linuxcncrsh_port = 0U;
    g_v5_linuxcncrsh_password[0] = '\0';
}

static void v5_linuxcncrsh_endpoint_snapshot(
    const V5LinuxcncrshConfig *config,
    char *host,
    size_t host_size,
    unsigned short *port,
    char *password,
    size_t password_size)
{
    if (host && host_size > 0U) {
        snprintf(host, host_size, "%s", v5_linuxcncrsh_host(config));
    }
    if (port) {
        *port = v5_linuxcncrsh_port(config);
    }
    if (password && password_size > 0U) {
        snprintf(password, password_size, "%s", v5_linuxcncrsh_password(config));
    }
}

static int v5_linuxcncrsh_gate_endpoint_matches(const char *host, unsigned short port, const char *password)
{
    return g_v5_linuxcncrsh_fd >= 0 &&
           g_v5_linuxcncrsh_port == port &&
           strcmp(g_v5_linuxcncrsh_host, host ? host : "") == 0 &&
           strcmp(g_v5_linuxcncrsh_password, password ? password : "") == 0;
}

static int v5_linuxcncrsh_gate_socket_alive(int fd)
{
    char byte;
    ssize_t rc;

    if (fd < 0) {
        return 0;
    }
    errno = 0;
    rc = recv(fd, &byte, 1U, MSG_PEEK | MSG_DONTWAIT);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return 0;
    }
    return 1;
}

static int v5_linuxcncrsh_gate_connect(const V5LinuxcncrshConfig *config)
{
    char host[64];
    char password[64];
    char hello[160];
    char ack[256];
    unsigned short port;
    int fd;

    v5_linuxcncrsh_endpoint_snapshot(config, host, sizeof(host), &port, password, sizeof(password));
    if (v5_linuxcncrsh_gate_endpoint_matches(host, port, password)) {
        if (v5_linuxcncrsh_gate_socket_alive(g_v5_linuxcncrsh_fd)) {
            (void)v5_linuxcncrsh_configure_timeout(g_v5_linuxcncrsh_fd, config);
            return g_v5_linuxcncrsh_fd;
        }
        v5_linuxcncrsh_gate_close();
    }

    v5_linuxcncrsh_gate_close();
    fd = v5_linuxcncrsh_open_socket(config);
    if (fd < 0) {
        return -1;
    }

    snprintf(
        hello,
        sizeof(hello),
        "Hello %s %s 1.0\n",
        password,
        v5_linuxcncrsh_client_name(config));
    if (!v5_linuxcncrsh_send_all(fd, hello) ||
        !v5_linuxcncrsh_recv_text(fd, ack, sizeof(ack))) {
        close(fd);
        return -1;
    }

    g_v5_linuxcncrsh_fd = fd;
    snprintf(g_v5_linuxcncrsh_host, sizeof(g_v5_linuxcncrsh_host), "%s", host);
    g_v5_linuxcncrsh_port = port;
    snprintf(g_v5_linuxcncrsh_password, sizeof(g_v5_linuxcncrsh_password), "%s", password);
    return g_v5_linuxcncrsh_fd;
}

int v5_linuxcncrsh_gate_preconnect(const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return 0;
#else
    return v5_linuxcncrsh_gate_connect(config) >= 0;
#endif
}

static int v5_linuxcncrsh_send_request_text(int fd, const char *request, char *out, size_t out_size)
{
    char framed[768];
    char discard[512];
    int rc;

    if (fd < 0 || !request || !request[0]) {
        return 0;
    }
    rc = snprintf(framed, sizeof(framed), "%s\n", request);
    if (!v5_linuxcncrsh_format_ok(rc, sizeof(framed))) {
        return 0;
    }
    if (!v5_linuxcncrsh_send_all(fd, framed)) {
        return 0;
    }
    if (out && out_size > 0U) {
        return v5_linuxcncrsh_recv_text(fd, out, out_size);
    }
    return v5_linuxcncrsh_recv_text(fd, discard, sizeof(discard));
}

static int v5_linuxcncrsh_send_fifo_commands(int fd, const char *line)
{
    const char *p = line;
    char command[384];

    while (p && *p) {
        const char *start;
        const char *end;
        size_t len;

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
        len = (size_t)(end - start);
        if (len == 0U) {
            continue;
        }
        if (len >= sizeof(command)) {
            return 0;
        }
        memcpy(command, start, len);
        command[len] = '\0';
        if (!v5_linuxcncrsh_send_request_text(fd, command, 0, 0U)) {
            return 0;
        }
    }
    return 1;
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
    return v5_linuxcncrsh_parse_machine_state(transcript, enabled_out);
}

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

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_estop_reset_sequence(
    const V5LinuxcncrshConfig *config,
    int *machine_on_status_out,
    int *machine_on_requested_out)
{
#ifdef _WIN32
    (void)config;
    if (machine_on_status_out) {
        *machine_on_status_out = (int)V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }
    if (machine_on_requested_out) {
        *machine_on_requested_out = 0;
    }
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    V5LinuxcncrshSendStatus status;
    int estop_active = 1;
    int latch_cleared = 0;
    char transcript[1024];

    if (machine_on_status_out) {
        *machine_on_status_out = 0;
    }
    if (machine_on_requested_out) {
        *machine_on_requested_out = 0;
    }

    status = v5_linuxcncrsh_send_line(config, "Set EStop Off");
    if (status != V5_LINUXCNCRSH_SEND_SENT) {
        return status;
    }
    for (unsigned int attempt = 0U; attempt < 10U; ++attempt) {
        if (v5_linuxcncrsh_probe_estop(config, &estop_active, transcript, sizeof(transcript)) && !estop_active) {
            latch_cleared = 1;
            break;
        }
        usleep(100000U);
    }
    if (!latch_cleared) {
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }

    if (machine_on_requested_out) {
        *machine_on_requested_out = 1;
    }
    status = v5_linuxcncrsh_send_line(config, "Set Machine On");
    if (machine_on_status_out) {
        *machine_on_status_out = (int)status;
    }
    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_home_sequence(
    const V5LinuxcncrshConfig *config,
    char *mode_out,
    size_t mode_out_size)
{
#ifdef _WIN32
    (void)config;
    (void)mode_out;
    (void)mode_out_size;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    int fd;
    int ok;
    if (mode_out && mode_out_size > 0U) {
        snprintf(mode_out, mode_out_size, "manual_home_all_joints");
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }
    ok = v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U) &&
         v5_linuxcncrsh_send_request_text(fd, "Set Mode Manual", 0, 0U) &&
         v5_linuxcncrsh_send_request_text(fd, "Set Home -1", 0, 0U);
    if (!ok) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_estop_force_sequence(
    const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    V5LinuxcncrshConfig urgent_config;
    int fd;
    char ack[512];
    char transcript[1024];
    int ok;

    memset(&urgent_config, 0, sizeof(urgent_config));
    if (config) {
        urgent_config = *config;
    }
    if (urgent_config.timeout_ms == 0U || urgent_config.timeout_ms > 250U) {
        urgent_config.timeout_ms = 250U;
    }

    fd = v5_linuxcncrsh_gate_connect(&urgent_config);
    if (fd < 0) {
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }
    ok = v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U) &&
         v5_linuxcncrsh_send_request_text(fd, "Set Machine Off", ack, sizeof(ack)) &&
         v5_linuxcncrsh_send_request_text(fd, "Get Machine", transcript, sizeof(transcript)) &&
         v5_linuxcncrsh_contains_machine_off(transcript);
    if (!ok) {
        v5_linuxcncrsh_gate_close();
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_prepared(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request)
{
    char line[384];

    if (prepared && request && request->kind == V5_COMMAND_HOME &&
        strcmp(prepared->owner ? prepared->owner : "", "native_home_mode_gate") == 0) {
        return v5_linuxcncrsh_send_home_sequence(config, 0, 0);
    }
    if (prepared && request && request->kind == V5_COMMAND_ESTOP_RESET &&
        strcmp(prepared->owner ? prepared->owner : "", "native_safety") == 0) {
        return v5_linuxcncrsh_send_estop_reset_sequence(config, 0, 0);
    }
    if (prepared && request && request->kind == V5_COMMAND_ESTOP_FORCE &&
        strcmp(prepared->owner ? prepared->owner : "", "native_safety") == 0) {
        return v5_linuxcncrsh_send_estop_force_sequence(config);
    }
    if (!v5_linuxcncrsh_format_line(prepared, request, line, sizeof(line))) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }

    return v5_linuxcncrsh_send_line(config, line);
}
