#define _GNU_SOURCE

#include "v5_backend_readiness_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum WaitTarget {
    WAIT_TARGET_NONE = 0,
    WAIT_TARGET_DATA = 1,
    WAIT_TARGET_MOTION = 2
};

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0U;
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static int write_full(int fd, const void *buffer, size_t size)
{
    const unsigned char *cursor = (const unsigned char *)buffer;
    while (size) {
        ssize_t count = write(fd, cursor, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        cursor += (size_t)count;
        size -= (size_t)count;
    }
    return 1;
}

static int read_full(int fd, void *buffer, size_t size)
{
    unsigned char *cursor = (unsigned char *)buffer;
    while (size) {
        ssize_t count = read(fd, cursor, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        cursor += (size_t)count;
        size -= (size_t)count;
    }
    return 1;
}

static int owner_start_ticks(uint32_t pid, uint64_t *result)
{
    char path[64];
    char buffer[4096];
    char *right;
    char *save = 0;
    char *token;
    unsigned int field = 3U;
    int fd;
    ssize_t count;
    if (!pid || !result) return 0;
    snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    count = read(fd, buffer, sizeof(buffer) - 1U);
    close(fd);
    if (count <= 0) return 0;
    buffer[(size_t)count] = '\0';
    right = strrchr(buffer, ')');
    if (!right || right[1] != ' ') return 0;
    token = strtok_r(right + 2, " ", &save);
    while (token) {
        if (field == 22U) {
            char *end = 0;
            unsigned long long value = strtoull(token, &end, 10);
            if (!end || *end || !value) return 0;
            *result = (uint64_t)value;
            return 1;
        }
        token = strtok_r(0, " ", &save);
        ++field;
    }
    return 0;
}

static int query_owner(
    uint32_t operation,
    uint64_t request_id,
    V5BackendReadinessResponse *response)
{
    struct sockaddr_un address;
    struct timeval timeout = {0, 100000};
    V5BackendReadinessRequest request;
    uint64_t actual_start_ticks = 0U;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             V5_BACKEND_READINESS_SOCKET_PATH);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    request.magic = V5_BACKEND_READINESS_MAGIC;
    request.version = V5_BACKEND_READINESS_VERSION;
    request.size = (uint32_t)sizeof(request);
    request.operation = operation;
    request.request_id = request_id;
    if (!write_full(fd, &request, sizeof(request)) ||
        !read_full(fd, response, sizeof(*response))) {
        close(fd);
        return 0;
    }
    close(fd);
    if (response->magic != V5_BACKEND_READINESS_MAGIC ||
        response->version != V5_BACKEND_READINESS_VERSION ||
        response->size != (uint32_t)sizeof(*response) ||
        response->operation != operation || response->request_id != request_id ||
        !response->generation ||
        !owner_start_ticks(response->owner_pid, &actual_start_ticks) ||
        actual_start_ticks != response->owner_start_ticks) {
        return 0;
    }
    return 1;
}

static enum WaitTarget parse_target(const char *text)
{
    if (text && strcmp(text, "data") == 0) return WAIT_TARGET_DATA;
    if (text && strcmp(text, "motion") == 0) return WAIT_TARGET_MOTION;
    return WAIT_TARGET_NONE;
}

static int target_ready(
    enum WaitTarget target,
    const V5BackendReadinessResponse *response)
{
    if (target == WAIT_TARGET_DATA) return response->backend_data_ready != 0U;
    if (target == WAIT_TARGET_MOTION) return response->motion_backend_ready != 0U;
    return 1;
}

static void print_response(const V5BackendReadinessResponse *response)
{
    printf(
        "code=%s generation=%u owner_pid=%u owner_start_ticks=%llu "
        "armed=%u data_ready=%u motion_ready=%u revoked=%u "
        "slaves=%u/%u wkc=%u/%u wkc_complete=%u all_op=%u "
        "dc_phased=%u dc_valid=%u dc_age=%u dc_seq=%u dc_errors=%u "
        "cpu_contract_ready=%llu first_full_wkc=%llu "
        "dc_fresh_pair_ready=%llu backend_data_ready=%llu "
        "backend_ready_published=%llu\n",
        response->code,
        response->generation,
        response->owner_pid,
        (unsigned long long)response->owner_start_ticks,
        response->armed,
        response->backend_data_ready,
        response->motion_backend_ready,
        response->revoked,
        response->actual_slaves,
        response->expected_slaves,
        response->actual_wkc,
        response->expected_wkc,
        response->wkc_complete,
        response->all_op,
        response->dc_phased,
        response->dc_time_valid,
        response->dc_time_age_cycles,
        response->dc_time_ok_seq,
        response->dc_time_error_count,
        (unsigned long long)response->cpu_contract_ready_ns,
        (unsigned long long)response->first_full_wkc_ns,
        (unsigned long long)response->dc_fresh_pair_ready_ns,
        (unsigned long long)response->backend_data_ready_ns,
        (unsigned long long)response->backend_ready_published_ns);
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--arm] [--status] [--wait data|motion] "
            "[--require data|motion] [--timeout-ms N] "
            "[--expect-generation N --expect-owner-pid N "
            "--expect-owner-start-ticks N]\n",
            program);
}

