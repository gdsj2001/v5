#define _GNU_SOURCE

#include "v5_position_status_publisher_core.h"
#include "v5_position_status_sampler.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define V5_POSITION_LOCK_PATH "/run/8ax/v5_position_status_publisher.lock"
#define V5_POSITION_PID_PATH "/run/8ax/v5_position_status_publisher.pid"
#define V5_BUS_STATUS_DEFAULT_PATH "/dev/shm/v5_native_bus_status.bin"
#define V5_POSITION_HEARTBEAT_NS 100000000ull
#define V5_BUS_SAMPLE_NS 200000000ull
#define V5_STATUS_LOG_NS 5000000000ull
#define V5_MIN_INTERVAL_MS 20u

typedef struct V5MappedWriter {
    int fd;
    void *page;
    size_t size;
    uint32_t sequence;
} V5MappedWriter;

typedef struct V5LifecycleLock {
    int fd;
    char lock_path[512];
    char pid_path[512];
    char record[128];
    uint32_t writer_identity;
} V5LifecycleLock;

static volatile sig_atomic_t stop_requested;

static void memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0ull;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void stop_handler(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int mkdir_parent(const char *path)
{
    char copy[512];
    char *slash;
    if (!path || strlen(path) >= sizeof(copy)) return 0;
    strcpy(copy, path);
    slash = strrchr(copy, '/');
    if (!slash || slash == copy) return 1;
    *slash = '\0';
    if (mkdir(copy, 0755) == 0 || errno == EEXIST) return 1;
    return 0;
}

static uint32_t random_identity(void)
{
    uint32_t value = 0u;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t count = read(fd, &value, sizeof(value));
        close(fd);
        if (count != (ssize_t)sizeof(value)) value = 0u;
    }
    if (!value) {
        uint64_t now = monotonic_ns();
        value = (uint32_t)(now ^ (now >> 32) ^ (uint64_t)getpid());
        if (!value) value = 1u;
    }
    return value;
}

static int process_start_ticks(char *buffer, size_t size)
{
    char line[2048];
    char *tail;
    char *token;
    char *save = NULL;
    int field = 3;
    FILE *stream;

    if (!buffer || !size) return 0;
    stream = fopen("/proc/self/stat", "r");
    if (!stream) return 0;
    if (!fgets(line, sizeof(line), stream)) {
        fclose(stream);
        return 0;
    }
    fclose(stream);
    tail = strrchr(line, ')');
    if (!tail || tail[1] != ' ') return 0;
    token = strtok_r(tail + 2, " ", &save);
    while (token) {
        if (field == 22) {
            size_t length = strcspn(token, "\r\n");
            if (length + 1u > size) return 0;
            memcpy(buffer, token, length);
            buffer[length] = '\0';
            return 1;
        }
        ++field;
        token = strtok_r(NULL, " ", &save);
    }
    return 0;
}

static int write_pid_record(const char *path, const char *record)
{
    char temporary[600];
    int fd;
    size_t length;
    if (!path || !record || !mkdir_parent(path) ||
        snprintf(temporary, sizeof(temporary), "%s.%ld.tmp", path,
            (long)getpid()) >= (int)sizeof(temporary)) {
        return 0;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    length = strlen(record);
    if (write(fd, record, length) != (ssize_t)length || fsync(fd) != 0 ||
        close(fd) != 0 || rename(temporary, path) != 0) {
        close(fd);
        unlink(temporary);
        return 0;
    }
    return 1;
}

static int lifecycle_acquire(
    V5LifecycleLock *lifecycle,
    const char *lock_path,
    const char *pid_path)
{
    char start[64];
    size_t length;
    if (!lifecycle || !lock_path || !pid_path ||
        strlen(lock_path) >= sizeof(lifecycle->lock_path) ||
        strlen(pid_path) >= sizeof(lifecycle->pid_path) ||
        !mkdir_parent(lock_path) || !process_start_ticks(start, sizeof(start))) {
        return 0;
    }
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->fd = -1;
    strcpy(lifecycle->lock_path, lock_path);
    strcpy(lifecycle->pid_path, pid_path);
    lifecycle->fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lifecycle->fd < 0) return 0;
    if (flock(lifecycle->fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "position_publisher_already_running\n");
        close(lifecycle->fd);
        lifecycle->fd = -1;
        return 0;
    }
    lifecycle->writer_identity = random_identity();
    if (snprintf(lifecycle->record, sizeof(lifecycle->record), "%ld %s %u\n",
            (long)getpid(), start, lifecycle->writer_identity) >=
        (int)sizeof(lifecycle->record)) {
        return 0;
    }
    length = strlen(lifecycle->record);
    if (ftruncate(lifecycle->fd, 0) != 0 ||
        lseek(lifecycle->fd, 0, SEEK_SET) < 0 ||
        write(lifecycle->fd, lifecycle->record, length) != (ssize_t)length ||
        fsync(lifecycle->fd) != 0 ||
        !write_pid_record(pid_path, lifecycle->record)) {
        return 0;
    }
    return 1;
}

