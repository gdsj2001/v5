#include "v5_native_readback.h"

#include <stdio.h>
#include <string.h>

void v5_native_readback_init(V5NativeReadback *readback)
{
    if (!readback) {
        return;
    }
    memset(readback, 0, sizeof(*readback));
}

void v5_native_readback_set_unavailable(V5NativeReadback *readback, const char *reason)
{
    if (!readback) {
        return;
    }
    readback->rtcp_actual_available = 0;
    readback->wcs_actual_available = 0;
    snprintf(
        readback->unavailable_reason,
        sizeof(readback->unavailable_reason),
        "%s",
        reason ? reason : "native_readback_unavailable");
}

void v5_native_readback_set_rtcp_actual(V5NativeReadback *readback, int enabled)
{
    if (!readback) {
        return;
    }
    readback->rtcp_actual_available = 1;
    readback->rtcp_enabled = enabled ? 1 : 0;
}

void v5_native_readback_set_wcs_actual(V5NativeReadback *readback, int wcs_index)
{
    if (!readback) {
        return;
    }
    if (wcs_index < 0 || wcs_index > 8) {
        readback->wcs_actual_available = 0;
        return;
    }
    readback->wcs_actual_available = 1;
    readback->wcs_index = wcs_index;
}

int v5_native_readback_rtcp_known(const V5NativeReadback *readback)
{
    return readback && readback->rtcp_actual_available;
}

int v5_native_readback_wcs_known(const V5NativeReadback *readback)
{
    return readback && readback->wcs_actual_available;
}
