#ifndef V5_NATIVE_FIRST_POINT_H
#define V5_NATIVE_FIRST_POINT_H

#include "v5_command_gate.h"
#include "v5_linuxcncrsh_client.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int v5_native_first_point_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);

V5LinuxcncrshSendStatus v5_native_first_point_send(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
