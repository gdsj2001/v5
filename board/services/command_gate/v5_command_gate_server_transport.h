#ifndef V5_COMMAND_GATE_SERVER_TRANSPORT_H
#define V5_COMMAND_GATE_SERVER_TRANSPORT_H

#include "v5_command_gate_ipc.h"

#include <signal.h>

typedef void (*V5CommandGateServerFrameHandler)(
    const V5CommandGateIpcRequestFrame *request,
    V5CommandGateIpcResponseFrame *response,
    void *context);

typedef struct V5CommandGateServerTransport {
    int listen_fd;
    const char *socket_path;
    volatile sig_atomic_t *stop_requested;
    unsigned int timeout_ms;
    V5CommandGateServerFrameHandler handler;
    void *handler_context;
} V5CommandGateServerTransport;

int v5_command_gate_server_transport_open(
    V5CommandGateServerTransport *transport,
    const char *socket_path,
    volatile sig_atomic_t *stop_requested,
    unsigned int timeout_ms,
    V5CommandGateServerFrameHandler handler,
    void *handler_context);
void v5_command_gate_server_transport_serve(
    V5CommandGateServerTransport *transport);
void v5_command_gate_server_transport_close(
    V5CommandGateServerTransport *transport);

#endif
