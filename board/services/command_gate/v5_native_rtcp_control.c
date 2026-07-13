#include "v5_native_rtcp_control.h"

#include "v5_native_hal_owner_client.h"

#include <stdio.h>
#include <string.h>

static void copy_code(V5NativeRtcpControlResult *result, const char *code)
{
    if (result && code) {
        snprintf(result->code, sizeof(result->code), "%s", code);
    }
}

void v5_native_rtcp_control_result_init(V5NativeRtcpControlResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    copy_code(result, "NATIVE_RTCP_CONTROL_NOT_ATTEMPTED");
}

int v5_native_rtcp_control_set(int enabled, V5NativeRtcpControlResult *result)
{
    V5NativeHalOwnerResponse response;
    int status;
    v5_native_rtcp_control_result_init(result);
    status = v5_native_hal_owner_exchange(
        V5_NATIVE_HAL_OWNER_OP_RTCP_SET,
        enabled ? 1U : 0U,
        600U,
        &response);
    if (result) {
        result->target_active = enabled ? 1 : 0;
        result->actual_known = response.rtcp_actual_known ? 1 : 0;
        result->actual_active = response.rtcp_actual_active ? 1 : 0;
        copy_code(result, response.code);
    }
    if (status == V5_NATIVE_HAL_OWNER_CLIENT_OK) {
        return V5_NATIVE_RTCP_CONTROL_SEND_SENT;
    }
    return status == V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE
               ? V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE
               : V5_NATIVE_RTCP_CONTROL_SEND_IO_ERROR;
}
