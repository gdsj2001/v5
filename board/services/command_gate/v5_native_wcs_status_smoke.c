#include "v5_native_wcs_status.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "v5_native_wcs_status_smoke.bin";
    V5NativeReadback readback;
    int wcs_index = -1;
    double table[V5_NATIVE_WCS_STATUS_WCS_COUNT][V5_NATIVE_WCS_STATUS_AXIS_COUNT];

    memset(table, 0, sizeof(table));
    table[2][0] = 30.0;
    table[2][1] = 20.0;
    table[2][2] = -50.0;
    table[2][3] = 4.0;
    table[2][4] = 6.0;

    unlink(path);
    v5_native_readback_init(&readback);
    if (v5_native_wcs_status_read(path, 1000U, &readback) || v5_native_readback_wcs_known(&readback)) {
        return 1;
    }
    if (!strstr(readback.unavailable_reason, "missing")) {
        return 2;
    }
    if (!v5_native_wcs_status_from_g5x(1, &wcs_index) || wcs_index != 0) {
        return 3;
    }
    if (!v5_native_wcs_status_from_g5x(9, &wcs_index) || wcs_index != 8) {
        return 4;
    }
    if (v5_native_wcs_status_from_g5x(10, &wcs_index)) {
        return 5;
    }
    if (!v5_native_wcs_status_write_table(
            path,
            1,
            2,
            &table[0][0],
            V5_NATIVE_WCS_STATUS_WCS_COUNT,
            V5_NATIVE_WCS_STATUS_AXIS_COUNT,
            17U)) {
        return 6;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_wcs_status_read(path, 1000U, &readback) ||
        !v5_native_readback_wcs_known(&readback) ||
        !v5_native_readback_wcs_offset_known(&readback) ||
        !v5_native_readback_wcs_table_known(&readback) ||
        readback.wcs_index != 2 ||
        readback.wcs_offsets_epoch != 17U ||
        readback.wcs_offsets[2][0] != 30.0 ||
        readback.wcs_offsets[2][1] != 20.0 ||
        readback.wcs_offsets[2][2] != -50.0 ||
        readback.wcs_offsets[2][3] != 4.0 ||
        readback.wcs_offsets[2][4] != 6.0) {
        unlink(path);
        return 7;
    }
    if (!v5_native_wcs_status_write(path, 0, -1, 0, 0)) {
        unlink(path);
        return 8;
    }
    v5_native_readback_init(&readback);
    if (v5_native_wcs_status_read(path, 1000U, &readback) || v5_native_readback_wcs_known(&readback)) {
        unlink(path);
        return 9;
    }
    unlink(path);
    printf("v5 native wcs status: missing=fail_closed g5x=1..9 maps=0..8 full_table=9x5 invalid=fail_closed\n");
    return 0;
}
