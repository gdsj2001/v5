#include "v5_state_publisher_service.h"
#include "v5_process_residency.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define V5_STATE_LOCK_PATH "/run/8ax/v5_state_publisher.lock"
#define V5_STATE_PIDFILE_PATH "/run/8ax/v5_state_publisher.pid"

static int state_lifecycle_fd = -1;

static int process_start_ticks(char *out, size_t out_size)
{
    FILE *stream = fopen("/proc/self/stat", "r");
    char buffer[1024];
    char *cursor;
    unsigned int field = 3u;
    if (!stream || !fgets(buffer, sizeof(buffer), stream)) {
        if (stream) fclose(stream);
        return 0;
    }
    fclose(stream);
    cursor = strrchr(buffer, ')');
    if (!cursor || cursor[1] != ' ') return 0;
    cursor += 2;
    while (*cursor) {
        char *end = strchr(cursor, ' ');
        if (field == 22u) {
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            if (!length || length + 1u > out_size) return 0;
            memcpy(out, cursor, length);
            out[length] = '\0';
            return 1;
        }
        if (!end) break;
        cursor = end + 1;
        ++field;
    }
    return 0;
}

static int write_owner_record(int fd, const char *record)
{
    char temporary[160];
    int out;
    if (ftruncate(fd, 0) != 0 || lseek(fd, 0, SEEK_SET) < 0 ||
        write(fd, record, strlen(record)) != (ssize_t)strlen(record) ||
        fsync(fd) != 0) return 0;
    snprintf(temporary, sizeof(temporary), "%s.%ld.tmp", V5_STATE_PIDFILE_PATH, (long)getpid());
    unlink(temporary);
    out = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) return 0;
    if (write(out, record, strlen(record)) != (ssize_t)strlen(record) ||
        fsync(out) != 0) {
        close(out);
        unlink(temporary);
        return 0;
    }
    if (close(out) != 0 || rename(temporary, V5_STATE_PIDFILE_PATH) != 0) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

static int acquire_state_lifecycle_lock(void)
{
    char start_ticks[64];
    char record[128];
    int fd = open(V5_STATE_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return 0;
    }
    if (!process_start_ticks(start_ticks, sizeof(start_ticks))) {
        close(fd);
        return 0;
    }
    snprintf(record, sizeof(record), "%ld %s\n", (long)getpid(), start_ticks);
    if (!write_owner_record(fd, record)) {
        close(fd);
        return 0;
    }
    state_lifecycle_fd = fd;
    return 1;
}

static void release_state_lifecycle_lock(void)
{
    if (state_lifecycle_fd < 0) return;
    unlink(V5_STATE_PIDFILE_PATH);
    (void)flock(state_lifecycle_fd, LOCK_UN);
    close(state_lifecycle_fd);
    state_lifecycle_fd = -1;
}
#else
static int acquire_state_lifecycle_lock(void) { return 1; }
static void release_state_lifecycle_lock(void) { }
#endif

static unsigned int parse_u32(const char *text, unsigned int default_value)
{
    char *end = 0;
    unsigned long value;
    if (!text || !text[0]) {
        return default_value;
    }
    value = strtoul(text, &end, 10);
    if (!end || *end || value > 0xfffffffful) {
        return default_value;
    }
    return (unsigned int)value;
}

static void handle_stop_signal(int signo)
{
    (void)signo;
    v5_state_publisher_request_stop();
}

int main(int argc, char **argv)
{
    const char *path = V5_STATUS_SHM_PATH;
    unsigned int frames = 0u;
    unsigned int interval_ms = V5_STATE_PUBLISHER_INTERVAL_MS;
    int i;
    V5StatePublisherReport report = {0};

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = parse_u32(argv[++i], frames);
        } else if (strcmp(argv[i], "--once") == 0) {
            frames = 1u;
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            interval_ms = parse_u32(argv[++i], interval_ms);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: v5_state_publisher [--path PATH] [--once|--frames N] [--interval-ms 33]\n");
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!v5_process_residency_lock("v5_state_publisher")) {
        return 3;
    }
    if (strcmp(path, V5_STATUS_SHM_PATH) == 0 && !acquire_state_lifecycle_lock()) {
#ifndef _WIN32
        fprintf(stderr, "v5_state_publisher lifecycle owner already active or unavailable errno=%d\n", errno);
#else
        fprintf(stderr, "v5_state_publisher lifecycle owner unavailable\n");
#endif
        return 4;
    }

    signal(SIGTERM, handle_stop_signal);
    signal(SIGINT, handle_stop_signal);

    if (!v5_state_publisher_run_loop(path, interval_ms, frames, &report)) {
        release_state_lifecycle_lock();
        return 1;
    }

    printf(
        "v5 state publisher: frames=%u interval_ms=%u path=%s valid_mask=0x%08x flags=0x%08x sample_available=%d\n",
        report.publish_count,
        report.interval_ms,
        report.path ? report.path : path,
        report.valid_mask,
        report.frame_flags,
        report.sample_available);
    release_state_lifecycle_lock();
    return 0;
}
