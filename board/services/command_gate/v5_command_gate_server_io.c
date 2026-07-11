#include "v5_command_gate_server_io.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int v5_command_gate_server_set_timeout(int fd, unsigned int timeout_ms)
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

int v5_command_gate_server_read_all(int fd, void *data, size_t size,
    const volatile sig_atomic_t *stop_requested)
{
    char *p = (char *)data;
    while (size > 0U) {
        ssize_t n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) {
            if (stop_requested && *stop_requested) return 0;
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

int v5_command_gate_server_write_all(int fd, const void *data, size_t size,
    const volatile sig_atomic_t *stop_requested)
{
    const char *p = (const char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            if (stop_requested && *stop_requested) return 0;
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
