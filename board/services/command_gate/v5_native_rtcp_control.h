#ifndef V5_NATIVE_RTCP_CONTROL_H
#define V5_NATIVE_RTCP_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE 0
#define V5_NATIVE_RTCP_CONTROL_SEND_SENT 1
#define V5_NATIVE_RTCP_CONTROL_SEND_IO_ERROR -2

#define V5_NATIVE_RTCP_CONTROL_LATCH_MAGIC 0x56355254u
#define V5_NATIVE_RTCP_CONTROL_LATCH_VERSION 1u
#define V5_NATIVE_RTCP_CONTROL_LATCH_PATH_ENV "V5_NATIVE_RTCP_CONTROL_LATCH_PATH"
#define V5_NATIVE_RTCP_CONTROL_LATCH_DEFAULT_PATH "/dev/shm/v5_native_rtcp_control_latch.bin"

#define V5_NATIVE_RTCP_CONTROL_STATUS_UNKNOWN 0u
#define V5_NATIVE_RTCP_CONTROL_STATUS_OK 1u
#define V5_NATIVE_RTCP_CONTROL_STATUS_FAILED 2u

typedef struct V5NativeRtcpControlLatchFrame {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t request_epoch;
    volatile uint32_t ack_epoch;
    volatile uint32_t target_active;
    volatile uint32_t actual_known;
    volatile uint32_t actual_active;
    volatile uint32_t status_code;
} V5NativeRtcpControlLatchFrame;

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
