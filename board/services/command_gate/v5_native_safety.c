#include "v5_native_safety.h"

#include "v5_native_hal_owner_client.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

static void copy_code(V5NativeSafetyResult *result, const char *code)
{
    if (result && code) {
        snprintf(result->code, sizeof(result->code), "%s", code);
    }
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
static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return ((uint64_t)now.tv_sec * 1000000000ULL) + (uint64_t)now.tv_nsec;
}

static uint32_t frame_crc32(const V5NativeSafetyLatchFrame *frame)
{
    const unsigned char *bytes = (const unsigned char *)frame;
    uint32_t value = 2166136261U;
    size_t i;
    for (i = 0U; i < offsetof(V5NativeSafetyLatchFrame, crc32); ++i) {
        value ^= bytes[i];
        value *= 16777619U;
    }
    return value;
}

static int frame_is_fresh(const V5NativeSafetyLatchFrame *frame)
{
    uint64_t now = monotonic_ns();
    uint64_t max_age_ns = (uint64_t)V5_NATIVE_SAFETY_LATCH_MAX_AGE_MS * 1000000ULL;
    return frame && frame->magic == V5_NATIVE_SAFETY_LATCH_MAGIC &&
           frame->version == V5_NATIVE_SAFETY_LATCH_VERSION &&
           frame->size == (uint32_t)sizeof(*frame) && frame->valid &&
           frame->crc32 == frame_crc32(frame) && now && frame->monotonic_ns &&
           now >= frame->monotonic_ns && now - frame->monotonic_ns <= max_age_ns;
}

static void result_from_frame(V5NativeSafetyResult *result, const V5NativeSafetyLatchFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->safety_estop_known = frame->safety_estop_known ? 1 : 0;
    result->safety_estop_active = frame->safety_estop_active ? 1 : 0;
    result->machine_enable_known = frame->machine_enable_known ? 1 : 0;
    result->machine_enabled = frame->machine_enabled ? 1 : 0;
}

static void result_from_response(V5NativeSafetyResult *result, const V5NativeHalOwnerResponse *response)
{
    if (!result || !response) {
        return;
    }
    result->safety_estop_known = response->safety_estop_known ? 1 : 0;
    result->safety_estop_active = response->safety_estop_active ? 1 : 0;
    result->machine_enable_known = response->machine_enable_known ? 1 : 0;
    result->machine_enabled = response->machine_enabled ? 1 : 0;
    copy_code(result, response->code);
}

