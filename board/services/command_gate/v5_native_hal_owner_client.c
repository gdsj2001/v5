#include "v5_native_hal_owner_client.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#endif

static void response_init(V5NativeHalOwnerResponse *response, unsigned int operation)
{
    if (!response) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->operation = operation;
    response->status = V5_NATIVE_HAL_OWNER_STATUS_UNAVAILABLE;
    snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_NOT_ATTEMPTED");
}

int v5_native_hal_owner_request_target(
    unsigned int operation,
    unsigned int target,
    unsigned int *wire_target)
{
    if (!wire_target) {
        return 0;
    }
    if (operation == V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS) {
        if (target > 2U) return 0;
        *wire_target = target;
        return 1;
    }
    if (operation == V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG) {
        if (target >= V5_NATIVE_HOME_JOINT_COUNT) return 0;
        *wire_target = target;
        return 1;
    }
    if (operation == V5_NATIVE_HAL_OWNER_OP_HOME_STATUS) {
        if (target != 0U) return 0;
        *wire_target = 0U;
        return 1;
    }
    if (target > 1U) return 0;
    *wire_target = target;
    return 1;
}

#ifndef _WIN32
static uint64_t next_request_id(void)
{
    static volatile uint32_t sequence = 0U;
    struct timespec now;
    uint64_t value = 1U;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        value = ((uint64_t)now.tv_sec * 1000000000ULL) + (uint64_t)now.tv_nsec;
    }
    value ^= (uint64_t)__sync_add_and_fetch(&sequence, 1U);
    return value ? value : 1U;
}

static int write_full(int fd, const void *buffer, size_t size)
{
    const unsigned char *cursor = (const unsigned char *)buffer;
    while (size > 0U) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return 0;
        }
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return 1;
}

static int read_full(int fd, void *buffer, size_t size)
{
    unsigned char *cursor = (unsigned char *)buffer;
    while (size > 0U) {
        ssize_t received = read(fd, cursor, size);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            return 0;
        }
        cursor += (size_t)received;
        size -= (size_t)received;
    }
    return 1;
}

static int exchange_request(
    const V5NativeHalOwnerRequest *request,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response)
{
    struct sockaddr_un address;
    struct timeval timeout;
    int fd;

    response_init(response, request ? request->operation : 0U);
    if (!request || !response) {
        return V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
    }
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_SOCKET_FAILED");
        return V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE;
    }
    timeout.tv_sec = (time_t)((timeout_ms ? timeout_ms : 100U) / 1000U);
    timeout.tv_usec = (suseconds_t)(((timeout_ms ? timeout_ms : 100U) % 1000U) * 1000U);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", V5_NATIVE_HAL_OWNER_SOCKET_PATH);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_CONNECT_FAILED");
        return V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE;
    }
    if (!write_full(fd, request, sizeof(*request)) || !read_full(fd, response, sizeof(*response))) {
        close(fd);
        response_init(response, request->operation);
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_IO_FAILED");
        return V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
    }
    close(fd);
    if (response->magic != V5_NATIVE_HAL_OWNER_MAGIC ||
        response->version != V5_NATIVE_HAL_OWNER_VERSION ||
        response->size != (uint32_t)sizeof(*response) ||
        response->operation != request->operation ||
        response->request_id != request->request_id) {
        response_init(response, request->operation);
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_BAD_RESPONSE");
        return V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
    }
    return response->status == V5_NATIVE_HAL_OWNER_STATUS_OK
               ? V5_NATIVE_HAL_OWNER_CLIENT_OK
               : V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
}

int v5_native_hal_owner_exchange(
    unsigned int operation,
    unsigned int target,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response)
{
    V5NativeHalOwnerRequest request;
    unsigned int wire_target;
    response_init(response, operation);
    if (!response || !v5_native_hal_owner_request_target(operation, target, &wire_target)) {
        if (response) snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_TARGET_INVALID");
        return V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
    }
    memset(&request, 0, sizeof(request));
    request.magic = V5_NATIVE_HAL_OWNER_MAGIC;
    request.version = V5_NATIVE_HAL_OWNER_VERSION;
    request.size = (uint32_t)sizeof(request);
    request.operation = operation;
    request.request_id = next_request_id();
    request.target = wire_target;
    return exchange_request(&request, timeout_ms, response);
}

int v5_native_hal_owner_stage_home_joint(
    const V5NativeHomeConfigRecord *record,
    int commit,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response)
{
    V5NativeHalOwnerRequest request;
    unsigned int wire_target;
    response_init(response, V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG);
    if (!record || !response || record->joint != record->status_slot ||
        !v5_native_hal_owner_request_target(
            V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG, record->joint, &wire_target)) {
        if (response) snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HOME_CONFIG_TARGET_INVALID");
        return V5_NATIVE_HAL_OWNER_CLIENT_IO_ERROR;
    }
    memset(&request, 0, sizeof(request));
    request.magic = V5_NATIVE_HAL_OWNER_MAGIC;
    request.version = V5_NATIVE_HAL_OWNER_VERSION;
    request.size = (uint32_t)sizeof(request);
    request.operation = V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG;
    request.request_id = next_request_id();
    request.target = wire_target;
    request.flags = (record->active ? V5_NATIVE_HOME_CONFIG_ACTIVE : 0U) |
                    (commit ? V5_NATIVE_HOME_CONFIG_COMMIT : 0U);
    request.home_status_slot = record->status_slot;
    request.axis_code = record->axis_code;
    request.home_slave_position = record->slave_position;
    request.home_mapping_generation = record->mapping_generation;
    request.home_expected_active_mask = record->expected_active_mask;
    request.home_config_commit_seq = record->commit_seq;
    request.home_zero_counts = record->zero_counts;
    request.home_counts_per_unit = record->counts_per_unit;
    return exchange_request(&request, timeout_ms, response);
}
#else
int v5_native_hal_owner_exchange(
    unsigned int operation,
    unsigned int target,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response)
{
    (void)target;
    (void)timeout_ms;
    response_init(response, operation);
    if (response) {
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_UNAVAILABLE_ON_WIN32");
    }
    return V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE;
}

int v5_native_hal_owner_stage_home_joint(
    const V5NativeHomeConfigRecord *record,
    int commit,
    unsigned int timeout_ms,
    V5NativeHalOwnerResponse *response)
{
    (void)record;
    (void)commit;
    (void)timeout_ms;
    response_init(response, V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG);
    if (response) {
        snprintf(response->code, sizeof(response->code), "%s", "NATIVE_HAL_OWNER_UNAVAILABLE_ON_WIN32");
    }
    return V5_NATIVE_HAL_OWNER_CLIENT_UNAVAILABLE;
}
#endif
