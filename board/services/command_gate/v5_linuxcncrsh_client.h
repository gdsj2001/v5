#ifndef V5_LINUXCNCRSH_CLIENT_H
#define V5_LINUXCNCRSH_CLIENT_H

#include "v5_command_gate.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5LinuxcncrshSendStatus {
    V5_LINUXCNCRSH_SEND_UNAVAILABLE = 0,
    V5_LINUXCNCRSH_SEND_SENT = 1,
    V5_LINUXCNCRSH_SEND_INVALID = -1,
    V5_LINUXCNCRSH_SEND_IO_ERROR = -2,
} V5LinuxcncrshSendStatus;

typedef struct V5LinuxcncrshConfig {
    const char *host;
    unsigned short port;
    const char *connect_password;
    const char *client_name;
    unsigned int timeout_ms;
} V5LinuxcncrshConfig;

int v5_linuxcncrsh_format_line(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_format_home_sequence(char *out, size_t out_size);

int v5_linuxcncrsh_probe_machine(
    const V5LinuxcncrshConfig *config,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_probe_machine_enabled(
    const V5LinuxcncrshConfig *config,
    int *enabled_out,
    char *out,
    size_t out_size);
int v5_native_probe_machine_enabled_actual(int *enabled_out);
int v5_linuxcncrsh_probe_estop(
    const V5LinuxcncrshConfig *config,
    int *active_out,
    char *out,
    size_t out_size);
int v5_linuxcncrsh_probe_active_driver_mode(char *out, size_t out_size);
int v5_linuxcncrsh_gate_preconnect(const V5LinuxcncrshConfig *config);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_line(
    const V5LinuxcncrshConfig *config,
    const char *line);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_machine_on_sequence(
    const V5LinuxcncrshConfig *config);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_home_sequence(
    const V5LinuxcncrshConfig *config,
    char *mode_out,
    size_t mode_out_size,
    char *code_out,
    size_t code_out_size);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_rtcp_sequence(
    const V5LinuxcncrshConfig *config,
    int enabled);

V5LinuxcncrshSendStatus v5_linuxcncrsh_send_prepared(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
