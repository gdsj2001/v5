#include "v5_native_rtcp_control.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void copy_code(V5NativeRtcpControlResult *result, const char *code)
{
    if (!result || !code) {
        return;
    }
    snprintf(result->code, sizeof(result->code), "%s", code);
}

void v5_native_rtcp_control_result_init(V5NativeRtcpControlResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    copy_code(result, "NATIVE_RTCP_CONTROL_NOT_ATTEMPTED");
}

#ifndef _WIN32
static uint32_t epoch_increment(volatile uint32_t *value)
{
#if defined(__GNUC__)
    return __sync_add_and_fetch((uint32_t *)value, 1U);
#else
    *value = *value + 1U;
    return *value;
#endif
}

static void result_from_latch(
    V5NativeRtcpControlResult *result,
    const V5NativeRtcpControlLatchFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->target_active = frame->target_active ? 1 : 0;
    result->actual_known = frame->actual_known ? 1 : 0;
    result->actual_active = frame->actual_active ? 1 : 0;
}

static int map_latch(V5NativeRtcpControlResult *result, V5NativeRtcpControlLatchFrame **frame_out)
{
    const char *path;
    int fd;
    struct stat st;
    void *mapped;

    if (frame_out) {
        *frame_out = 0;
    }
    path = getenv(V5_NATIVE_RTCP_CONTROL_LATCH_PATH_ENV);
    if (!path || !path[0]) {
        path = V5_NATIVE_RTCP_CONTROL_LATCH_DEFAULT_PATH;
    }

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        copy_code(result, "NATIVE_RTCP_CONTROL_LATCH_OPEN_FAILED");
        return V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE;
    }
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(V5NativeRtcpControlLatchFrame)) {
        close(fd);
        copy_code(result, "NATIVE_RTCP_CONTROL_LATCH_BAD_SIZE");
        return V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE;
    }
    mapped = mmap(0, sizeof(V5NativeRtcpControlLatchFrame), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        copy_code(result, "NATIVE_RTCP_CONTROL_LATCH_MAP_FAILED");
        return V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE;
    }
    if (((V5NativeRtcpControlLatchFrame *)mapped)->magic != V5_NATIVE_RTCP_CONTROL_LATCH_MAGIC ||
        ((V5NativeRtcpControlLatchFrame *)mapped)->version != V5_NATIVE_RTCP_CONTROL_LATCH_VERSION) {
        munmap(mapped, sizeof(V5NativeRtcpControlLatchFrame));
        copy_code(result, "NATIVE_RTCP_CONTROL_LATCH_BAD_MAGIC");
        return V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE;
    }
    *frame_out = (V5NativeRtcpControlLatchFrame *)mapped;
    return V5_NATIVE_RTCP_CONTROL_SEND_SENT;
}

static void unmap_latch(V5NativeRtcpControlLatchFrame *frame)
{
    if (frame) {
        munmap((void *)frame, sizeof(V5NativeRtcpControlLatchFrame));
    }
}

int v5_native_rtcp_control_set(int enabled, V5NativeRtcpControlResult *result)
{
    V5NativeRtcpControlLatchFrame *frame = 0;
    uint32_t epoch;
    uint32_t target = enabled ? 1U : 0U;
    int status;

    v5_native_rtcp_control_result_init(result);
    status = map_latch(result, &frame);
    if (status != V5_NATIVE_RTCP_CONTROL_SEND_SENT) {
        return status;
    }

    frame->target_active = target;
    frame->status_code = V5_NATIVE_RTCP_CONTROL_STATUS_UNKNOWN;
    epoch = epoch_increment(&frame->request_epoch);

    for (unsigned int i = 0U; i < 50U; ++i) {
        result_from_latch(result, frame);
        if (frame->ack_epoch >= epoch) {
            if (frame->status_code == V5_NATIVE_RTCP_CONTROL_STATUS_OK &&
                frame->actual_known &&
                frame->actual_active == target) {
                copy_code(result, target ? "NATIVE_RTCP_CONTROL_ON_OK" : "NATIVE_RTCP_CONTROL_OFF_OK");
                unmap_latch(frame);
                return V5_NATIVE_RTCP_CONTROL_SEND_SENT;
            }
            copy_code(result, "NATIVE_RTCP_CONTROL_NOT_CONFIRMED");
            unmap_latch(frame);
            return V5_NATIVE_RTCP_CONTROL_SEND_IO_ERROR;
        }
        usleep(50000U);
    }

    result_from_latch(result, frame);
    copy_code(result, "NATIVE_RTCP_CONTROL_ACK_TIMEOUT");
    unmap_latch(frame);
    return V5_NATIVE_RTCP_CONTROL_SEND_IO_ERROR;
}
#else
int v5_native_rtcp_control_set(int enabled, V5NativeRtcpControlResult *result)
{
    (void)enabled;
    v5_native_rtcp_control_result_init(result);
    copy_code(result, "NATIVE_RTCP_CONTROL_UNAVAILABLE_ON_WIN32");
    return V5_NATIVE_RTCP_CONTROL_SEND_UNAVAILABLE;
}
#endif
