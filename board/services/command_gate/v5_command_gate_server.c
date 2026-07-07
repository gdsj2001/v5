#include "v5_command_gate_ipc.h"
#include "v5_command_gate_validator.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_native_first_point.h"
#include "v5_process_residency.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop_requested;
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
        ssize_t n = send(fd, p, size, 0);
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
    V5LinuxcncrshSendStatus status = V5_LINUXCNCRSH_SEND_INVALID;
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

    if (request.kind == V5_COMMAND_FIRST_POINT && strcmp(prepared.owner, "native_first_point") == 0) {
        if (!v5_native_first_point_format_report(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            return;
        }
        status = v5_native_first_point_send(&g_linuxcncrsh_config, &prepared, &request);
    } else if (request.kind == V5_COMMAND_HOME && strcmp(prepared.owner, "native_home_mode_gate") == 0) {
        (void)v5_linuxcncrsh_format_home_sequence(response->command_line, sizeof(response->command_line));
        status = v5_linuxcncrsh_send_home_sequence(&g_linuxcncrsh_config, 0, 0);
    } else if (request.kind == V5_COMMAND_ESTOP_RESET && strcmp(prepared.owner, "native_safety") == 0) {
        (void)v5_linuxcncrsh_format_estop_reset_sequence(response->command_line, sizeof(response->command_line));
        status = v5_linuxcncrsh_send_estop_reset_sequence(&g_linuxcncrsh_config, &response->machine_on_status, &response->machine_on_requested);
    } else if (request.kind == V5_COMMAND_ESTOP_FORCE && strcmp(prepared.owner, "native_safety") == 0) {
        (void)v5_linuxcncrsh_format_line(&prepared, &request, response->command_line, sizeof(response->command_line));
        status = v5_linuxcncrsh_send_estop_force_sequence(&g_linuxcncrsh_config);
    } else {
        if (!v5_linuxcncrsh_format_line(&prepared, &request, response->command_line, sizeof(response->command_line))) {
            response->send_status = V5_COMMAND_GATE_SEND_INVALID;
            return;
        }
        status = v5_linuxcncrsh_send_line(&g_linuxcncrsh_config, response->command_line);
    }
    response->send_status = (int32_t)status;
    response->executed = status == V5_LINUXCNCRSH_SEND_SENT ? 1 : 0;
    if (strcmp(prepared.owner, "native_safety") == 0) {
        fill_safety_readback(response);
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
        fill_safety_readback(response);
        response->send_status = (response->safety_estop_known || response->machine_enable_known) ? V5_COMMAND_GATE_SEND_SENT : V5_COMMAND_GATE_SEND_UNAVAILABLE;
        response->executed = 0;
        return;
    }
    if (request->op == V5_COMMAND_GATE_IPC_OP_EXECUTE) {
        execute_request(request, response);
        return;
    }
    response->send_status = V5_COMMAND_GATE_SEND_INVALID;
    copy_cstr(response->readback_code, sizeof(response->readback_code), "BAD_OPCODE");
}

static void serve_client(int client_fd)
{
    while (!g_stop_requested) {
        V5CommandGateIpcRequestFrame request;
        V5CommandGateIpcResponseFrame response;
        if (!read_all(client_fd, &request, sizeof(request))) {
            break;
        }
        handle_frame(&request, &response);
        if (!write_all(client_fd, &response, sizeof(response))) {
            break;
        }
    }
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
    if (listen(fd, 4) != 0) {
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
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        serve_client(client_fd);
        close(client_fd);
    }
    close(listen_fd);
    unlink(g_socket_path);
    return 0;
}
