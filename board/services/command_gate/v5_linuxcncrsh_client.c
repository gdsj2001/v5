#include "v5_linuxcncrsh_client.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_sample.h"

#include <ctype.h>
#include <math.h>
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

static void v5_linuxcncrsh_copy_code(char *out, size_t out_size, const char *code)
{
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (code && code[0]) {
        snprintf(out, out_size, "%s", code);
    }
}

int v5_linuxcncrsh_format_home_sequence(char *out, size_t out_size)
{
    int rc;
    if (!out || out_size == 0U) {
        return 0;
    }
    rc = snprintf(out, out_size, "native_home_mode_gate BUS joint-jog proof + joint-jog zero + native MCS readback");
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

static void v5_linuxcncrsh_drain_pending(int fd)
{
    char discard[512];
    if (fd < 0) {
        return;
    }
    while (recv(fd, discard, sizeof(discard), MSG_DONTWAIT) > 0) {
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        errno = 0;
    }
}

static int v5_linuxcncrsh_response_has_word(const char *text, const char *word)
{
    size_t word_len;
    const char *p;
    if (!text || !word || !word[0]) {
        return 0;
    }
    word_len = strlen(word);
    p = text;
    while (*p) {
        while (*p && !isalnum((unsigned char)*p)) {
            ++p;
        }
        if (*p) {
            const char *start = p;
            size_t len;
            while (*p && isalnum((unsigned char)*p)) {
                ++p;
            }
            len = (size_t)(p - start);
            if (len == word_len) {
                int match = 1;
                size_t i;
                for (i = 0U; i < len; ++i) {
                    if (toupper((unsigned char)start[i]) != toupper((unsigned char)word[i])) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void v5_linuxcncrsh_recv_available(int fd, char *out, size_t *used, size_t out_size)
{
    if (fd < 0 || !out || !used || out_size == 0U) {
        return;
    }
    while (*used + 1U < out_size) {
        ssize_t rc;
        errno = 0;
        rc = recv(fd, out + *used, out_size - *used - 1U, MSG_DONTWAIT);
        if (rc <= 0) {
            break;
        }
        *used += (size_t)rc;
        out[*used] = '\0';
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        errno = 0;
    }
}

static int v5_linuxcncrsh_contains_machine_state(const char *text);
static int v5_linuxcncrsh_contains_estop_state(const char *text, int *active_out);

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

static int v5_linuxcncrsh_line_span_equals(const char *start, const char *end, const char *wanted)
{
    size_t wanted_len;
    size_t len;
    size_t i;

    if (!start || !end || start > end || !wanted) {
        return 0;
    }
    while (start < end && isspace((unsigned char)*start)) {
        ++start;
    }
    while (end > start && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    len = (size_t)(end - start);
    wanted_len = strlen(wanted);
    if (len != wanted_len) {
        return 0;
    }
    for (i = 0U; i < len; ++i) {
        if (toupper((unsigned char)start[i]) != toupper((unsigned char)wanted[i])) {
            return 0;
        }
    }
    return 1;
}

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
    v5_linuxcncrsh_drain_pending(fd);
    if (!v5_linuxcncrsh_send_all(fd, framed)) {
        return 0;
    }
    if (out && out_size > 0U) {
        if (!v5_linuxcncrsh_recv_text(fd, out, out_size)) {
            return 0;
        }
        return !v5_linuxcncrsh_response_has_word(out, "NAK") &&
               !v5_linuxcncrsh_response_has_word(out, "ERROR");
    }
    if (!v5_linuxcncrsh_recv_text(fd, discard, sizeof(discard))) {
        return 0;
    }
    return !v5_linuxcncrsh_response_has_word(discard, "NAK") &&
           !v5_linuxcncrsh_response_has_word(discard, "ERROR");
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
    return v5_linuxcncrsh_parse_machine_state(transcript, enabled_out);
}

#ifndef _WIN32
static int v5_linuxcncrsh_wait_machine_enabled_actual(
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

static int v5_linuxcncrsh_read_all_homed_actual(int *all_homed_out)
{
    V5NativeReadback readback;
    if (all_homed_out) {
        *all_homed_out = 0;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &readback) ||
        !v5_native_readback_all_homed_known(&readback)) {
        return 0;
    }
    if (all_homed_out) {
        *all_homed_out = readback.all_homed ? 1 : 0;
    }
    return 1;
}

static int v5_linuxcncrsh_wait_home_completion_actual(unsigned int attempts, unsigned int delay_us)
{
    unsigned int attempt;
    unsigned int stable = 0U;
    for (attempt = 0U; attempt < attempts; ++attempt) {
        int all_homed = 0;
        if (v5_linuxcncrsh_read_all_homed_actual(&all_homed) && all_homed) {
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

#define V5_BUS_HOME_TARGET_TOLERANCE 0.050
#define V5_BUS_HOME_MOTION_TOLERANCE 0.010
#define V5_BUS_HOME_PROOF_OFFSET 1.000
#define V5_BUS_HOME_LINEAR_SPEED_MM_PER_MIN 10000.000
#define V5_BUS_HOME_ROTARY_SPEED_DEG_PER_MIN 50000.000

static int v5_linuxcncrsh_read_native_mcs(V5NativeDisplaySample *sample)
{
    return sample &&
           v5_native_display_sample_read(sample) &&
           sample->available &&
           (sample->valid_mask & V5_STATUS_VALID_MCS) != 0U;
}

static double v5_linuxcncrsh_axis_target_error(unsigned int axis_i, double current, double target);

static int v5_linuxcncrsh_home_mcs_at_target(
    const double mcs[V5_STATUS_AXIS_COUNT],
    const double target[V5_STATUS_AXIS_COUNT],
    double tolerance)
{
    unsigned int i;
    if (!mcs || !target) {
        return 0;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (v5_linuxcncrsh_axis_target_error(i, mcs[i], target[i]) > tolerance) {
            return 0;
        }
    }
    return 1;
}

static double v5_linuxcncrsh_axis_target_error(unsigned int axis_i, double current, double target)
{
    double error;
    if (!isfinite(current) || !isfinite(target)) {
        return HUGE_VAL;
    }
    error = fabs(current - target);
    if (axis_i >= 3U) {
        while (error >= 360.0) {
            error -= 360.0;
        }
        if (error > 180.0) {
            error = 360.0 - error;
        }
    }
    return error;
}

static void v5_linuxcncrsh_home_note_axis_motion(
    const double before[V5_STATUS_AXIS_COUNT],
    const double after[V5_STATUS_AXIS_COUNT],
    double tolerance,
    int moved[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    if (!before || !after || !moved) {
        return;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!isfinite(before[i]) || !isfinite(after[i])) {
            continue;
        }
        if (fabs(after[i] - before[i]) > tolerance) {
            moved[i] = 1;
        }
    }
}

static int v5_linuxcncrsh_home_all_axes_moved(const int moved[V5_STATUS_AXIS_COUNT])
{
    unsigned int i;
    if (!moved) {
        return 0;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (!moved[i]) {
            return 0;
        }
    }
    return 1;
}

static int v5_linuxcncrsh_wait_bus_home_axis_arrival(
    const double start_mcs[V5_STATUS_AXIS_COUNT],
    int moved_axes[V5_STATUS_AXIS_COUNT],
    unsigned int axis_i,
    double target,
    unsigned int attempts,
    unsigned int delay_us)
{
    unsigned int attempt;
    unsigned int stable = 0U;
    if (axis_i >= V5_STATUS_AXIS_COUNT) {
        return 0;
    }
    for (attempt = 0U; attempt < attempts; ++attempt) {
        V5NativeDisplaySample sample;
        if (v5_linuxcncrsh_read_native_mcs(&sample)) {
            v5_linuxcncrsh_home_note_axis_motion(
                start_mcs,
                sample.mcs,
                V5_BUS_HOME_MOTION_TOLERANCE,
                moved_axes);
            if (v5_linuxcncrsh_axis_target_error(axis_i, sample.mcs[axis_i], target) <= V5_BUS_HOME_TARGET_TOLERANCE) {
                ++stable;
            } else {
                stable = 0U;
            }
            if (stable >= 3U) {
                return 1;
            }
        } else {
            stable = 0U;
        }
        if (delay_us > 0U) {
            usleep(delay_us);
        }
    }
    return 0;
}

static double v5_linuxcncrsh_bus_home_proof_delta(double current)
{
    if (!isfinite(current)) {
        return V5_BUS_HOME_PROOF_OFFSET;
    }
    if (fabs(current) > V5_BUS_HOME_PROOF_OFFSET) {
        return current > 0.0 ? -V5_BUS_HOME_PROOF_OFFSET : V5_BUS_HOME_PROOF_OFFSET;
    }
    return V5_BUS_HOME_PROOF_OFFSET;
}

static char v5_linuxcncrsh_bus_axis_letter(unsigned int axis_i)
{
    static const char axes[V5_STATUS_AXIS_COUNT] = {'X', 'Y', 'Z', 'A', 'C'};
    return axis_i < V5_STATUS_AXIS_COUNT ? axes[axis_i] : '\0';
}

static int v5_linuxcncrsh_send_bus_home_joint_jog(
    int fd,
    unsigned int axis_i,
    double delta,
    int coordinated_axis_jog)
{
    char line[128];
    double distance;
    double speed;
    int rc;
    if (axis_i >= V5_STATUS_AXIS_COUNT || !isfinite(delta) || fabs(delta) <= V5_BUS_HOME_MOTION_TOLERANCE) {
        return 0;
    }
    distance = fabs(delta);
    speed = axis_i < 3U ? V5_BUS_HOME_LINEAR_SPEED_MM_PER_MIN : V5_BUS_HOME_ROTARY_SPEED_DEG_PER_MIN;
    if (delta < 0.0) {
        speed = -speed;
    }
    if (coordinated_axis_jog) {
        char axis = v5_linuxcncrsh_bus_axis_letter(axis_i);
        if (!axis) {
            return 0;
        }
        rc = snprintf(
            line,
            sizeof(line),
            "Set Jog_Incr %c %.6f %.6f",
            axis,
            speed,
            distance);
    } else {
        rc = snprintf(
            line,
            sizeof(line),
            "Set Jog_Incr %u %.6f %.6f",
            axis_i,
            speed,
            distance);
    }
    return v5_linuxcncrsh_format_ok(rc, sizeof(line)) &&
           v5_linuxcncrsh_send_request_text(fd, "Set Mode Manual", 0, 0U) &&
           v5_linuxcncrsh_send_request_text(fd, line, 0, 0U);
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
    return v5_linuxcncrsh_wait_machine_enabled_actual(config, 1, 20U, 25000U)
        ? V5_LINUXCNCRSH_SEND_SENT
        : V5_LINUXCNCRSH_SEND_IO_ERROR;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_home_sequence(
    const V5LinuxcncrshConfig *config,
    char *mode_out,
    size_t mode_out_size,
    char *code_out,
    size_t code_out_size)
{
#ifdef _WIN32
    (void)config;
    (void)mode_out;
    (void)mode_out_size;
    v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_UNAVAILABLE_ON_WIN32");
    return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
#else
    V5NativeDisplaySample start_sample;
    V5NativeDisplaySample current_sample;
    double zero_target[V5_STATUS_AXIS_COUNT] = {0.0, 0.0, 0.0, 0.0, 0.0};
    int moved_axes[V5_STATUS_AXIS_COUNT] = {0, 0, 0, 0, 0};
    int fd;
    int ok;
    int all_homed = 0;
    int coordinated_axis_jog = 0;
    unsigned int axis_i;
    if (mode_out && mode_out_size > 0U) {
        snprintf(mode_out, mode_out_size, "bus_real_home_joint_jog_zero");
    }
    v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_NOT_ATTEMPTED");
    if (!v5_linuxcncrsh_wait_machine_enabled_actual(config, 1, 3U, 100000U)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_MACHINE_ENABLE_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_LINUXCNCRSH_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Enable EMCTOO", 0, 0U)) {
        v5_linuxcncrsh_gate_close();
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_ENABLE_REJECTED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_send_request_text(fd, "Set Abort", 0, 0U) ||
        !v5_linuxcncrsh_send_request_text(fd, "Set Mode Manual", 0, 0U)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_MANUAL_MODE_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    if (!v5_linuxcncrsh_read_native_mcs(&start_sample)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_NATIVE_MCS_UNAVAILABLE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    coordinated_axis_jog = v5_linuxcncrsh_read_all_homed_actual(&all_homed) && all_homed;
    for (axis_i = 0U; axis_i < V5_STATUS_AXIS_COUNT; ++axis_i) {
        double target = start_sample.mcs[axis_i] + v5_linuxcncrsh_bus_home_proof_delta(start_sample.mcs[axis_i]);
        if (!v5_linuxcncrsh_send_bus_home_joint_jog(
                fd,
                axis_i,
                target - start_sample.mcs[axis_i],
                coordinated_axis_jog)) {
            v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_PROOF_JOG_COMMAND_REJECTED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!v5_linuxcncrsh_wait_bus_home_axis_arrival(
                start_sample.mcs,
                moved_axes,
                axis_i,
                target,
                300U,
                50000U)) {
            v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_PROOF_JOG_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (!v5_linuxcncrsh_read_native_mcs(&current_sample)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_NATIVE_MCS_UNAVAILABLE_AFTER_PROOF");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    for (axis_i = 0U; axis_i < V5_STATUS_AXIS_COUNT; ++axis_i) {
        double delta = -current_sample.mcs[axis_i];
        if (!v5_linuxcncrsh_send_bus_home_joint_jog(
                fd,
                axis_i,
                delta,
                coordinated_axis_jog)) {
            v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_ZERO_JOG_COMMAND_REJECTED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!v5_linuxcncrsh_wait_bus_home_axis_arrival(
                start_sample.mcs,
                moved_axes,
                axis_i,
                0.0,
                600U,
                50000U)) {
            v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_ZERO_JOG_NOT_CONFIRMED");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
        if (!v5_linuxcncrsh_read_native_mcs(&current_sample)) {
            v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_NATIVE_MCS_UNAVAILABLE_DURING_ZERO");
            return V5_LINUXCNCRSH_SEND_IO_ERROR;
        }
    }
    if (!v5_linuxcncrsh_home_mcs_at_target(current_sample.mcs, zero_target, V5_BUS_HOME_TARGET_TOLERANCE) ||
        !v5_linuxcncrsh_home_all_axes_moved(moved_axes)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_REAL_MOVE_NOT_CONFIRMED");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    ok = v5_linuxcncrsh_send_request_text(fd, "Set Mode Manual", 0, 0U) &&
         v5_linuxcncrsh_send_request_text(fd, "Set Home -1", 0, 0U);
    if (!ok || !v5_linuxcncrsh_wait_home_completion_actual(50U, 100000U)) {
        v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_HOMED_SYNC_FAILED_AFTER_REAL_MOVE");
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }
    v5_linuxcncrsh_copy_code(code_out, code_out_size, "BUS_HOME_REAL_MOVE_CONFIRMED");
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
        return v5_linuxcncrsh_send_home_sequence(config, 0, 0, 0, 0);
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
