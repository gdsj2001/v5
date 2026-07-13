#ifndef V5_NATIVE_RTCP_CONTROL_H
#define V5_NATIVE_RTCP_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE 0
#define V5_NATIVE_RTCP_CONTROL_SEND_SENT 1
#define V5_NATIVE_RTCP_CONTROL_SEND_IO_ERROR -2

typedef struct V5NativeRtcpControlResult {
    int target_active;
    int actual_known;
    int actual_active;
    char code[64];
} V5NativeRtcpControlResult;

void v5_native_rtcp_control_result_init(V5NativeRtcpControlResult *result);
int v5_native_rtcp_control_set(int enabled, V5NativeRtcpControlResult *result);

#ifdef __cplusplus
}
#endif

#endif