static void lifecycle_release(V5LifecycleLock *lifecycle)
{
    char record[128];
    FILE *stream;
    if (!lifecycle || lifecycle->fd < 0) return;
    stream = fopen(lifecycle->pid_path, "r");
    if (stream) {
        size_t count = fread(record, 1u, sizeof(record) - 1u, stream);
        fclose(stream);
        record[count] = '\0';
        if (strcmp(record, lifecycle->record) == 0) unlink(lifecycle->pid_path);
    }
    flock(lifecycle->fd, LOCK_UN);
    close(lifecycle->fd);
    lifecycle->fd = -1;
}

static int mapped_writer_open(
    V5MappedWriter *writer,
    const char *path,
    size_t size)
{
    if (!writer || !path || !size || !mkdir_parent(path)) return 0;
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
    writer->fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (writer->fd < 0 || ftruncate(writer->fd, (off_t)size) != 0) return 0;
    writer->page = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
        writer->fd, 0);
    if (writer->page == MAP_FAILED) {
        writer->page = NULL;
        return 0;
    }
    writer->size = size;
    return 1;
}

static void mapped_writer_close(V5MappedWriter *writer)
{
    if (!writer) return;
    if (writer->page) munmap(writer->page, writer->size);
    if (writer->fd >= 0) close(writer->fd);
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
}

static uint32_t next_sequence(V5MappedWriter *writer)
{
    writer->sequence += 2u;
    if (!writer->sequence) writer->sequence = 2u;
    return writer->sequence;
}

static void mapped_publish(
    V5MappedWriter *writer,
    const void *payload,
    size_t sequence_offset,
    uint32_t even_sequence)
{
    unsigned char *page = (unsigned char *)writer->page;
    const unsigned char *source = (const unsigned char *)payload;
    uint32_t odd_sequence = even_sequence > 1u ? even_sequence - 1u : 1u;
    memcpy(page + sequence_offset, &odd_sequence, sizeof(odd_sequence));
    memory_barrier();
    memcpy(page, source, sequence_offset);
    memcpy(page + sequence_offset + sizeof(uint32_t),
        source + sequence_offset + sizeof(uint32_t),
        writer->size - sequence_offset - sizeof(uint32_t));
    memory_barrier();
    memcpy(page + sequence_offset, &even_sequence, sizeof(even_sequence));
    memory_barrier();
}

