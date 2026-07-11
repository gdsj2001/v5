#ifndef V5_SETTINGS_ACTION_IPC_H
#define V5_SETTINGS_ACTION_IPC_H

#include <stddef.h>

typedef enum V5SettingsActionIpcError {
    V5_SETTINGS_ACTION_IPC_ERROR_NONE = 0,
    V5_SETTINGS_ACTION_IPC_ERROR_ARGUMENT,
    V5_SETTINGS_ACTION_IPC_ERROR_SOCKET,
    V5_SETTINGS_ACTION_IPC_ERROR_CONNECT,
    V5_SETTINGS_ACTION_IPC_ERROR_WRITE,
    V5_SETTINGS_ACTION_IPC_ERROR_RESPONSE
} V5SettingsActionIpcError;

int v5_settings_action_ipc_request(const char *socket_path,
                                   const char *request,
                                   unsigned int timeout_ms,
                                   char *response,
                                   size_t response_size,
                                   V5SettingsActionIpcError *error_out);
int v5_settings_action_ipc_response_accepted(const char *response);

#endif
