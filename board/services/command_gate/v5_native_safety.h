#ifndef V5_NATIVE_SAFETY_H
#define V5_NATIVE_SAFETY_H

#include "v5_native_hal_owner_protocol.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_SAFETY_SEND_UNAVAILABLE 0
#define V5_NATIVE_SAFETY_SEND_SENT 1
#define V5_NATIVE_SAFETY_SEND_IO_ERROR -2

#define V5_NATIVE_SAFETY_LATCH_MAGIC V5_NATIVE_SAFETY_STATUS_MAGIC
#define V5_NATIVE_SAFETY_LATCH_VERSION V5_NATIVE_SAFETY_STATUS_VERSION
#define V5_NATIVE_SAFETY_LATCH_PATH_ENV "V5_NATIVE_SAFETY_LATCH_PATH"
#define V5_NATIVE_SAFETY_LATCH_DEFAULT_PATH V5_NATIVE_SAFETY_STATUS_PATH
#define V5_NATIVE_SAFETY_LATCH_MAX_AGE_MS 1000U

typedef V5NativeSafetyStatusBlock V5NativeSafetyLatchFrame;

typedef struct V5NativeSafetyResult {
    int safety_estop_known;
    int safety_estop_active;
    int machine_enable_known;
    int machine_enabled;
    int machine_on_requested;
    int machine_on_status;
    char code[64];
} V5NativeSafetyResult;

void v5_native_safety_result_init(V5NativeSafetyResult *result);
int v5_native_safety_read_status(V5NativeSafetyResult *result);
int v5_native_safety_estop_force(V5NativeSafetyResult *result);
int v5_native_safety_estop_reset_latch(V5NativeSafetyResult *result);
int v5_native_safety_wait_reset_confirmed(
    V5NativeSafetyResult *result,
    unsigned int attempts,
    unsigned int delay_us);
int v5_native_safety_estop_reset(V5NativeSafetyResult *result);

#ifdef __cplusplus
}
#endif

#endif
