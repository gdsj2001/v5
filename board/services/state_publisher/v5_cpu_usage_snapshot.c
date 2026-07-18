#define _POSIX_C_SOURCE 200809L

#include "v5_cpu_usage_snapshot.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define V5_CPU_USAGE_VALID_MASK \
    ((1u << V5_CPU_USAGE_SNAPSHOT_CPU_COUNT) - 1u)
#define V5_MICROSECONDS_PER_SECOND 1000000ull
#define V5_NANOSECONDS_PER_MICROSECOND 1000ull

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ull;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static int path_join_cpu_idle(
    char *path,
    size_t path_size,
    const char *root,
    unsigned int cpu)
{
    int written;

    written = snprintf(path, path_size, "%s/cpu%u/cpuidle", root, cpu);
    return written > 0 && (size_t)written < path_size;
}

static int path_join_state_time(
    char *path,
    size_t path_size,
    const char *idle_path,
    const char *state_name)
{
    int written;

    written = snprintf(path, path_size, "%s/%s/time", idle_path, state_name);
    return written > 0 && (size_t)written < path_size;
}

static int read_u64_file(const char *path, uint64_t *value)
{
    FILE *fp;
    unsigned long long parsed;

    if (!path || !value) {
        return 0;
    }
    fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }
    if (fscanf(fp, "%llu", &parsed) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *value = (uint64_t)parsed;
    return 1;
}

static int read_cpu_idle_us(
    const char *sysfs_root,
    unsigned int cpu,
    uint64_t *idle_us)
{
    char idle_path[PATH_MAX];
    DIR *directory;
    struct dirent *entry;
    uint64_t total = 0ull;
    unsigned int state_count = 0u;
    int valid = 1;

    if (!sysfs_root || !idle_us ||
        !path_join_cpu_idle(idle_path, sizeof(idle_path), sysfs_root, cpu)) {
        return 0;
    }
    directory = opendir(idle_path);
    if (!directory) {
        return 0;
    }

    while ((entry = readdir(directory)) != NULL) {
        char time_path[PATH_MAX];
        uint64_t state_time;

        if (strncmp(entry->d_name, "state", 5u) != 0) {
            continue;
        }
        if (!path_join_state_time(
                time_path, sizeof(time_path), idle_path, entry->d_name) ||
            !read_u64_file(time_path, &state_time) ||
            UINT64_MAX - total < state_time) {
            valid = 0;
            break;
        }
        total += state_time;
        ++state_count;
    }
    closedir(directory);
    if (!valid || state_count == 0u) {
        return 0;
    }
    *idle_us = total;
    return 1;
}

static int read_cpuidle_counters(
    const char *sysfs_root,
    uint64_t idle_us[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT])
{
    unsigned int cpu;

    for (cpu = 0u; cpu < V5_CPU_USAGE_SNAPSHOT_CPU_COUNT; ++cpu) {
        if (!read_cpu_idle_us(sysfs_root, cpu, &idle_us[cpu])) {
            return 0;
        }
    }
    return 1;
}

static int ticks_to_microseconds(
    uint64_t ticks,
    uint64_t ticks_per_second,
    uint64_t *microseconds)
{
    uint64_t whole_seconds;
    uint64_t remaining_ticks;
    uint64_t whole_us;
    uint64_t remainder_us;

    if (!ticks_per_second || !microseconds) {
        return 0;
    }
    whole_seconds = ticks / ticks_per_second;
    remaining_ticks = ticks % ticks_per_second;
    if (whole_seconds > UINT64_MAX / V5_MICROSECONDS_PER_SECOND ||
        remaining_ticks > UINT64_MAX / V5_MICROSECONDS_PER_SECOND) {
        return 0;
    }
    whole_us = whole_seconds * V5_MICROSECONDS_PER_SECOND;
    remainder_us =
        (remaining_ticks * V5_MICROSECONDS_PER_SECOND) / ticks_per_second;
    if (UINT64_MAX - whole_us < remainder_us) {
        return 0;
    }
    *microseconds = whole_us + remainder_us;
    return 1;
}

static int read_proc_stat_counters(
    const char *proc_path,
    uint64_t idle_us[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT])
{
    FILE *fp;
    char line[512];
    unsigned int found_mask = 0u;
    long ticks_per_second_long;
    uint64_t ticks_per_second;

    if (!proc_path) {
        return 0;
    }
    ticks_per_second_long = sysconf(_SC_CLK_TCK);
    if (ticks_per_second_long <= 0) {
        return 0;
    }
    ticks_per_second = (uint64_t)ticks_per_second_long;

    fp = fopen(proc_path, "r");
    if (!fp) {
        return 0;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned int cpu;

        for (cpu = 0u; cpu < V5_CPU_USAGE_SNAPSHOT_CPU_COUNT; ++cpu) {
            char prefix[16];
            size_t prefix_length;
            unsigned long long fields[10] = {0ull};
            int parsed;
            uint64_t idle_ticks;

            (void)snprintf(prefix, sizeof(prefix), "cpu%u ", cpu);
            prefix_length = strlen(prefix);
            if (strncmp(line, prefix, prefix_length) != 0) {
                continue;
            }
            parsed = sscanf(
                line + prefix_length,
                "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                &fields[0], &fields[1], &fields[2], &fields[3], &fields[4],
                &fields[5], &fields[6], &fields[7], &fields[8], &fields[9]);
            if (parsed < 4 || UINT64_MAX - (uint64_t)fields[3] <
                                  (parsed >= 5 ? (uint64_t)fields[4] : 0ull)) {
                fclose(fp);
                return 0;
            }
            idle_ticks = (uint64_t)fields[3] +
                         (parsed >= 5 ? (uint64_t)fields[4] : 0ull);
            if (!ticks_to_microseconds(
                    idle_ticks, ticks_per_second, &idle_us[cpu])) {
                fclose(fp);
                return 0;
            }
            found_mask |= 1u << cpu;
            break;
        }
    }
    fclose(fp);
    return found_mask == V5_CPU_USAGE_VALID_MASK;
}

