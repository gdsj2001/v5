#ifndef V5_NATIVE_ROTARY_EQUIV_ZERO_H
#define V5_NATIVE_ROTARY_EQUIV_ZERO_H

#include "v5_linuxcncrsh_client.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_native_rotary_equiv_zero_format_report(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    char *out,
    size_t out_size);

V5LinuxcncrshSendStatus v5_native_rotary_equiv_zero_send(
    const V5LinuxcncrshConfig *config,
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
