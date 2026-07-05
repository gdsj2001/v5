#include "v5_linuxcncrsh_client.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

int v5_linuxcncrsh_format_line(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size)
{
    int rc;
    static const char *wcs_codes[] = {"G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"};

    if (!prepared || !request || !out || out_size == 0U || !prepared->accepted) {
        return 0;
    }

    switch (request->kind) {
    case V5_COMMAND_PROGRAM_OPEN:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set Open %s", request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_START:
        rc = snprintf(out, out_size, "Set Run 0");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_RESUME:
        rc = snprintf(out, out_size, "Set Resume");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_HOME:
        rc = snprintf(out, out_size, "Set Home -1");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_PAUSE:
        rc = snprintf(out, out_size, "Set Pause");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_ESTOP_FORCE:
        rc = snprintf(out, out_size, "Set Estop");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_ESTOP_RESET:
        rc = snprintf(out, out_size, "Set Estop Reset");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_WCS_SELECT:
        if (request->index_value < 0 || request->index_value > 8) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set MDI %s", wcs_codes[request->index_value]);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_WORK_ZERO:
        if (!request->text_value || !request->text_value[0]) {
            return 0;
        }
        if (request->index_value < 1 || request->index_value > 9) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set MDI G10 L20 P%d %s0", request->index_value, request->text_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_G92_CLEAR:
        rc = snprintf(out, out_size, "Set MDI G92.1");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_RTCP_SET:
        rc = snprintf(out, out_size, "Set MDI %s", request->enabled_value ? "M128" : "M129");
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_FEED_OVERRIDE_SET:
        if (request->index_value < 0 || request->index_value > 200) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set FeedOverride %d", request->index_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    case V5_COMMAND_SPINDLE_OVERRIDE_SET:
        if (request->index_value < 0 || request->index_value > 200) {
            return 0;
        }
        rc = snprintf(out, out_size, "Set SpindleOverride %d", request->index_value);
        return v5_linuxcncrsh_format_ok(rc, out_size);
    default:
        return 0;
    }
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

static int v5_linuxcncrsh_contains_machine_state(const char *text)
{
    char upper[1024];
    size_t i;
    size_t n;
    if (!text) {
        return 0;
    }
    n = strlen(text);
    if (n >= sizeof(upper)) {
        n = sizeof(upper) - 1U;
    }
    for (i = 0U; i < n; ++i) {
        upper[i] = (char)toupper((unsigned char)text[i]);
    }
    upper[n] = '\0';
    return strstr(upper, "MACHINE ON") != 0 || strstr(upper, "MACHINE OFF") != 0;
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
    char hello[160];
    char transcript[1024];
    char command[64];
    int ok;

    if (out && out_size > 0U) {
        out[0] = '\0';
    }
    fd = v5_linuxcncrsh_open_socket(config);
    if (fd < 0) {
        return 0;
    }

    snprintf(
        hello,
        sizeof(hello),
        "Hello %s %s 1.0\n",
        v5_linuxcncrsh_password(config),
        v5_linuxcncrsh_client_name(config));
    snprintf(command, sizeof(command), "Get Machine\nQuit\n");

    ok = v5_linuxcncrsh_send_all(fd, hello) && v5_linuxcncrsh_send_all(fd, command);
    if (ok) {
        (void)v5_linuxcncrsh_recv_text(fd, transcript, sizeof(transcript));
        ok = v5_linuxcncrsh_contains_machine_state(transcript);
        if (out && out_size > 0U) {
            snprintf(out, out_size, "%s", transcript);
        }
    }
    close(fd);
    return ok;
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
    char hello[160];
    char command[512];

    if (!line || !line[0]) {
        return V5_LINUXCNCRSH_SEND_INVALID;
    }

    fd = v5_linuxcncrsh_open_socket(config);
    if (fd < 0) {
        return V5_LINUXCNCRSH_SEND_UNAVAILABLE;
    }

    snprintf(
        hello,
        sizeof(hello),
        "Hello %s %s 1.0\n",
        v5_linuxcncrsh_password(config),
        v5_linuxcncrsh_client_name(config));
    snprintf(command, sizeof(command), "%s\nQuit\n", line);

    if (!v5_linuxcncrsh_send_all(fd, hello) || !v5_linuxcncrsh_send_all(fd, command)) {
        close(fd);
        return V5_LINUXCNCRSH_SEND_IO_ERROR;
    }

    close(fd);
    return V5_LINUXCNCRSH_SEND_SENT;
#endif
}

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_prepared(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepa