#include "v5_command_gate_server_transport.h"

#include "v5_command_gate_server_io.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct V5CommandGateClientThread {
    int client_fd;
    volatile sig_atomic_t *stop_requested;
    V5CommandGateServerFrameHandler handler;
    void *handler_context;
} V5CommandGateClientThread;

static void serve_client(
    const V5CommandGateClientThread *client)
{
    V5CommandGateIpcRequestFrame request;
    V5CommandGateIpcResponseFrame response;
    if (!client || !client->handler || !client->stop_requested ||
        *client->stop_requested) return;
    if (!v5_command_gate_server_read_all(
            client->client_fd, &request, sizeof(request),
            client->stop_requested)) return;
    client->handler(&request, &response, client->handler_context);
    (void)v5_command_gate_server_write_all(
        client->client_fd, &response, sizeof(response),
        client->stop_requested);
}

static void *serve_client_thread(void *arg)
{
    V5CommandGateClientThread *client =
        (V5CommandGateClientThread *)arg;
    int client_fd;
    if (!client) return 0;
    client_fd = client->client_fd;
    serve_client(client);
    close(client_fd);
    free(client);
    return 0;
}

int v5_command_gate_server_transport_open(
    V5CommandGateServerTransport *transport,
    const char *socket_path,
    volatile sig_atomic_t *stop_requested,
    unsigned int timeout_ms,
    V5CommandGateServerFrameHandler handler,
    void *handler_context)
{
    struct sockaddr_un address;
    int fd;
    if (!transport || !socket_path || !socket_path[0] ||
        strlen(socket_path) >= sizeof(address.sun_path) ||
        !stop_requested || !handler) return 0;
    memset(transport, 0, sizeof(*transport));
    transport->listen_fd = -1;
    (void)mkdir("/run/8ax_v5_product_ui", 0755);
    unlink(socket_path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return 0;
    }
    (void)chmod(socket_path, 0660);
    if (listen(fd, 16) != 0) {
        close(fd);
        unlink(socket_path);
        return 0;
    }
    transport->listen_fd = fd;
    transport->socket_path = socket_path;
    transport->stop_requested = stop_requested;
    transport->timeout_ms = timeout_ms;
    transport->handler = handler;
    transport->handler_context = handler_context;
    return 1;
}

void v5_command_gate_server_transport_serve(
    V5CommandGateServerTransport *transport)
{
    if (!transport || transport->listen_fd < 0 ||
        !transport->stop_requested) return;
    while (!*transport->stop_requested) {
        V5CommandGateClientThread *client;
        pthread_t thread;
        int client_fd = accept(transport->listen_fd, 0, 0);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        (void)v5_command_gate_server_set_timeout(
            client_fd, transport->timeout_ms);
        client = (V5CommandGateClientThread *)malloc(sizeof(*client));
        if (!client) {
            close(client_fd);
            continue;
        }
        client->client_fd = client_fd;
        client->stop_requested = transport->stop_requested;
        client->handler = transport->handler;
        client->handler_context = transport->handler_context;
        if (pthread_create(&thread, 0, serve_client_thread, client) != 0) {
            free(client);
            close(client_fd);
            continue;
        }
        (void)pthread_detach(thread);
    }
}

void v5_command_gate_server_transport_close(
    V5CommandGateServerTransport *transport)
{
    if (!transport) return;
    if (transport->listen_fd >= 0) close(transport->listen_fd);
    transport->listen_fd = -1;
}