static int map_status(V5NativeSafetyResult *result, const V5NativeSafetyLatchFrame **frame_out)
{
    const char *path = getenv(V5_NATIVE_SAFETY_LATCH_PATH_ENV);
    struct stat st;
    void *mapped;
    int fd;
    if (frame_out) {
        *frame_out = 0;
    }
    if (!path || !path[0]) {
        path = V5_NATIVE_SAFETY_LATCH_DEFAULT_PATH;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        copy_code(result, "NATIVE_SAFETY_STATUS_OPEN_FAILED");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(V5NativeSafetyLatchFrame)) {
        close(fd);
        copy_code(result, "NATIVE_SAFETY_STATUS_BAD_SIZE");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    mapped = mmap(0, sizeof(V5NativeSafetyLatchFrame), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        copy_code(result, "NATIVE_SAFETY_STATUS_MAP_FAILED");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    if (!frame_is_fresh((const V5NativeSafetyLatchFrame *)mapped)) {
        munmap(mapped, sizeof(V5NativeSafetyLatchFrame));
        copy_code(result, "NATIVE_SAFETY_STATUS_INVALID_OR_STALE");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    *frame_out = (const V5NativeSafetyLatchFrame *)mapped;
    return V5_NATIVE_SAFETY_SEND_SENT;
}

static void unmap_status(const V5NativeSafetyLatchFrame *frame)
{
    if (frame) {
        munmap((void *)frame, sizeof(V5NativeSafetyLatchFrame));
    }
}

static int exchange_safety(unsigned int operation, V5NativeSafetyResult *result)
{
    V5NativeHalOwnerResponse response;
    int status;
    v5_native_safety_result_init(result);
    status = v5_native_hal_owner_exchange(operation, 0U, 100U, &response);
    result_from_response(result, &response);
    if (status == V5_NATIVE_HAL_OWNER_CLIENT_OK) {
        return V5_NATIVE_SAFETY_SEND_SENT;
    }
    return status == V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE
               ? V5_NATIVE_SAFETY_SEND_UNAVAILABLE
               : V5_NATIVE_SAFETY_SEND_IO_ERROR;
}

int v5_native_safety_read_status(V5NativeSafetyResult *result)
{
    const V5NativeSafetyLatchFrame *frame = 0;
    int status;
    v5_native_safety_result_init(result);
    status = map_status(result, &frame);
    if (status != V5_NATIVE_SAFETY_SEND_SENT) {
        return status;
    }
    result_from_frame(result, frame);
    unmap_status(frame);
    if (!result || (!result->safety_estop_known && !result->machine_enable_known)) {
        copy_code(result, "NATIVE_SAFETY_STATUS_UNKNOWN");
        return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    copy_code(result, "NATIVE_SAFETY_STATUS_OK");
    return V5_NATIVE_SAFETY_SEND_SENT;
}

int v5_native_safety_estop_force(V5NativeSafetyResult *result)
{
    return exchange_safety(V5_NATIVE_HAL_OWNER_OP_ESTOP_FORCE, result);
}

int v5_native_safety_estop_reset_latch(V5NativeSafetyResult *result)
{
    return exchange_safety(V5_NATIVE_HAL_OWNER_OP_ESTOP_RESET, result);
}

int v5_native_safety_wait_reset_confirmed(
    V5NativeSafetyResult *result,
    unsigned int attempts,
    unsigned int delay_us)
{
    int machine_on_requested = result ? result->machine_on_requested : 0;
    int machine_on_status = result ? result->machine_on_status : 0;
    if (attempts == 0U) {
        attempts = 1U;
    }
    while (attempts-- > 0U) {
        V5NativeSafetyResult current;
        int status = v5_native_safety_read_status(&current);
        if (result) {
            *result = current;
            result->machine_on_requested = machine_on_requested;
            result->machine_on_status = machine_on_status;
        }
        if (status == V5_NATIVE_SAFETY_SEND_SENT && current.safety_estop_known &&
            !current.safety_estop_active && current.machine_enable_known && current.machine_enabled) {
            copy_code(result, "NATIVE_SAFETY_ESTOP_RESET_OK");
            return V5_NATIVE_SAFETY_SEND_SENT;
        }
        if (delay_us > 0U) {
            usleep(delay_us);
        }
    }
    copy_code(result, "NATIVE_SAFETY_ESTOP_RESET_NOT_CONFIRMED");
    return V5_NATIVE_SAFETY_SEND_IO_ERROR;
}

int v5_native_safety_estop_reset(V5NativeSafetyResult *result)
{
    int status = v5_native_safety_estop_reset_latch(result);
    if (result) {
        result->machine_on_requested = 1;
        result->machine_on_status = V5_NATIVE_SAFETY_SEND_IO_ERROR;
    }
    if (status != V5_NATIVE_SAFETY_SEND_SENT) {
        return status;
    }
    return v5_native_safety_wait_reset_confirmed(result, 1000U, 1000U);
}
#else
int v5_native_safety_read_status(V5NativeSafetyResult *result)
{
    v5_native_safety_result_init(result);
    copy_code(result, "NATIVE_SAFETY_UNAVAILABLE_ON_WIN32");
    return V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
}

int v5_native_safety_estop_force(V5NativeSafetyResult *result)
{
    return v5_native_safety_read_status(result);
}

int v5_native_safety_estop_reset_latch(V5NativeSafetyResult *result)
{
    return v5_native_safety_read_status(result);
}

int v5_native_safety_wait_reset_confirmed(V5NativeSafetyResult *result, unsigned int attempts, unsigned int delay_us)
{
    (void)attempts;
    (void)delay_us;
    return v5_native_safety_read_status(result);
}

int v5_native_safety_estop_reset(V5NativeSafetyResult *result)
{
    int status = v5_native_safety_read_status(result);
    if (result) {
        result->machine_on_requested = 1;
        result->machine_on_status = V5_NATIVE_SAFETY_SEND_UNAVAILABLE;
    }
    return status;
}
#endif
