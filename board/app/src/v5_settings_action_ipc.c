#include "v5_settings_action_ipc.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static void set_error(V5SettingsActionIpcError *error_out, V5SettingsActionIpcError error)
{
    if (error_out) {
        *error_out = error;
    }
}

static int write_all(int fd, const char *data, size_t size)
{
    size_t offset = 0U;
    while (offset < size) {
        ssize_t written = write(fd, data + offset, size - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return 0;
        }
        offset += (size_t)written;
    }
    return 1;
}

int v5_settings_action_ipc_request(const char *socket_path,
                                   const char *request,
                                   unsigned int timeout_ms,
                                   char *response,
                                   size_t response_size,
                                   V5SettingsActionIpcError *error_out)
{
    struct sockaddr_un addr;
    struct timeval timeout;
    int fd;
    ssize_t received;

    set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_NONE);
    if (!socket_path || !socket_path[0] || !request || !response || response_size < 2U ||
        strlen(socket_path) >= sizeof(addr.sun_path)) {
        set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_ARGUMENT);
        return 0;
    }
    response[0] = '\0';
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_SOCKET);
        return 0;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, socket_path, strlen(socket_path) + 1U);
    timeout.tv_sec = (time_t)(timeout_ms / 1000U);
    timeout.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_CONNECT);
        close(fd);
        return 0;
    }
    if (!write_all(fd, request, strlen(request))) {
        set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_WRITE);
        close(fd);
        return 0;
    }
    do {
        received = read(fd, response, response_size - 1U);
    } while (received < 0 && errno == EINTR);
    close(fd);
    if (received <= 0) {
        response[0] = '\0';
        set_error(error_out, V5_SETTINGS_ACTION_IPC_ERROR_RESPONSE);
        return 0;
    }
    response[received] = '\0';
    return 1;
}

int v5_settings_action_ipc_response_accepted(const char *response)
{
    return response &&
           (strstr(response, "\"accepted\": true") != 0 ||
            strstr(response, "\"accepted\":true") != 0);
}
