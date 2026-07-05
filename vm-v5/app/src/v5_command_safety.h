#ifndef V5_COMMAND_SAFETY_H
#define V5_COMMAND_SAFETY_H

#include "v5_command_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_estop_force_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_estop_reset_prepare(V5CommandPrepared *prepared, V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
