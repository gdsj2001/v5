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
    if (!v5_native_rtcp_status_write(path, 1, 1)) {
        return 3;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_rtcp_status_read(path, 1000U, &readback) || !v5_native_readback_rtcp_known(&readback) || !readback.rtcp_enabled) {
        unlink(path);
        return 4;
    }
    if (!v5_native_rtcp_status_write(path, 1, 0)) {
        unlink(path);
        return 5;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_rtcp_status_read(path, 1000U, &readback) || !v5_native_readback_rtcp_known(&readback) || readback.rtcp_enabled) {
        unlink(path);
        return 6;
    }
    if (!v5_native_rtcp_status_write(path, 0, 0)) {
        unlink(path);
        return 7;
    }
    v5_native_readback_init(&readback);
    if (v5_native_rtcp_status_read(path, 1000U, &readback) || v5_native_readback_rtcp_known(&readback)) {
        unlink(path);
        return 8;
    }
    unlink(path);
    printf("v5 native rtcp status: missing=fail_closed active=on inactive=off invalid=fail_closed\n");
    return 0;
}
