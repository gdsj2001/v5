#ifndef V5_NATIVE_HAL_OWNER_CLIENT_H
#define V5_NATIVE_HAL_OWNER_CLIENT_H

#include "v5_native_hal_owner_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE 0
#define V5_NATIVE_HAL_OWNER_CLIENT_OK 1
#define V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR -2

typedef struct V5NativeHomeConfigRecord {
    unsigned int joint;
    unsigned int status_slot;
    unsigned int active;
    unsigned int axis_code;
    unsigned int slave_position;
    unsigned int mapping_generation;
    unsigned int expected_active_mask;
    unsigned int commit_seq;
    int home_ready;
    double zero_counts;
    double counts_per_unit;
} V5NativeHomeConfigRecord;

int v5_native_hal_owner_request_target(
    unsigned int operation,
    unsigned int target,
    unsigned int *wire_target);

int v5_native_hal_owner_exchange(
    unsigned int operation,
    unsigned int target,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response);

int v5_native_hal_owner_stage_home_joint(
    const V5NativeHomeConfigRecord *record,
    int commit,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response);

#ifdef __cplusplus
}
#endif

#endif