static int sleep_until(uint64_t deadline_ns)
{
    struct timespec deadline;
    deadline.tv_sec = (time_t)(deadline_ns / 1000000000ull);
    deadline.tv_nsec = (long)(deadline_ns % 1000000000ull);
    while (!stop_requested) {
        int result = clock_nanosleep(
            CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        if (result == 0) return 1;
        if (result != EINTR) return 0;
    }
    return 1;
}

static void usage(const char *program)
{
    printf("usage: %s [--path PATH] [--bus-path PATH] "
        "[--interval-ms 33] [--once]\n", program);
}

int main(int argc, char **argv)
{
    const char *position_path = V5_POSITION_DEFAULT_PATH;
    const char *bus_path = V5_BUS_STATUS_DEFAULT_PATH;
    unsigned int interval_ms = 33u;
    int once = 0;
    int canonical;
    char lock_path[600];
    char pid_path[600];
    V5LifecycleLock lifecycle;
    V5MappedWriter position_writer;
    V5MappedWriter bus_writer;
    V5PositionStatusSampler sampler;
    V5NativePositionStatusBlock block;
    V5NativePositionStatusBlock last_published;
    int last_published_valid = 0;
    uint64_t last_publish_ns = 0ull;
    uint64_t source_generation = 0ull;
    uint32_t bus_source_generation = 0u;
    uint64_t next_bus_sample = 0ull;
    uint64_t next_status_log = 0ull;
    uint64_t next_deadline;
    unsigned long samples = 0ul;
    unsigned long published = 0ul;
    unsigned long missed_slots = 0ul;
    unsigned int consecutive_failures = 0u;
    int result = 1;
    int index;

    memset(&lifecycle, 0, sizeof(lifecycle));
    lifecycle.fd = -1;
    memset(&position_writer, 0, sizeof(position_writer));
    position_writer.fd = -1;
    memset(&bus_writer, 0, sizeof(bus_writer));
    bus_writer.fd = -1;
    memset(&sampler, 0, sizeof(sampler));
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--path") == 0 && index + 1 < argc) {
            position_path = argv[++index];
        } else if (strcmp(argv[index], "--bus-path") == 0 && index + 1 < argc) {
            bus_path = argv[++index];
        } else if (strcmp(argv[index], "--interval-ms") == 0 &&
                   index + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++index], &end, 10);
            if (!end || *end || value > 60000ul) {
                usage(argv[0]);
                return 2;
            }
            interval_ms = (unsigned int)value;
        } else if (strcmp(argv[index], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[index], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (interval_ms < V5_MIN_INTERVAL_MS) interval_ms = V5_MIN_INTERVAL_MS;
    canonical = strcmp(position_path, V5_POSITION_DEFAULT_PATH) == 0;
    if (canonical) {
        strcpy(lock_path, V5_POSITION_LOCK_PATH);
        strcpy(pid_path, V5_POSITION_PID_PATH);
    } else if (snprintf(lock_path, sizeof(lock_path), "%s.publisher.lock",
                   position_path) >= (int)sizeof(lock_path) ||
               snprintf(pid_path, sizeof(pid_path), "%s.publisher.pid",
                   position_path) >= (int)sizeof(pid_path)) {
        fprintf(stderr, "position_publisher_path_too_long\n");
        return 2;
    }
    if (!lifecycle_acquire(&lifecycle, lock_path, pid_path)) goto cleanup;
    if (!mapped_writer_open(
            &position_writer, position_path,
            sizeof(V5NativePositionStatusBlock)) ||
        !mapped_writer_open(&bus_writer, bus_path, sizeof(V5BusStatusBlock))) {
        fprintf(stderr, "native_position_mapping_unavailable\n");
        goto cleanup;
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "v5_position_status_publisher mlockall failed: errno=%d\n",
            errno);
        goto cleanup;
    }
    if (!v5_position_status_sampler_open(&sampler)) goto cleanup;
    signal(SIGTERM, stop_handler);
    signal(SIGINT, stop_handler);
    next_deadline = monotonic_ns();
    while (!stop_requested) {
        V5PositionSourceSnapshot source;
        uint64_t sample_now = monotonic_ns();
        uint32_t sequence;
        int should_publish;
        if (v5_position_status_sampler_sample_source(
                &sampler, &source)) {
            ++source_generation;
            sequence = position_writer.sequence + 2u;
            if (!sequence) sequence = 2u;
            if (v5_position_status_build(
                    &source, lifecycle.writer_identity, sequence, sample_now,
                    source_generation, &block)) {
                should_publish = !last_published_valid ||
                    !v5_position_status_display_equal(&block, &last_published) ||
                    sample_now - last_publish_ns >= V5_POSITION_HEARTBEAT_NS;
                if (should_publish) {
                    next_sequence(&position_writer);
                    mapped_publish(&position_writer, &block,
                        offsetof(V5NativePositionStatusBlock, seq), block.seq);
                    last_published = block;
                    last_published_valid = 1;
                    last_publish_ns = sample_now;
                    ++published;
                }
                consecutive_failures = 0u;
                ++samples;
                if (sample_now >= next_bus_sample) {
                    V5BusStatusBlock bus_block;
                    uint32_t bus_sequence = next_sequence(&bus_writer);
                    ++bus_source_generation;
                    v5_position_status_sampler_sample_bus(
                        &sampler, lifecycle.writer_identity, bus_sequence,
                        bus_source_generation, monotonic_ns(), &bus_block);
                    mapped_publish(&bus_writer, &bus_block,
                        offsetof(V5BusStatusBlock, sequence), bus_sequence);
                    next_bus_sample = sample_now + V5_BUS_SAMPLE_NS;
                }
                if (once) {
                    result = 0;
                    break;
                }
            } else {
                ++consecutive_failures;
            }
        } else {
            ++consecutive_failures;
        }
        if (consecutive_failures == 1u || sample_now >= next_status_log) {
            if (consecutive_failures) {
                fprintf(stderr,
                    "v5_position_status_publisher sample unavailable: "
                    "native_position_sample_invalid\n");
            }
        }
        if (sample_now >= next_status_log) {
            printf("v5_position_status_publisher samples=%lu published=%lu "
                "missed_slots=%lu\n", samples, published, missed_slots);
            fflush(stdout);
            samples = 0ul;
            published = 0ul;
            missed_slots = 0ul;
            next_status_log = sample_now + V5_STATUS_LOG_NS;
        }
        next_deadline += (uint64_t)interval_ms * 1000000ull;
        sample_now = monotonic_ns();
        if (sample_now > next_deadline) {
            uint64_t interval_ns = (uint64_t)interval_ms * 1000000ull;
            uint64_t missed = (sample_now - next_deadline) / interval_ns + 1ull;
            next_deadline += missed * interval_ns;
            missed_slots += (unsigned long)missed;
        }
        if (!sleep_until(next_deadline)) {
            fprintf(stderr, "position_publisher_sleep_failed\n");
            goto cleanup;
        }
    }
    if (stop_requested) result = 0;

cleanup:
    v5_position_status_sampler_close(&sampler);
    mapped_writer_close(&bus_writer);
    mapped_writer_close(&position_writer);
    lifecycle_release(&lifecycle);
    return result;
}
