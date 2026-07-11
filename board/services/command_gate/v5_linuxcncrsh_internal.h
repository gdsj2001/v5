#ifndef V5_LINUXCNCRSH_INTERNAL_H
#define V5_LINUXCNCRSH_INTERNAL_H

#include "v5_linuxcncrsh_client.h"

int v5_linuxcncrsh_format_ok(int rc, size_t out_size);

#ifndef _WIN32
int v5_linuxcncrsh_send_all(int fd, const char *text);
void v5_linuxcncrsh_drain_pending(int fd);
void v5_linuxcncrsh_recv_available(int fd, char *out, size_t *used, size_t out_size);
int v5_linuxcncrsh_line_span_equals(
    const char *start, const char *end, const char *wanted);
void v5_linuxcncrsh_gate_close(void);
int v5_linuxcncrsh_gate_connect(const V5LinuxcncrshConfig *config);
int v5_linuxcncrsh_send_request_text(
    int fd, const char *request, char *out, size_t out_size);
int v5_linuxcncrsh_send_fifo_commands(int fd, const char *line);
int v5_linuxcncrsh_wait_machine_enabled_actual(
    const V5LinuxcncrshConfig *config,
    int expected_enabled,
    unsigned int attempts,
    unsigned int delay_us);
#endif

#endif
