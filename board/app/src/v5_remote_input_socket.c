#include "v5_remote_input_socket.h"

#include "v5_lvgl_remote_input.h"
#include "v5_remote_ws.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int g_input_fd = -1;
static char g_session_id[96];

static long long unix_time_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0LL;
    }
    return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

static const char *json_find_value(const char *json, const char *key)
{
    char pattern[64];
    const char *p;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p += strlen(pattern);
    while (*p && *p != ':') {
        ++p;
    }
    if (*p != ':') {
        return 0;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static int json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p = json_find_value(json, key);
    const char *end;
    size_t n;
    if (!p || *p != '\"' || out_size == 0U) {
        return 0;
    }
    ++p;
    end = p;
    while (*end && *end != '\"') {
        ++end;
    }
    if (*end != '\"') {
        return 0;
    }
    n = (size_t)(end - p);
    if (n >= out_size) {
        n = out_size - 1U;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static int json_get_long(const char *json, const char *key, long long *out)
{
    const char *p = json_find_value(json, key);
    char *end = 0;
    long long value;
    if (!p) {
        return 0;
    }
    value = strtoll(p, &end, 10);
    if (end == p) {
        return 0;
    }
    *out = value;
    return 1;
}

static void send_ack(const char *type, const char *session_id, long long seq, const char *phase, int accepted, const char *reason)
{
    char json[360];
    snprintf(json, sizeof(json),
             "{\"type\":\"%s\",\"session_id\":\"%s\",\"seq\":%lld,"
             "\"phase\":\"%s\",\"accepted\":%s,\"server_time_ms\":%lld,\"reason\":%s%s%s}",
             type,
             session_id ? session_id : "",
             seq,
             phase ? phase : "",
             accepted ? "true" : "false",
             unix_time_ms(),
             reason ? "\"" : "null",
             reason ? reason : "",
             reason ? "\"" : "");
    if (g_input_fd >= 0) {
        (void)v5_remote_ws_send_text(g_input_fd, json);
    }
}

static int handle_control_request(const char *json)
{
    char session_id[96];
    if (!json_get_string(json, "session_id", session_id, sizeof(session_id))) {
        send_ack("control_revoke", "", 0, "control", 0, "missing_session_id");
        return 0;
    }
    snprintf(g_session_id, sizeof(g_session_id), "%s", session_id);
    send_ack("control_grant", g_session_id, 0, "control", 1, 0);
    return 1;
}

static int handle_pointer_event(const char *json)
{
    char session_id[96];
    char phase[24];
    long long seq = 0;
    long long x = -1;
    long long y = -1;
    int ok;
    if (!json_get_string(json, "session_id", session_id, sizeof(session_id)) ||
        !json_get_string(json, "phase", phase, sizeof(phase)) ||
        !json_get_long(json, "seq", &seq) ||
        !json_get_long(json, "x", &x) ||
        !json_get_long(json, "y", &y)) {
        send_ack("pointer_reject", g_session_id, seq, "unknown", 0, "invalid_pointer_event");
        return 0;
    }
    if (g_session_id[0] && strcmp(session_id, g_session_id) != 0) {
        send_ack("pointer_reject", session_id, seq, phase, 0, "session_mismatch");
        return 0;
    }
    ok = v5_lvgl_remote_input_pointer_event(phase, (int)x, (int)y);
    send_ack(ok ? "pointer_ack" : "pointer_reject", session_id, seq, phase, ok, ok ? 0 : "remote_input_disabled");
    return ok;
}

int v5_remote_input_socket_enabled(void)
{
    return v5_lvgl_remote_input_accepts_pointer();
}

int v5_remote_input_socket_begin(int fd, const char *http_request)
{
    struct timeval tv;
    if (!v5_remote_input_socket_enabled()) {
        return 0;
    }
    if (!v5_remote_ws_accept(fd, http_request)) {
        return 0;
    }
    v5_remote_input_socket_close();
    tv.tv_sec = 0;
    tv.tv_usec = 150000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    g_input_fd = fd;
    g_session_id[0] = '\0';
    return 1;
}

int v5_remote_input_socket_fd(void)
{
    return g_input_fd;
}

void v5_remote_input_socket_close(void)
{
    if (g_input_fd >= 0) {
        close(g_input_fd);
        g_input_fd = -1;
    }
    g_session_id[0] = '\0';
}

void v5_remote_input_socket_process(void)
{
    char json[1024];
    char type[40];
    if (g_input_fd < 0) {
        return;
    }
    if (v5_remote_ws_recv_text(g_input_fd, json, sizeof(json)) <= 0) {
        v5_remote_input_socket_close();
        return;
    }
    if (!json_get_string(json, "type", type, sizeof(type))) {
        send_ack("pointer_reject", g_session_id, 0, "unknown", 0, "missing_type");
        return;
    }
    if (strcmp(type, "control_request") == 0) {
        (void)handle_control_request(json);
    } else if (strcmp(type, "pointer_event") == 0) {
        (void)handle_pointer_event(json);
    } else {
        send_ack("pointer_reject", g_session_id, 0, "unknown", 0, "unsupported_type");
    }
}
