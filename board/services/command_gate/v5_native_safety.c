#include "v5_native_safety.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void copy_code(V5NativeSafetyResult *result, const char *code)
{
    if (!result || !code) {
        return;
    }
    snprintf(result->code, sizeof(result->code), "%s", code);
}

void v5_native_safety_result_init(V5NativeSafetyResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    copy_code(result, "NATIVE_SAFETY_NOT_ATTEMPTED");
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

static void result_from_latch(V5NativeSafetyResult *result, const V5NativeSafetyLatchFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->safety_estop_known = frame->safety_estop_known ? 1 : 0;
    result->safety_estop_active = frame->safety_estop_active ? 1 : 0;
    result->machine_enable_known = frame->machine_enable_known ? 1 : 0;
    result->machine_enabled = frame->machine_enabled ? 1 : 0;
}

static int map_latch(V5NativeSafetyResult *result, V5NativeSafetyLatchFrame **frame_out)
{
    const char *path;
    int fd;
    struct stat st;
    void *mapped;

    if (frame_out) {
        *frame_out = 0;
    }
    path = getenv(V5_NATIVE_SAFETY_LATCH_PATH_ENV);
    if (!path || !path[0]) {
        copy_code(result, "NATIVE_SAFETY_LATCH_UNCONFIGURED");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        copy_code(result, "NATIVE_SAFETY_LATCH_OPEN_FAILED");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(V5NativeSafetyLatchFrame)) {
        close(fd);
        copy_code(result, "NATIVE_SAFETY_LATCH_BAD_SIZE");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    mapped = mmap(0, sizeof(V5NativeSafetyLatchFrame), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        copy_code(result, "NATIVE_SAFETY_LATCH_MAP_FAILED");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    if (((V5NativeSafetyLatchFrame *)mapped)->magic != V5_NATIVE_SAFETY_LATCH_MAGIC ||
        ((V5NativeSafetyLatchFrame *)mapped)->version != V5_NATIVE_SAFETY_LATCH_VERSION) {
        munmap(mapped, sizeof(V5NativeSafetyLatchFrame));
        copy_code(result, "NATIVE_SAFETY_LATCH_BAD_MAGIC");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    *frame_out = (V5NativeSafetyLatchFrame *)mapped;
    return V5_NATIVE_SAFETY_SEND_SENT;
}

static void unmap_latch(V5NativeSafetyLatchFrame *frame)
{
    if (frame) {
        munmap((void *)frame, sizeof(V5NativeSafetyLatchFrame));
    }
}

int v5_native_safety_estop_force(V5NativeSafetyResult *result)
{
    V5NativeSafetyLatchFrame *frame = 0;
    uint32_t epoch;
    int status;

    v5_native_safety_result_init(result);
    status = map_latch(result, &frame);
    if (status != V5_NATIVE_SAFETY_SEND_SENT) {
        return status;
    }

    epoch = epoch_increment(&frame->estop_force_epoch);
    for (unsigned int i = 0U; i < 100U; ++i) {
        result_from_latch(result, frame);
        if (frame->estop_force_ack_epoch >= epoch &&
            result->safety_estop_known &&
            result->safety_estop_active) {
            copy_code(result, "NATIVE_SAFETY_ESTOP_FORCE_OK");
            unmap_latch(frame);
            return V5_NATIVE_SAFETY_SEND_SENT;
        }
        usleep(1000U);
    }

    result_from_latch(result, frame);
    copy_code(result, "NATIVE_SAFETY_ESTOP_FORCE_NOT_CONFIRMED");
    unmap_latch(frame);
    return V5_NATIVE_SAFETY_SEND_IO_ERROR;
}

int v5_native_safety_estop_reset(V5NativeSafetyResult *result)
{
    V5NativeSafetyLatchFrame *frame = 0;
    uint32_t epoch;
    int status;

    v5_native_safety_result_init(result);
    if (result) {
        result->machine_on_requested = 1;
    }
    status = map_latch(result, &frame);
    if (status != V5_NATIVE_SAFETY_SEND_SENT) {
        if (result) {
            result->machine_on_status = status;
        }
        return status;
    }

    epoch = epoch_increment(&frame->estop_reset_epoch);
    for (unsigned int i = 0U; i < 1000U; ++i) {
        result_from_latch(result, frame);
        if (frame->estop_reset_ack_epoch >= epoch &&
            result->safety_estop_known &&
            !result->safety_estop_active &&
            result->machine_enable_known &&
            result->machine_enabled) {
            copy_code(result, "NATIVE_SAFETY_ESTOP_RESET_OK");
            if (result) {
                result->machine_on_status = V5_NATIVE_SAFETY_SEND_SENT;
            }
            unmap_latch(frame);
            return V5_NATIVE_SAFETY_SEND_SENT;
        }
        usleep(1000U);
    }

    result_from_latch(result, frame);
    copy_code(result, "NATIVE_SAFETY_ESTOP_RESET_NOT_CONFIRMED");
    if (result) {
        result->machine_on_status = V5_NATIVE_SAFETY_SEND_IO_ERROR;
    }
    unmap_latch(frame);
    return V5_NATIVE_SAFETY_SEND_IO_ERROR;
}
#else
int v5_native_safety_estop_force(V5NativeSafetyResult *result)
{
    v5_native_safety_result_init(result);
    copy_code(result, "NATIVE_SAFETY_LATCH_UNAVAILABLE_ON_WIN32");
    return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
}

int v5_native_safety_estop_reset(V5NativeSafetyResult *result)
{
    v5_native_safety_result_init(result);
    if (result) {
        result->machine_on_requested = 1;
        result->machine_on_status = V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    copy_code(result, "NATIVE_SAFETY_LATCH_UNAVAILABLE_ON_WIN32");
    return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
}
#endif
