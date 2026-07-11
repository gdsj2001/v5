#ifndef V5_COMMAND_GATE_SERVER_IO_H
#define V5_COMMAND_GATE_SERVER_IO_H

#include <signal.h>
#include <stddef.h>

int v5_command_gate_server_set_timeout(int fd, unsigned int timeout_ms);
int v5_command_gate_server_read_all(
    int fd, void *data, size_t size,
    const volatile sig_atomic_t *stop_requested);
int v5_command_gate_server_write_all(
    int fd, const void *data, size_t size,
    const volatile sig_atomic_t *stop_requested);

#endif
