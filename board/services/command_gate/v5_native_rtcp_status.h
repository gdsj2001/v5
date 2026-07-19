#ifndef V5_NATIVE_RTCP_STATUS_H
#define V5_NATIVE_RTCP_STATUS_H

#include "v5_native_readback.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_RTCP_STATUS_DEFAULT_PATH "/dev/shm/v5_native_rtcp_status.bin"
#define V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS 1000U

int v5_native_rtcp_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback);
size_t v5_native_rtcp_status_block_size(void);
int v5_native_rtcp_status_read_from_memory(
    const void *memory,
    size_t size,
    unsigned int max_age_ms,
    V5NativeReadback *readback);
int v5_native_rtcp_status_write(const char *path, int valid, int active);

#ifdef __cplusplus
}
#endif

#endif
