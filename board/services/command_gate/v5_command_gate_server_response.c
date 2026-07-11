#include "v5_command_gate_server_response.h"

#include <stdio.h>
#include <string.h>

void v5_command_gate_response_copy_text(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    snprintf(dst, cap, "%s", src);
}

void v5_command_gate_response_init(V5CommandGateIpcResponseFrame *response)
{
    memset(response, 0, sizeof(*response));
    response->magic = V5_COMMAND_GATE_IPC_MAGIC;
    response->version = V5_COMMAND_GATE_IPC_VERSION;
    response->size = (uint32_t)sizeof(*response);
    response->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
}

void v5_command_gate_response_copy_native_safety(
    V5CommandGateIpcResponseFrame *response,
    const V5NativeSafetyResult *result)
{
    if (!response || !result) {
        return;
    }
    response->safety_estop_known = result->safety_estop_known ? 1 : 0;
    response->safety_estop_active = result->safety_estop_active ? 1 : 0;
    response->machine_enable_known = result->machine_enable_known ? 1 : 0;
    response->machine_enabled = result->machine_enabled ? 1 : 0;
    response->machine_on_requested = result->machine_on_requested ? 1 : 0;
    response->machine_on_status = (int32_t)result->machine_on_status;
    v5_command_gate_response_copy_text(response->readback_code, sizeof(response->readback_code), result->code);
}

void v5_command_gate_response_fill_safety(V5CommandGateIpcResponseFrame *response)
{
    V5NativeSafetyResult native_result;
    if (v5_native_safety_read_status(&native_result) != V5_NATIVE_SAFETY_SEND_SENT) {
        return;
    }
    v5_command_gate_response_copy_native_safety(response, &native_result);
}

int v5_command_gate_response_fixed_text_has_nul(const char *text, size_t cap)
{
    return text && memchr(text, '\0', cap) != 0;
}

void v5_command_gate_response_copy_settings(
    V5CommandGateIpcResponseFrame *response,
    const V5SettingsApplyAxisCommitResult *result)
{
    if (!response || !result) {
        return;
    }
    response->settings_owner_written = result->owner_written ? 1 : 0;
    response->settings_source_readback_confirmed = result->source_readback_confirmed ? 1 : 0;
    response->settings_restart_pending = result->restart_pending ? 1 : 0;
    response->settings_scale_chain_attempted = result->scale_chain.attempted ? 1 : 0;
    response->settings_scale_recomputed = result->scale_chain.scale_recomputed ? 1 : 0;
    response->settings_raw_limits_recomputed = result->scale_chain.raw_limits_recomputed ? 1 : 0;
    response->settings_effective_scale = result->scale_chain.effective_scale;
    response->settings_raw_zero_position = result->scale_chain.raw_zero_position;
    response->settings_raw_min_limit = result->scale_chain.raw_min_limit;
    response->settings_raw_max_limit = result->scale_chain.raw_max_limit;
    v5_command_gate_response_copy_text(response->settings_readback_value, sizeof(response->settings_readback_value), result->readback_value);
    v5_command_gate_response_copy_text(response->settings_scale_chain_code, sizeof(response->settings_scale_chain_code), result->scale_chain.code);
}

int v5_command_gate_response_owner_is_allowed(const char *owner)
{
    return owner &&
        (strcmp(owner, "native_linuxcncrsh") == 0 ||
         strcmp(owner, "native_home_mode_gate") == 0 ||
         strcmp(owner, "native_safety") == 0 ||
         strcmp(owner, "native_first_point") == 0 ||
         strcmp(owner, "native_axis_zero_position") == 0 ||
         strcmp(owner, "native_work_zero") == 0 ||
         strcmp(owner, "native_rtcp_control") == 0);
}
