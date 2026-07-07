#ifndef V5_NATIVE_G53_GEOMETRY_STATUS_H
#define V5_NATIVE_G53_GEOMETRY_STATUS_H

#include "v5_native_readback.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_PATH "/dev/shm/v5_native_g53_geometry_status.bin"
#define V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS 1000U
#define V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT V5_NATIVE_READBACK_G53_CENTER_COUNT
#define V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT V5_NATIVE_READBACK_G53_AXIS_COUNT

int v5_native_g53_geometry_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback);
int v5_native_g53_geometry_status_write(
    const char *path,
    int valid,
    const double *centers,
    size_t center_count,
    size_t axis_count,
    unsigned int epoch);

#ifdef __cplusplus
}
#endif

#endif
