#include "v5_remote_input_ipc.h"

#include "v5_lvgl_remote_input.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define V5_REMOTE_RUN_DIR "/run/8ax_v5_product_ui"
#define V5_REMOTE_INPUT_FIFO_PATH V5_REMOTE_RUN_DIR "/remote_input"

static int g_input_fd = -1;
static char g_input_buffer[4096];
static size_t g_input_buffer_len;

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

static int ensure_input_fifo(void)
{
    int fd;
    if (g_input_fd >= 0) {
        return 1;
    }
    if (mkdir(V5_REMOTE_RUN_DIR, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    if (mkfifo(V5_REMOTE_INPUT_FIFO_PATH, 0600) != 0 && errno != EEXIST) {
        return 0;
    }
    fd = open(V5_REMOTE_INPUT_FIFO_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    g_input_fd = fd;
    return 1;
}

static void handle_input_line(const char *line)
{
    char type[40];
    char phase[24];
    long long x = -1;
    long long y = -1;
    if (!json_get_string(line, "type", type, sizeof(type))) {
        return;
    }
    if (strcmp(type, "pointer_event") != 0) {
        return;
    }
    if (!json_get_string(line, "phase", phase, sizeof(phase)) ||
        !json_get_long(line, "x", &x) ||
        !json_get_long(line, "y", &y)) {
        (void)v5_lvgl_remote_input_pointer_event("invalid", -1, -1);
        return;
    }
    (void)v5_lvgl_remote_input_pointer_event(phase, (int)x, (int)y);
}

static void process_buffer_lines(void)
{
    size_t start = 0U;
    size_t i;
    for (i = 0U; i < g_input_buffer_len; ++i) {
        if (g_input_buffer[i] == '\n') {
            g_input_buffer[i] = '\0';
            handle_input_line(g_input_buffer + start);
            start = i + 1U;
        }
    }
    if (start > 0U) {
        memmove(g_input_buffer, g_input_buffer + start, g_input_buffer_len - start);
        g_input_buffer_len -= start;
    }
    if (g_input_buffer_len == sizeof(g_input_buffer)) {
        g_input_buffer_len = 0U;
    }
}

int v5_remote_input_ipc_enabled(void)
{
    return v5_lvgl_remote_input_accepts_pointer();
}

int v5_remote_input_ipc_process(void)
{
    unsigned int reads = 0U;
    if (!v5_remote_input_ipc_enabled()) {
        return 1;
    }
    if (!ensure_input_fifo()) {
        return 0;
    }
    while (reads < 16U) {
        ssize_t n = read(g_input_fd,
                         g_input_buffer + g_input_buffer_len,
                         sizeof(g_input_buffer) - g_input_buffer_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close(g_input_fd);
            g_input_fd = -1;
            return 0;
        }
        if (n == 0) {
            break;
        }
        g_input_buffer_len += (size_t)n;
        process_buffer_lines();
        ++reads;
    }
    return 1;
}
