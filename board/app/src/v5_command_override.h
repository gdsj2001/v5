#ifndef V5_COMMAND_OVERRIDE_H
#define V5_COMMAND_OVERRIDE_H

#include "v5_command_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_feed_override_prepare(int percent, V5CommandPrepared *prepared, V5CommandRequest *request);
int v5_command_spindle_override_prepare(int percent, V5CommandPrepared *prepared, V5CommandRequest *request);

#ifdef __cplusplus
}
#endif

#endif