static int read_idle_counters(
    const char *sysfs_root,
    const char *proc_path,
    uint64_t idle_us[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT],
    unsigned int *source)
{
    if (read_cpuidle_counters(sysfs_root, idle_us)) {
        *source = V5_CPU_USAGE_SOURCE_CPUIDLE;
        return 1;
    }
    if (read_proc_stat_counters(proc_path, idle_us)) {
        *source = V5_CPU_USAGE_SOURCE_PROC_STAT;
        return 1;
    }
    *source = V5_CPU_USAGE_SOURCE_NONE;
    return 0;
}

static double busy_percent(uint64_t idle_delta_us, uint64_t elapsed_ns)
{
    double idle_fraction;
    double busy;

    idle_fraction =
        ((double)idle_delta_us * (double)V5_NANOSECONDS_PER_MICROSECOND) /
        (double)elapsed_ns;
    busy = (1.0 - idle_fraction) * 100.0;
    if (busy < 0.0) {
        return 0.0;
    }
    if (busy > 100.0) {
        return 100.0;
    }
    return busy;
}

static int return_cached(
    const V5CpuUsageSnapshotSampler *sampler,
    V5CpuUsageSnapshot *snapshot)
{
    if (!sampler->has_snapshot) {
        memset(snapshot, 0, sizeof(*snapshot));
        return 0;
    }
    *snapshot = sampler->cached;
    return 1;
}

void v5_cpu_usage_snapshot_sampler_init(V5CpuUsageSnapshotSampler *sampler)
{
    if (sampler) {
        memset(sampler, 0, sizeof(*sampler));
    }
}

int v5_cpu_usage_snapshot_read_at(
    V5CpuUsageSnapshotSampler *sampler,
    V5CpuUsageSnapshot *snapshot,
    uint64_t now_ns,
    const char *sysfs_root,
    const char *proc_path)
{
    uint64_t idle_us[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT];
    unsigned int source = V5_CPU_USAGE_SOURCE_NONE;
    unsigned int cpu;
    uint64_t elapsed_ns;

    if (!sampler || !snapshot || !now_ns || !sysfs_root || !proc_path) {
        return 0;
    }

    if (sampler->last_io_monotonic_ns != 0ull) {
        if (now_ns < sampler->last_io_monotonic_ns) {
            v5_cpu_usage_snapshot_sampler_init(sampler);
        } else if (now_ns - sampler->last_io_monotonic_ns <
                   V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS) {
            return return_cached(sampler, snapshot);
        }
    }

    sampler->last_io_monotonic_ns = now_ns;
    ++sampler->io_sample_count;
    if (!read_idle_counters(sysfs_root, proc_path, idle_us, &source)) {
        return return_cached(sampler, snapshot);
    }

    if (!sampler->has_baseline || sampler->baseline_source != source ||
        now_ns <= sampler->baseline_monotonic_ns) {
        memcpy(sampler->baseline_idle_us, idle_us, sizeof(idle_us));
        sampler->baseline_monotonic_ns = now_ns;
        sampler->baseline_source = source;
        sampler->has_baseline = 1u;
        return return_cached(sampler, snapshot);
    }

    elapsed_ns = now_ns - sampler->baseline_monotonic_ns;
    for (cpu = 0u; cpu < V5_CPU_USAGE_SNAPSHOT_CPU_COUNT; ++cpu) {
        if (idle_us[cpu] < sampler->baseline_idle_us[cpu]) {
            memcpy(sampler->baseline_idle_us, idle_us, sizeof(idle_us));
            sampler->baseline_monotonic_ns = now_ns;
            sampler->baseline_source = source;
            return return_cached(sampler, snapshot);
        }
    }

    ++sampler->generation;
    if (sampler->generation == 0ull) {
        sampler->generation = 1ull;
    }
    memset(&sampler->cached, 0, sizeof(sampler->cached));
    sampler->cached.generation = sampler->generation;
    sampler->cached.monotonic_ns = now_ns;
    sampler->cached.valid_mask = V5_CPU_USAGE_VALID_MASK;
    sampler->cached.source = source;
    for (cpu = 0u; cpu < V5_CPU_USAGE_SNAPSHOT_CPU_COUNT; ++cpu) {
        sampler->cached.busy_percent[cpu] = busy_percent(
            idle_us[cpu] - sampler->baseline_idle_us[cpu], elapsed_ns);
    }
    sampler->has_snapshot = 1u;

    memcpy(sampler->baseline_idle_us, idle_us, sizeof(idle_us));
    sampler->baseline_monotonic_ns = now_ns;
    sampler->baseline_source = source;
    *snapshot = sampler->cached;
    return 1;
}

int v5_cpu_usage_snapshot_read(
    V5CpuUsageSnapshotSampler *sampler,
    V5CpuUsageSnapshot *snapshot)
{
    uint64_t now_ns = monotonic_ns();

    if (!now_ns) {
        return 0;
    }
    return v5_cpu_usage_snapshot_read_at(
        sampler,
        snapshot,
        now_ns,
        V5_CPU_USAGE_SNAPSHOT_DEFAULT_SYSFS_ROOT,
        V5_CPU_USAGE_SNAPSHOT_DEFAULT_PROC_PATH);
}
