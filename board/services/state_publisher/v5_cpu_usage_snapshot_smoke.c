#define _POSIX_C_SOURCE 200809L

#include "v5_cpu_usage_snapshot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int make_directory(const char *path)
{
    return mkdir(path, 0700) == 0;
}

static int write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");

    if (!fp) {
        return 0;
    }
    if (fputs(text, fp) == EOF || fclose(fp) != 0) {
        return 0;
    }
    return 1;
}

static int write_u64(const char *path, uint64_t value)
{
    char text[64];

    (void)snprintf(text, sizeof(text), "%llu\n", (unsigned long long)value);
    return write_text(path, text);
}

static int near_value(double actual, double expected)
{
    return fabs(actual - expected) < 0.01;
}

static int require_true(int condition, const char *name)
{
    if (condition) {
        return 1;
    }
    fprintf(stderr, "FAILED: %s\n", name);
    return 0;
}

static void cleanup_tree(const char *root)
{
    char path[PATH_MAX];
    unsigned int cpu;
    const unsigned int states[2][2] = {{0u, 1u}, {0u, 2u}};
    unsigned int state;

    (void)snprintf(path, sizeof(path), "%s/proc_stat", root);
    (void)unlink(path);
    for (cpu = 0u; cpu < 2u; ++cpu) {
        for (state = 0u; state < 2u; ++state) {
            (void)snprintf(
                path,
                sizeof(path),
                "%s/sys/cpu%u/cpuidle/state%u/time",
                root,
                cpu,
                states[cpu][state]);
            (void)unlink(path);
            (void)snprintf(
                path,
                sizeof(path),
                "%s/sys/cpu%u/cpuidle/state%u",
                root,
                cpu,
                states[cpu][state]);
            (void)rmdir(path);
        }
        (void)snprintf(path, sizeof(path), "%s/sys/cpu%u/cpuidle", root, cpu);
        (void)rmdir(path);
        (void)snprintf(path, sizeof(path), "%s/sys/cpu%u", root, cpu);
        (void)rmdir(path);
    }
    (void)snprintf(path, sizeof(path), "%s/sys", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static int create_cpuidle_tree(
    const char *root,
    char *sysfs_root,
    size_t sysfs_root_size,
    char time_paths[2][2][PATH_MAX])
{
    char path[PATH_MAX];
    const unsigned int states[2][2] = {{0u, 1u}, {0u, 2u}};
    unsigned int cpu;
    unsigned int state;

    (void)snprintf(sysfs_root, sysfs_root_size, "%s/sys", root);
    if (!make_directory(sysfs_root)) {
        return 0;
    }
    for (cpu = 0u; cpu < 2u; ++cpu) {
        (void)snprintf(path, sizeof(path), "%s/cpu%u", sysfs_root, cpu);
        if (!make_directory(path)) {
            return 0;
        }
        (void)snprintf(path, sizeof(path), "%s/cpu%u/cpuidle", sysfs_root, cpu);
        if (!make_directory(path)) {
            return 0;
        }
        for (state = 0u; state < 2u; ++state) {
            (void)snprintf(
                path,
                sizeof(path),
                "%s/cpu%u/cpuidle/state%u",
                sysfs_root,
                cpu,
                states[cpu][state]);
            if (!make_directory(path)) {
                return 0;
            }
            (void)snprintf(
                time_paths[cpu][state],
                PATH_MAX,
                "%s/time",
                path);
        }
    }
    return 1;
}

static int exercise_cpuidle(
    const char *sysfs_root,
    const char *proc_path,
    char time_paths[2][2][PATH_MAX])
{
    const uint64_t start_ns = 10000000000ull;
    V5CpuUsageSnapshotSampler sampler;
    V5CpuUsageSnapshot snapshot;

    if (!write_u64(time_paths[0][0], 100000ull) ||
        !write_u64(time_paths[0][1], 200000ull) ||
        !write_u64(time_paths[1][0], 400000ull) ||
        !write_u64(time_paths[1][1], 500000ull)) {
        return 0;
    }

    v5_cpu_usage_snapshot_sampler_init(&sampler);
    if (!require_true(
            !v5_cpu_usage_snapshot_read_at(
                &sampler, &snapshot, start_ns, sysfs_root, proc_path),
            "first cpuidle read establishes baseline") ||
        !require_true(sampler.io_sample_count == 1ull, "baseline one I/O") ||
        !require_true(sampler.generation == 0ull, "baseline no generation")) {
        return 0;
    }

    if (!write_u64(time_paths[0][0], 200000ull) ||
        !write_u64(time_paths[0][1], 600000ull) ||
        !write_u64(time_paths[1][0], 1000000ull) ||
        !write_u64(time_paths[1][1], 1400000ull)) {
        return 0;
    }
    if (!require_true(
            !v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns + 33000000ull,
                sysfs_root,
                proc_path),
            "33ms before first interval remains unavailable") ||
        !require_true(sampler.io_sample_count == 1ull, "33ms performs no I/O")) {
        return 0;
    }

    if (!require_true(
            v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns + V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS,
                sysfs_root,
                proc_path),
            "two second cpuidle snapshot") ||
        !require_true(snapshot.generation == 1ull, "generation one") ||
        !require_true(
            snapshot.monotonic_ns ==
                start_ns + V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS,
            "shared sample time") ||
        !require_true(snapshot.valid_mask == 3u, "two CPUs valid") ||
        !require_true(
            snapshot.source == V5_CPU_USAGE_SOURCE_CPUIDLE,
            "cpuidle preferred") ||
        !require_true(near_value(snapshot.busy_percent[0], 75.0),
                      "cpu0 multi-state sum") ||
        !require_true(near_value(snapshot.busy_percent[1], 25.0),
                      "cpu1 multi-state sum") ||
        !require_true(sampler.io_sample_count == 2ull, "interval second I/O")) {
        return 0;
    }

    if (!write_u64(time_paths[0][0], 9000000ull) ||
        !write_u64(time_paths[0][1], 9000000ull)) {
        return 0;
    }
    if (!require_true(
            v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns + V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS + 33000000ull,
                sysfs_root,
                proc_path),
            "33ms returns cache") ||
        !require_true(snapshot.generation == 1ull, "cache generation stable") ||
        !require_true(near_value(snapshot.busy_percent[0], 75.0),
                      "cache value stable") ||
        !require_true(sampler.io_sample_count == 2ull, "cache no repeated I/O")) {
        return 0;
    }

    /* Restore monotonic counters: zero idle delta clamps to 100% busy; an
     * idle delta above wall time clamps to 0% busy. */
    if (!write_u64(time_paths[0][0], 200000ull) ||
        !write_u64(time_paths[0][1], 600000ull) ||
        !write_u64(time_paths[1][0], 2500000ull) ||
        !write_u64(time_paths[1][1], 3900000ull)) {
        return 0;
    }
    if (!require_true(
            v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns + 2ull * V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS,
                sysfs_root,
                proc_path),
            "second cpuidle generation") ||
        !require_true(snapshot.generation == 2ull, "generation increments once") ||
        !require_true(near_value(snapshot.busy_percent[0], 100.0),
                      "busy upper clamp") ||
        !require_true(near_value(snapshot.busy_percent[1], 0.0),
                      "busy lower clamp")) {
        return 0;
    }
    return 1;
}

