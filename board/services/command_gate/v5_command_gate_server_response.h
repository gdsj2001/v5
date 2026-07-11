#ifndef V5_COMMAND_GATE_SERVER_RESPONSE_H
#define V5_COMMAND_GATE_SERVER_RESPONSE_H

#include "v5_command_gate_ipc.h"
#include "v5_native_safety.h"

void v5_command_gate_response_copy_text(char *dst, size_t cap, const char *src);
void v5_command_gate_response_init(V5CommandGateIpcResponseFrame *response);
void v5_command_gate_response_fill_safety(V5CommandGateIpcResponseFrame *response);
int v5_command_gate_response_fixed_text_has_nul(const char *text, size_t cap);
void v5_command_gate_response_copy_settings(
    V5CommandGateIpcResponseFrame *response,
    const V5SettingsApplyAxisCommitResult *result);
void v5_command_gate_response_copy_native_safety(
    V5CommandGateIpcResponseFrame *response,
    const V5NativeSafetyResult *result);
int v5_command_gate_response_owner_is_allowed(const char *owner);

#endif
