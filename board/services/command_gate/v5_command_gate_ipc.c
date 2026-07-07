#include "v5_command_gate_ipc.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static int g_gate_fd = -1;

void v5_command_gate_result_init(V5CommandGateResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
}

static void copy_cstr(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void result_from_frame(V5CommandGateResult *result, const V5CommandGateIpcResponseFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->send_status = frame->send_status;
    result->executed = frame->executed;
    result->machine_on_requested = frame->machine_on_requested;
    result->machine_on_status = frame->machine_on_status;
    result->safety_estop_known = frame->safety_estop_known;
    result->safety_estop_active = frame->safety_estop_active;
    result->machine_enable_known = frame->machine_enable_known;
    result->machine_enabled = frame->machine_enabled;
    copy_cstr(result->command_line, sizeof(result->command_line), frame->command_line);
    copy_cstr(result->readback_code, sizeof(result->readback_code), frame->readback_code);
}

#ifndef _WIN32
static void gate_close(void)
{
    if (g_gate_fd >= 0) {
        close(g_gate_fd);
    }
    g_gate_fd = -1;
}

static int set_socket_timeout(int fd, unsigned int timeout_ms)
{
    struct timeval tv;
    if (timeout_ms == 0U) {
        timeout_ms = 1000U;
    }
    tv.tv_sec = (time_t)(timeout_ms / 1000U);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static int read_all(int fd, void *data, size_t size)
{
    char *p = (char *)data;
    while (size > 0U) {
        ssize_t n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static int gate_connect(unsigned int timeout_ms)
{
    struct sockaddr_un addr;
    int fd;
    if (g_gate_fd >= 0) {
        (void)set_socket_timeout(g_gate_fd, timeout_ms);
        return g_gate_fd;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    (void)set_socket_timeout(fd, timeout_ms);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", V5_COMMAND_GATE_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    g_gate_fd = fd;
    return g_gate_fd;
}

static int transact(const V5CommandGateIpcRequestFrame *request, V5CommandGateResult *result, unsigned int timeout_ms)
{
    V5CommandGateIpcResponseFrame response;
    int fd;
    int attempt;
    v5_command_gate_result_init(result);
    if (!request) {
        return 0;
    }
    for (attempt = 0; attempt < 2; ++attempt) {
        fd = gate_connect(timeout_ms);
        if (fd < 0) {
            return 0;
        }
        if (write_all(fd, request, sizeof(*request)) && read_all(fd, &response, sizeof(response)) &&
            response.magic == V5_COMMAND_GATE_IPC_MAGIC &&
            response.version == V5_COMMAND_GATE_IPC_VERSION &&
            response.size == (uint32_t)sizeof(response)) {
            result_from_frame(result, &response);
            return 1;
        }
        gate_close();
    }
    return 0;
}
#endif

static void init_request_frame(V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcOp op)
{
    memset(frame, 0, sizeof(*frame));
    frame->magic = V5_COMMAND_GATE_IPC_MAGIC;
    frame->version = V5_COMMAND_GATE_IPC_VERSION;
    frame->size = (uint32_t)sizeof(*frame);
    frame->op = (uint32_t)op;
}

int v5_command_gate_send_prepared(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)prepared;
    (void)request;
    (void)timeout_ms;
    v5_command_gate_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    unsigned int i;
    v5_command_gate_result_init(result);
    if (!prepared || !prepared->accepted || !request || request->kind == V5_COMMAND_UI_LOCAL) {
        if (result) {
            result->send_status = V5_COMMAND_GATE_SEND_INVALID;
        }
        return 0;
    }
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_EXECUTE);
    frame.kind = (int32_t)request->kind;
    frame.index_value = (int32_t)request->index_value;
    frame.enabled_value = (int32_t)request->enabled_value;
    frame.axis_mask = request->axis_mask;
    frame.axis_value = request->axis_value;
    frame.increment_value = request->increment_value;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        frame.point_axis[i] = request->point_axis[i];
    }
    copy_cstr(frame.text_value, sizeof(frame.text_value), request->text_value);
    copy_cstr(frame.secondary_text_value, sizeof(frame.secondary_text_value), request->secondary_text_value);
    copy_cstr(frame.mode_value, sizeof(frame.mode_value), request->mode_value);
    return transact(&frame, result, timeout_ms);
#endif
}

int v5_command_gate_probe_safety(V5CommandGateResult *result, unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)timeout_ms;
    v5_command_gate_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY);
    return transact(&frame, result, timeout_ms);
#endif
}