static int exercise_proc_fallback(
    const char *missing_sysfs_root,
    const char *proc_path)
{
    const uint64_t start_ns = 30000000000ull;
    long ticks_per_second = sysconf(_SC_CLK_TCK);
    V5CpuUsageSnapshotSampler sampler;
    V5CpuUsageSnapshot snapshot;
    char text[512];

    if (ticks_per_second <= 0) {
        return 0;
    }
    if (!write_text(
            proc_path,
            "cpu 0 0 0 0 0 0 0 0 0 0\n"
            "cpu0 10 0 5 1000 100 0 0 0 0 0\n"
            "cpu1 10 0 5 2000 200 0 0 0 0 0\n")) {
        return 0;
    }

    v5_cpu_usage_snapshot_sampler_init(&sampler);
    if (!require_true(
            !v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns,
                missing_sysfs_root,
                proc_path),
            "proc fallback baseline")) {
        return 0;
    }

    (void)snprintf(
        text,
        sizeof(text),
        "cpu 0 0 0 0 0 0 0 0 0 0\n"
        "cpu0 20 0 10 %llu 100 0 0 0 0 0\n"
        "cpu1 20 0 10 %llu 200 0 0 0 0 0\n",
        1000ull + (unsigned long long)ticks_per_second,
        2000ull + 2ull * (unsigned long long)ticks_per_second);
    if (!write_text(proc_path, text)) {
        return 0;
    }

    if (!require_true(
            v5_cpu_usage_snapshot_read_at(
                &sampler,
                &snapshot,
                start_ns + V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS,
                missing_sysfs_root,
                proc_path),
            "proc fallback snapshot") ||
        !require_true(snapshot.generation == 1ull, "fallback generation") ||
        !require_true(
            snapshot.source == V5_CPU_USAGE_SOURCE_PROC_STAT,
            "proc used only without cpuidle") ||
        !require_true(near_value(snapshot.busy_percent[0], 50.0),
                      "proc cpu0 wall idle delta") ||
        !require_true(near_value(snapshot.busy_percent[1], 0.0),
                      "proc cpu1 wall idle delta")) {
        return 0;
    }
    return 1;
}

int main(void)
{
    char root_template[] = "/tmp/v5_cpu_usage_snapshot_XXXXXX";
    char *root;
    char sysfs_root[PATH_MAX];
    char missing_sysfs_root[PATH_MAX];
    char proc_path[PATH_MAX];
    char time_paths[2][2][PATH_MAX];
    int ok;

    root = mkdtemp(root_template);
    if (!root) {
        perror("mkdtemp");
        return 1;
    }
    (void)snprintf(proc_path, sizeof(proc_path), "%s/proc_stat", root);
    (void)snprintf(
        missing_sysfs_root, sizeof(missing_sysfs_root), "%s/missing", root);

    ok = create_cpuidle_tree(
             root, sysfs_root, sizeof(sysfs_root), time_paths) &&
         exercise_cpuidle(sysfs_root, proc_path, time_paths) &&
         exercise_proc_fallback(missing_sysfs_root, proc_path);
    cleanup_tree(root);
    if (!ok) {
        return 2;
    }

    printf(
        "V5_CPU_USAGE_SNAPSHOT_OK interval_ns=%llu cpus=%u "
        "cpuidle_dynamic=1 proc_fallback=1\n",
        (unsigned long long)V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS,
        V5_CPU_USAGE_SNAPSHOT_CPU_COUNT);
    return 0;
}
