#include "v5_command_gate_ipc.h"
#include "v5_command_gate_validator.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_native_first_point.h"
#include "v5_native_safety.h"
#include "v5_process_residency.h"
#include "v5_settings_apply.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define V5_COMMAND_GATE_CLIENT_IO_TIMEOUT_MS 250U

static volatile sig_atomic_t g_stop_requested;
static pthread_mutex_t g_linuxcncrsh_lock = PTHREAD_MUTEX_INITIALIZER;
static V5LinuxcncrshConfig g_linuxcncrsh_config = {"127.0.0.1", 5007U, "EMC", "v5_command_gate", 1000U};
static char g_host[64] = "127.0.0.1";
static char g_password[64] = "EMC";
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = V5_COMMAND_GATE_SOCKET_PATH;

static void on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
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

static int set_socket_timeout_ms(int fd, unsigned int timeout_ms)
{
    struct timeval timeout;
    if (timeout_ms == 0U) {
        timeout_ms = 1U;
    }
    timeout.tv_sec = (time_t)(timeout_ms / 1000U);
    timeout.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

static void linuxcncrsh_lock(void)
{
    (void)pthread_mutex_lock(&g_linuxcncrsh_lock);
}

static void linuxcncrsh_unlock(void)
{
    (void)pthread_mutex_unlock(&g_linuxcncrsh_lock);
}

static int read_all(int fd, void *data, size_t size)
{
    char *p = (char *)data;
    while (size > 0U) {
        ssize_t n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) {
            if (g_stop_requested) return 0;
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

static int write_all(int fd, const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            if (g_stop_requested) return 0;
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

static void response_init(V5CommandGateIpcResponseFrame *response)
{
    memset(response, 0, sizeof(*response));
    response->magic = V5_COMMAND_GATE_IPC_MAGIC;
    response->version = V5_COMMAND_GATE_IPC_VERSION;
    response->size = (uint32_t)sizeof(*response);
    response->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
}

static void fill_safety_readback(V5CommandGateIpcResponseFrame *response)
{
    int estop_active = 0;
    int machine_enabled = 0;
    if (v5_linuxcncrsh_probe_estop(&g_linuxcncrsh_config, &estop_active, 0, 0)) {
        response->safety_estop_known = 1;
        response->safety_estop_active = estop_active ? 1 : 0;
    }
    if (v5_linuxcncrsh_probe_machine_enabled(&g_linuxcncrsh_config, &machine_enabled, 0, 0)) {
        response->machine_enable_known = 1;
        response->machine_enabled = machine_enabled ? 1 : 0;
    }
}

static int fixed_text_has_nul(const char *text, size_t cap)
{
    return text && memchr(text, '\0', cap) != 0;
}

static void response_copy_settings_result(
    V5CommandGateIpcResponseFrame *response,
    const V5SettingsApplyAxisCommitResult *result)
{
    if (!response || !result) {
        return;
    }
    response->settings_owner_written = result->owner_written ? 1 : 0;
    response->settings_source_readback_confirmed = result->source_readback_confirmed ? 1 : 0;
    response->settings_restart_pending = result->restart_pending ? 1 : 0;
    response->settings_scale_chain_attempted = result->scale_chain.attempted ? 1 : 0;
    response->settings_scale_recomputed = result->scale_chain.scale_recomputed ? 1 : 0;
    response->settings_raw_limits_recomputed = result->scale_chain.raw_limits_recomputed ? 1 : 0;
    response->settings_effective_scale = result->scale_chain.effective_scale;
    response->settings_raw_zero_position = result->scale_chain.raw_zero_position;
    response->settings_raw_min_limit = result->scale_chain.raw_min_limit;
    response->settings_raw_max_limit = result->scale_chain.raw_max_limit;
    copy_cstr(response->settings_readback_value, sizeof(response->settings_readback_value), result->readback_value);
    copy_cstr(response->settings_scale_chain_code, sizeof(response->settings_scale_chain_code), result->scale_chain.code);
}

static void response_copy_native_safety_result(
    V5CommandGateIpcResponseFrame *response,
    const V5NativeSafetyResult *result)
{
    if (!response || !result) {
        return;
    }
    response->safety_estop_known = result->safety_estop_known ? 1 : 0;
    response->safety_estop_active = result->safety_estop_active ? 1 : 0;
    response->machine_enable_known = result->machine_enable_known ? 1 : 0;
    response->machine_enabled = result->machine_enabled ? 1 : 0;
    response->machine_on_requested = result->machine_on_requested ? 1 : 0;
    response->machine_on_status = (int32_t)result->machine_on_status;
    copy_cstr(response->readback_code, sizeof(response->readback_code), result->code);
}

static int owner_is_allowed(const char *owner)
{
    return owner &&
        (strcmp(owner, "native_linuxcncrsh") == 0 ||
         strcmp(owner, "native_home_mode_gate") == 0 ||
         strcmp(owner, "native_safety") == 0 ||
         strcmp(owner, "native_first_point") == 0);
}

static void execute_request(const V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcResponseFrame *response)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    int status = V5_COMMAND_GATE_SEND_INVALID;
    char reject_reason[64];
    response_init(response);
    reject_reason[0] = '\0';
    if (!v5_command_gate_validate_execute_frame(frame, &request, reject_reason, sizeof(reject_reason)) ||
        !v5_command_gate_prepare(&request, &prepared) || !prepared.accepted || !owner_is_allowed(prepared.owner)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        copy_cstr(response->readback_code, sizeof(response->readback_code),
                  reject_reason[0] ? reject_reason : "COMMAND_GATE_REJECTED");
        return;
    }

    if (request.kind == V5_COMMAND_ESTOP_FORCE && strcmp(prepared.owner, "native_safety") == 0) {
        V5NativeSafetyResult native_result;
        copy_cstr(response->command_line, sizeof(response->command_line), "native_safety.estop_force");
        status = v5_native_safety_estop_force(&native_result);
        response_copy_native_safety_result(response, &native_result);
        response->send_status = (int32_t)status;
        response->executed = status == V5_NATIVE_SAFETY_SEND_SENT ? 1 : 0;
        return;
    }
    if (request.kind == V5_COMMAND_ESTOP_RESET && strcmp(prepared.owner, "native_safety") == 0) {
        V5NativeSafetyResult native_result;
        copy_cstr(response->command_line, sizeof(response->command_line), "native_safety.estop_reset");
        status = v5_native_safety_estop_reset(&native_result);
        response_copy_native_safety_result(response, &native_result);
        response->send_status = (int32_t)status;
        response->executed = status == V5_NATIVE_SAFETY_SEND_SENT ? 1 : 0;
        return;
    }

    linuxcncrsh_lock();
    if (request.kind == V5_COMMAND_FIRST_POINT && strcmp(prepared.owner, "native_first_point") == 0) {
        if (!v5_native_first_point_format_report(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            linuxcncrsh_unlock();
            return;
        }
        status = v5_native_first_point_send(&g_linuxcncrsh_config, &prepared, &request);
    } else if (request.kind == V5_COMMAND_HOME && strcmp(prepared.owner, "native_home_mode_gate") == 0) {
        (void)v5_linuxcncrsh_format_home_sequence(response->command_line, sizeof(response->command_line));
        status = v5_linuxcncrsh_send_home_sequence(&g_linuxcncrsh_config, 0, 0);
        copy_cstr(
            response->readback_code,
            sizeof(response->readback_code),
            status == V5_LINUXCNCRSH_SEND_SENT ? "HOME_NATIVE_ALL_HOMED" : "HOME_NATIVE_ALL_HOMED_TIMEOUT");
    } else if (request.kind == V5_COMMAND_RTCP_SET && strcmp(prepared.owner, "native_linuxcncrsh") == 0) {
        if (!v5_linuxcncrsh_format_line(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            linuxcncrsh_unlock();
            return;
        }
        status = v5_linuxcncrsh_send_rtcp_sequence(&g_linuxcncrsh_config, request.enabled_value);
    } else {
        if (!v5_linuxcncrsh_format_line(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            linuxcncrsh_unlock();
            return;
        }
        status = v5_linuxcncrsh_send_line(&g_linuxcncrsh_config, response->command_line);
    }
    response->send_status = (int32_t)status;
    response->executed = status == V5_LINUXCNCRSH_SEND_SENT ? 1 : 0;
    linuxcncrsh_unlock();
}

static void execute_settings_axis_commit(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcResponseFrame *response)
{
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult result;
    int ok;
    response_init(response);
    if (!fixed_text_has_nul(frame->settings_project_root, sizeof(frame->settings_project_root)) ||
        !fixed_text_has_nul(frame->settings_axis, sizeof(frame->settings_axis)) ||
        !fixed_text_has_nul(frame->settings_field_key, sizeof(frame->settings_field_key)) ||
        !fixed_text_has_nul(frame->settings_field_name, sizeof(frame->settings_field_name)) ||
        !fixed_text_has_nul(frame->settings_value_text, sizeof(frame->settings_value_text)) ||
        !frame->settings_project_root[0] ||
        !frame->settings_axis[0] ||
        !frame->settings_field_key[0] ||
        !frame->settings_field_name[0] ||
        !frame->settings_value_text[0]) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        copy_cstr(response->readback_code, sizeof(response->readback_code), "SETTINGS_AXIS_COMMIT_BAD_REQUEST");
        return;
    }
    memset(&request, 0, sizeof(request));
    request.project_root = frame->settings_project_root;
    request.axis = frame->settings_axis;
    request.axis_index = frame->settings_axis_index;
    request.field_key = frame->settings_field_key;
    request.field_name = frame->settings_field_name;
    request.value_text = frame->settings_value_text;
    request.owner_generation = frame->settings_owner_generation;
    request.readback_token = frame->settings_readback_token;
    ok = v5_settings_apply_commit_axis_value(&request, &result);
    response_copy_settings_result(response, &result);
    if (ok && result.source_readback_confirmed) {
        response->send_status = V5_COMMAND_GATE_SEND_SENT;
        response->executed = 1;
        copy_cstr(response->readback_code, sizeof(response->readback_code), "SETTINGS_AXIS_COMMIT_OK");
    } else {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        response->executed = 0;
        copy_cstr(response->readback_code, sizeof(response->readback_code),
                  result.scale_chain.code[0] ? result.scale_chain.code : "SETTINGS_AXIS_COMMIT_REJECTED");
    }
}

static void handle_frame(const V5CommandGateIpcRequestFrame *request, V5CommandGateIpcResponseFrame *response)
{
    char reject_reason[64];
    response_init(response);
    reject_reason[0] = '\0';
    if (!request || request->magic != V5_COMMAND_GATE_IPC_MAGIC || request->version != V5_COMMAND_GATE_IPC_VERSION ||
        request->size != (uint32_t)sizeof(*request)) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        copy_cstr(response->readback_code, sizeof(response->readback_code), "BAD_ENVELOPE");
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY) {
        if (!v5_command_gate_validate_envelope(request, V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY, reject_reason, sizeof(reject_reason))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            copy_cstr(response->readback_code, sizeof(response->readback_code), reject_reason);
            return;
        }
        linuxcncrsh_lock();
        fill_safety_readback(response);
        linuxcncrsh_unlock();
        response->send_status = (response->safety_estop_known || response->machine_enable_known) ? V5_COMMAND_GATE_SEND_SENT : V5_COMMAND_GATE_SEND_UNAVAILABLE;
        response->executed = 0;
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_EXECUTE) {
        execute_request(request, response);
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_COMMIT) {
        execute_settings_axis_commit(request, response);
        return;
    }
    response->send_status = V5_COMMAND_GATE_SEND_INVALID;
    copy_cstr(response->readback_code, sizeof(response->readback_code), "BAD_OPCODE");
}

static void serve_client(int client_fd)
{
    V5CommandGateIpcRequestFrame request;
    V5CommandGateIpcResponseFrame response;
    if (g_stop_requested) {
        return;
    }
    if (!read_all(client_fd, &request, sizeof(request))) {
        return;
    }
    handle_frame(&request, &response);
    if (!write_all(client_fd, &response, sizeof(response))) {
        return;
    }
}

static void *serve_client_thread(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    serve_client(client_fd);
    close(client_fd);
    return 0;
}

static int make_listener(void)
{
    struct sockaddr_un addr;
    int fd;
    mkdir("/run/8ax_v5_product_ui", 0755);
    unlink(g_socket_path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_socket_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    chmod(g_socket_path, 0660);
    if (listen(fd, 16) != 0) {
        close(fd);
        unlink(g_socket_path);
        return -1;
    }
    return fd;
}

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            copy_cstr(g_socket_path, sizeof(g_socket_path), argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            copy_cstr(g_host, sizeof(g_host), argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.port = (unsigned short)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            copy_cstr(g_password, sizeof(g_password), argv[++i]);
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            g_linuxcncrsh_config.timeout_ms = (unsigned int)atoi(argv[++i]);
        }
    }
    g_linuxcncrsh_config.host = g_host;
    g_linuxcncrsh_config.connect_password = g_password;
    g_linuxcncrsh_config.client_name = "v5_command_gate";
    if (g_linuxcncrsh_config.timeout_ms == 0U) {
        g_linuxcncrsh_config.timeout_ms = 1000U;
    }
}

int main(int argc, char **argv)
{
    int listen_fd;
    parse_args(argc, argv);
    if (!v5_process_residency_lock("v5_command_gate_server")) {
        return 3;
    }
    if (!v5_linuxcncrsh_gate_preconnect(&g_linuxcncrsh_config)) {
        fprintf(stderr, "v5_command_gate_server linuxcncrsh preconnect failed: %s:%u\n",
                g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
        return 4;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    listen_fd = make_listener();
    if (listen_fd < 0) {
        perror("v5_command_gate_server listen");
        return 1;
    }
    printf("v5_command_gate_server running socket=%s linuxcncrsh=%s:%u\n", g_socket_path, g_linuxcncrsh_config.host, g_linuxcncrsh_config.port);
    fflush(stdout);
    while (!g_stop_requested) {
        int client_fd = accept(listen_fd, 0, 0);
        pthread_t thread;
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        (void)set_socket_timeout_ms(client_fd, V5_COMMAND_GATE_CLIENT_IO_TIMEOUT_MS);
        if (pthread_create(&thread, 0, serve_client_thread, (void *)(intptr_t)client_fd) != 0) {
            close(client_fd);
            continue;
        }
        (void)pthread_detach(thread);
    }
    close(listen_fd);
    unlink(g_socket_path);
    return 0;
}