static int parse_u64(const char *text, uint64_t *result)
{
    char *end = 0;
    unsigned long long value;
    if (!text || !*text || !result) return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || !end || *end || !value) return 0;
    *result = (uint64_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    enum WaitTarget wait_target = WAIT_TARGET_NONE;
    enum WaitTarget require_target = WAIT_TARGET_NONE;
    uint32_t first_operation = V5_BACKEND_READINESS_OP_STATUS;
    uint32_t expected_generation = 0U;
    uint32_t expected_owner_pid = 0U;
    uint64_t expected_owner_start_ticks = 0U;
    unsigned long timeout_ms = 0UL;
    uint64_t request_id = ((uint64_t)getpid() << 32U) ^ monotonic_ns();
    uint64_t deadline;
    V5BackendReadinessResponse response;
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--arm") == 0) {
            first_operation = V5_BACKEND_READINESS_OP_ARM;
        } else if (strcmp(argv[index], "--status") == 0) {
            continue;
        } else if ((strcmp(argv[index], "--wait") == 0 ||
                    strcmp(argv[index], "--require") == 0) && index + 1 < argc) {
            enum WaitTarget target = parse_target(argv[++index]);
            if (!target) {
                usage(argv[0]);
                return 2;
            }
            if (strcmp(argv[index - 1], "--wait") == 0) wait_target = target;
            else require_target = target;
        } else if (strcmp(argv[index], "--timeout-ms") == 0 && index + 1 < argc) {
            char *end = 0;
            timeout_ms = strtoul(argv[++index], &end, 10);
            if (!end || *end || !timeout_ms || timeout_ms > 300000UL) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[index], "--expect-generation") == 0 &&
                   index + 1 < argc) {
            uint64_t value = 0U;
            if (!parse_u64(argv[++index], &value) || value > UINT32_MAX) {
                usage(argv[0]);
                return 2;
            }
            expected_generation = (uint32_t)value;
        } else if (strcmp(argv[index], "--expect-owner-pid") == 0 &&
                   index + 1 < argc) {
            uint64_t value = 0U;
            if (!parse_u64(argv[++index], &value) || value > UINT32_MAX) {
                usage(argv[0]);
                return 2;
            }
            expected_owner_pid = (uint32_t)value;
        } else if (strcmp(argv[index], "--expect-owner-start-ticks") == 0 &&
                   index + 1 < argc) {
            if (!parse_u64(argv[++index], &expected_owner_start_ticks)) {
                usage(argv[0]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (wait_target != WAIT_TARGET_NONE && require_target != WAIT_TARGET_NONE) {
        usage(argv[0]);
        return 2;
    }
    {
        const unsigned int identity_expectation_count =
            (unsigned int)!!expected_generation +
            (unsigned int)!!expected_owner_pid +
            (unsigned int)!!expected_owner_start_ticks;
        if (identity_expectation_count != 0U &&
                identity_expectation_count != 3U) {
            usage(argv[0]);
            return 2;
        }
    }
    if (wait_target != WAIT_TARGET_NONE && !timeout_ms) timeout_ms = 30000UL;
    deadline = monotonic_ns() + (uint64_t)timeout_ms * 1000000ULL;

    for (;;) {
        memset(&response, 0, sizeof(response));
        if (query_owner(first_operation, ++request_id, &response)) {
            first_operation = V5_BACKEND_READINESS_OP_STATUS;
            if (expected_generation &&
                (response.generation != expected_generation ||
                 response.owner_pid != expected_owner_pid ||
                 response.owner_start_ticks != expected_owner_start_ticks)) {
                print_response(&response);
                return 5;
            }
            if (response.revoked ||
                response.status == V5_BACKEND_READINESS_STATUS_REVOKED) {
                print_response(&response);
                return 3;
            }
            if (require_target != WAIT_TARGET_NONE) {
                print_response(&response);
                return target_ready(require_target, &response) ? 0 : 4;
            }
            if (wait_target == WAIT_TARGET_NONE || target_ready(wait_target, &response)) {
                print_response(&response);
                return response.status == V5_BACKEND_READINESS_STATUS_OK ? 0 : 4;
            }
        }
        if (wait_target == WAIT_TARGET_NONE || monotonic_ns() >= deadline) {
            if (response.magic == V5_BACKEND_READINESS_MAGIC) print_response(&response);
            else fprintf(stderr, "BACKEND_READINESS_OWNER_UNAVAILABLE\n");
            return 4;
        }
        {
            const struct timespec pause = {0, 10000000L};
            nanosleep(&pause, 0);
        }
    }
}
