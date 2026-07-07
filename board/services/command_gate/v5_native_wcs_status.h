#ifndef V5_NATIVE_WCS_STATUS_H
#define V5_NATIVE_WCS_STATUS_H

#include "v5_native_readback.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_WCS_STATUS_DEFAULT_PATH "/dev/shm/v5_native_wcs_status.bin"
#define V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS 1000U
#define V5_NATIVE_WCS_STATUS_WCS_COUNT V5_NATIVE_READBACK_WCS_COUNT
#define V5_NATIVE_WCS_STATUS_AXIS_COUNT V5_NATIVE_READBACK_WCS_AXIS_COUNT
#define V5_NATIVE_WCS_STATUS_OFFSET_COUNT V5_NATIVE_WCS_STATUS_AXIS_COUNT

int v5_native_wcs_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback);
int v5_native_wcs_status_write(
    const char *path,
    int valid,
    int wcs_index,
    const double *offsets,
    size_t offset_count);
int v5_native_wcs_status_write_table(
    const char *path,
    int valid,
    int wcs_index,
    const double *offsets,
    size_t wcs_count,
    size_t axis_count,
    unsigned int epoch);
int v5_native_wcs_status_from_g5x(int g5x_index, int *wcs_index_out);

#ifdef __cplusplus
}
#endif

#endif
