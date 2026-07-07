#ifndef V5_COMMAND_GATE_VALIDATOR_H
#define V5_COMMAND_GATE_VALIDATOR_H

#include "v5_command_gate_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

int v5_command_gate_validate_envelope(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandGateIpcOp expected_op,
    char *reason,
    size_t reason_cap);

int v5_command_gate_validate_execute_frame(
    const V5CommandGateIpcRequestFrame *frame,
    V5CommandRequest *request,
    char *reason,
    size_t reason_cap);

#ifdef __cplusplus
}
#endif

#endif
