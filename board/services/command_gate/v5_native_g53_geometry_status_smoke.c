#include "v5_native_g53_geometry_status.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = "v5_native_g53_geometry_status_smoke.bin";
    V5NativeReadback readback;
    double centers[V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT][V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT] = {
        {1.0, 20.0, -50.0},
        {0.0, 2.0, -25.0},
        {50.0, 20.0, 3.0},
    };

    if (argc == 2) {
        v5_native_readback_init(&readback);
        if (!v5_native_g53_geometry_status_read(argv[1], 1000U, &readback) ||
            !v5_native_readback_g53_geometry_known(&readback) ||
            !v5_native_readback_motion_model_known(&readback)) {
            return 7;
        }
        printf(
            "v5 native g53 geometry status: path=%s model=%s epoch=%u\n",
            argv[1],
            readback.motion_model,
            readback.g53_geometry_epoch);
        return 0;
    }

    unlink(path);
    v5_native_readback_init(&readback);
    if (v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        v5_native_readback_g53_geometry_known(&readback)) {
        return 1;
    }
    if (!strstr(readback.unavailable_reason, "missing")) {
        return 2;
    }
    if (!v5_native_g53_geometry_status_write(
            path,
            1,
            &centers[0][0],
            V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT,
            V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT,
            19U,
            "XYZAC_TRT")) {
        return 3;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        !v5_native_readback_g53_geometry_known(&readback) ||
        readback.g53_geometry_epoch != 19U ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][0] != 1.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][1] != 20.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][2] != -50.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][0] != 50.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][1] != 20.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][2] != 3.0 ||
        !v5_native_readback_motion_model_known(&readback) ||
        strcmp(readback.motion_model, "XYZAC_TRT") != 0 ||
        v5_native_readback_g53_center(&readback, V5_NATIVE_READBACK_G53_CENTER_B)[2] != -25.0) {
        unlink(path);
        return 4;
    }
    if (!v5_native_g53_geometry_status_write(path, 0, 0, 0, 0, 0U, 0)) {
        unlink(path);
        return 5;
    }
    v5_native_readback_init(&readback);
    if (v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        v5_native_readback_g53_geometry_known(&readback)) {
        unlink(path);
        return 6;
    }
    unlink(path);
    printf("v5 native g53 geometry status: missing=fail_closed centers=3x3 model=readback invalid=fail_closed\n");
    return 0;
}
