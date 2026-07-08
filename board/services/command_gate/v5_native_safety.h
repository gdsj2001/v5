#ifndef V5_NATIVE_SAFETY_H
#define V5_NATIVE_SAFETY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_SAFETY_SEND_UNAVAILABLE 0
#define V5_NATIVE_SAFETY_SEND_SENT 1
#define V5_NATIVE_SAFETY_SEND_IO_ERROR -2

#define V5_NATIVE_SAFETY_LATCH_MAGIC 0x56355346u
#define V5_NATIVE_SAFETY_LATCH_VERSION 1u
#define V5_NATIVE_SAFETY_LATCH_PATH_ENV "V5_NATIVE_SAFETY_LATCH_PATH"
#define V5_NATIVE_SAFETY_LATCH_DEFAULT_PATH "/dev/shm/v5_native_safety_latch.bin"

typedef struct V5NativeSafetyLatchFrame {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t estop_force_epoch;
    volatile uint32_t estop_force_ack_epoch;
    volatile uint32_t estop_reset_epoch;
    volatile uint32_t estop_reset_ack_epoch;
    volatile uint32_t safety_estop_known;
    volatile uint32_t safety_estop_active;
    volatile uint32_t machine_enable_known;
    volatile uint32_t machine_enabled;
} V5NativeSafetyLatchFrame;

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
