#ifndef V5_NATIVE_HAL_OWNER_CLIENT_H
#define V5_NATIVE_HAL_OWNER_CLIENT_H

#include "v5_native_hal_owner_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE 0
#define V5_NATIVE_HAL_OWNER_CLIENT_OK 1
#define V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR -2

int v5_native_hal_owner_exchange(
    unsigned int operation,
    unsigned int target,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response);

#ifdef __cplusplus
}
#endif

#endif
