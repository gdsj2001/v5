#include "v5_linuxcncrsh_internal.h"

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

#ifndef _WIN32
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
#endif

int v5_linuxcncrsh_format_ok(int rc, size_t out_size)
{
    return rc > 0 && (size_t)rc < out_size;
}


#ifndef _WIN32
static int g_v5_linuxcncrsh_fd = -1;
static char g_v5_linuxcncrsh_host[64];
static unsigned short g_v5_linuxcncrsh_port;
static char g_v5_linuxcncrsh_password[64];

int v5_linuxcncrsh_send_all(int fd, const char *text)
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

void v5_linuxcncrsh_drain_pending(int fd)
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

void v5_linuxcncrsh_recv_available(int fd, char *out, size_t *used, size_t out_size)
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


int v5_linuxcncrsh_line_span_equals(const char *start, const char *end, const char *wanted)
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

void v5_linuxcncrsh_gate_close(void)
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

int v5_linuxcncrsh_gate_connect(const V5LinuxcncrshConfig *config)
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


static int v5_linuxcncrsh_has_non_echo_line(const char *text, const char *request)
{
    const char *p;
    if (!text || !request) {
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
        while (start < end && isspace((unsigned char)*start)) {
            ++start;
        }
        while (end > start && isspace((unsigned char)*(end - 1))) {
            --end;
        }
        if (start < end && !v5_linuxcncrsh_line_span_equals(start, end, request)) {
            return 1;
        }
    }
    return 0;
}

static int v5_linuxcncrsh_recv_request_text(
    int fd,
    const char *request,
    char *out,
    size_t out_size)
{
    size_t used = 0U;
    int require_non_echo;
    if (fd < 0 || !request || !out || out_size == 0U) {
        return 0;
    }
    require_non_echo = strncasecmp(request, "Get ", 4U) == 0;
    out[0] = '\0';
    while (used + 1U < out_size) {
        ssize_t rc = recv(fd, out + used, out_size - used - 1U, 0);
        if (rc <= 0) {
            break;
        }
        used += (size_t)rc;
        out[used] = '\0';
        if (!require_non_echo || v5_linuxcncrsh_has_non_echo_line(out, request)) {
            break;
        }
    }
    if (used == 0U || (require_non_echo && !v5_linuxcncrsh_has_non_echo_line(out, request))) {
        return 0;
    }
    usleep(50000U);
    v5_linuxcncrsh_recv_available(fd, out, &used, out_size);
    return 1;
}

int v5_linuxcncrsh_send_request_text(int fd, const char *request, char *out, size_t out_size)
{
    char framed[768];
    char discard[512];
    char *response = out;
    size_t response_size = out_size;
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
    if (!response || response_size == 0U) {
        response = discard;
        response_size = sizeof(discard);
    }
    if (!v5_linuxcncrsh_recv_request_text(fd, request, response, response_size)) {
        return 0;
    }
    return !v5_linuxcncrsh_response_has_word(response, "NAK") &&
           !v5_linuxcncrsh_response_has_word(response, "ERROR");
}

int v5_linuxcncrsh_send_fifo_commands_with_sender(
    int fd,
    const char *line,
    V5LinuxcncrshCommandSender sender,
    void *user_data)
{
    const char *p = line;
    char command[384];

    if (!sender) {
        return 0;
    }
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
        if (!sender(fd, command, user_data)) {
            return 0;
        }
    }
    return 1;
}

static int v5_linuxcncrsh_send_fifo_command(
    int fd,
    const char *command,
    void *user_data)
{
    (void)user_data;
    return v5_linuxcncrsh_send_request_text(fd, command, 0, 0U);
}

int v5_linuxcncrsh_send_fifo_commands(int fd, const char *line)
{
    return v5_linuxcncrsh_send_fifo_commands_with_sender(
        fd, line, v5_linuxcncrsh_send_fifo_command, 0);
}
#endif

int v5_linuxcncrsh_gate_preconnect(const V5LinuxcncrshConfig *config)
{
#ifdef _WIN32
    (void)config;
    return 0;
#else
    return v5_linuxcncrsh_gate_connect(config) >= 0;
#endif
}
