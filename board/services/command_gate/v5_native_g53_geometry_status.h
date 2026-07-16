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
#define V5_NATIVE_G53_GEOMETRY_STATUS_MOTION_MODEL_CAP V5_NATIVE_READBACK_MOTION_MODEL_CAP

/* Version-2 wire field bits.  The mask occupies the former reserved0 slot. */
#define V5_NATIVE_G53_GEOMETRY_FIELD_A_Y (1U << 0)
#define V5_NATIVE_G53_GEOMETRY_FIELD_A_Z (1U << 1)
#define V5_NATIVE_G53_GEOMETRY_FIELD_B_X (1U << 2)
#define V5_NATIVE_G53_GEOMETRY_FIELD_B_Z (1U << 3)
#define V5_NATIVE_G53_GEOMETRY_FIELD_C_X (1U << 4)
#define V5_NATIVE_G53_GEOMETRY_FIELD_C_Y (1U << 5)
#define V5_NATIVE_G53_GEOMETRY_FIELD_MASK_ALL \
    (V5_NATIVE_G53_GEOMETRY_FIELD_A_Y | V5_NATIVE_G53_GEOMETRY_FIELD_A_Z | \
     V5_NATIVE_G53_GEOMETRY_FIELD_B_X | V5_NATIVE_G53_GEOMETRY_FIELD_B_Z | \
     V5_NATIVE_G53_GEOMETRY_FIELD_C_X | V5_NATIVE_G53_GEOMETRY_FIELD_C_Y)

int v5_native_g53_geometry_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback);
int v5_native_g53_geometry_status_write(
    const char *path,
    int valid,
    const double *centers,
    size_t center_count,
    size_t axis_count,
    unsigned int epoch,
    const char *motion_model);

#ifdef __cplusplus
}
#endif

#endif
