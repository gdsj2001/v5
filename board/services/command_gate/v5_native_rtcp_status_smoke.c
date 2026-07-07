#include "v5_native_rtcp_status.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "v5_native_rtcp_status_smoke.bin";
    V5NativeReadback readback;
    unlink(path);
    v5_native_readback_init(&readback);
    if (v5_native_rtcp_status_read(path, 1000U, &readback) || v5_native_readback_rtcp_known(&readback)) {
        return 1;
    }
    if (!strstr(readback.unavailable_reason, "missing")) {
        return 2;
    }
    {
        int active_mcodes[] = {3, 128};
        if (!v5_native_rtcp_status_write_from_mcodes(path, active_mcodes, sizeof(active_mcodes) / sizeof(active_mcodes[0]))) {
            return 3;
        }
    }
    v5_native_readback_init(&readback);
    if (!v5_native_rtcp_status_read(path, 1000U, &readback) || !v5_native_readback_rtcp_known(&readback) || !readback.rtcp_enabled) {
        unlink(path);
        return 4;
    }
    {
        int inactive_mcodes[] = {5, 129};
        if (!v5_native_rtcp_status_write_from_mcodes(path, inactive_mcodes, sizeof(inactive_mcodes) / sizeof(inactive_mcodes[0]))) {
            unlink(path);
            return 5;
        }
    }
    v5_native_readback_init(&readback);
    if (!v5_native_rtcp_status_read(path, 1000U, &readback) || !v5_native_readback_rtcp_known(&readback) || readback.rtcp_enabled) {
        unlink(path);
        return 6;
    }
    {
        int unrelated_mcodes[] = {3, 5};
        if (!v5_native_rtcp_status_write_from_mcodes(path, unrelated_mcodes, sizeof(unrelated_mcodes) / sizeof(unrelated_mcodes[0]))) {
            unlink(path);
            return 7;
        }
        v5_native_readback_init(&readback);
        if (!v5_native_rtcp_status_read(path, 1000U, &readback) ||
            !v5_native_readback_rtcp_known(&readback) ||
            readback.rtcp_enabled) {
            unlink(path);
            return 8;
        }
    }
    {
        int ambiguous_mcodes[] = {128, 129};
        if (!v5_native_rtcp_status_write_from_mcodes(path, ambiguous_mcodes, sizeof(ambiguous_mcodes) / sizeof(ambiguous_mcodes[0]))) {
            unlink(path);
            return 9;
        }
        v5_native_readback_init(&readback);
        if (v5_native_rtcp_status_read(path, 1000U, &readback) || v5_native_readback_rtcp_known(&readback)) {
            unlink(path);
            return 10;
        }
    }
    unlink(path);
    printf("v5 native rtcp status: missing=fail_closed mcodes_m128=active mcodes_m129_or_absent=inactive ambiguous=invalid\n");
    return 0;
}
