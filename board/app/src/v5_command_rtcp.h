#ifndef V5_COMMAND_RTCP_H
#define V5_COMMAND_RTCP_H

#include "v5_command_gate.h"
#include "v5_native_readback.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_rtcp_prepare(int enabled, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_rtcp_toggle_prepare(const V5NativeReadback *readback, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_rtcp_actual_known(const V5NativeReadback *readback, int *enabled_out);

#ifdef __cplusplus
}
#endif

#endif
