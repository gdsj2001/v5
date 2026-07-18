#ifndef V5_CPU_USAGE_SNAPSHOT_H
#define V5_CPU_USAGE_SNAPSHOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_CPU_USAGE_SNAPSHOT_CPU_COUNT 2u
#define V5_CPU_USAGE_SNAPSHOT_INTERVAL_NS 2000000000ull
#define V5_CPU_USAGE_SNAPSHOT_DEFAULT_SYSFS_ROOT "/sys/devices/system/cpu"
#define V5_CPU_USAGE_SNAPSHOT_DEFAULT_PROC_PATH "/proc/stat"

typedef enum V5CpuUsageSnapshotSource {
    V5_CPU_USAGE_SOURCE_NONE = 0,
    V5_CPU_USAGE_SOURCE_CPUIDLE = 1,
    V5_CPU_USAGE_SOURCE_PROC_STAT = 2
} V5CpuUsageSnapshotSource;

typedef struct V5CpuUsageSnapshot {
    uint64_t generation;
    uint64_t monotonic_ns;
    unsigned int valid_mask;
    unsigned int source;
    double busy_percent[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT];
} V5CpuUsageSnapshot;

/*
 * One resident sampler owns the baseline and the most recent immutable
 * two-CPU display snapshot.  io_sample_count is diagnostic evidence that
 * high-rate consumers reused the cache instead of rereading kernel files.
 */
typedef struct V5CpuUsageSnapshotSampler {
    uint64_t generation;
    uint64_t baseline_monotonic_ns;
    uint64_t last_io_monotonic_ns;
    uint64_t baseline_idle_us[V5_CPU_USAGE_SNAPSHOT_CPU_COUNT];
    uint64_t io_sample_count;
    unsigned int baseline_source;
    unsigned int has_baseline;
    unsigned int has_snapshot;
    V5CpuUsageSnapshot cached;
} V5CpuUsageSnapshotSampler;

void v5_cpu_usage_snapshot_sampler_init(V5CpuUsageSnapshotSampler *sampler);

/* Runtime entry: obtains now from CLOCK_MONOTONIC and uses canonical paths. */
int v5_cpu_usage_snapshot_read(
    V5CpuUsageSnapshotSampler *sampler,
    V5CpuUsageSnapshot *snapshot);

/*
 * Deterministic entry for smoke tests and explicit mount namespaces.
 * A nonzero return means snapshot contains the current cached generation.
 * A zero return means that a baseline exists but no complete interval has
 * been published yet, or that no valid source has ever been sampled.
 */
int v5_cpu_usage_snapshot_read_at(
    V5CpuUsageSnapshotSampler *sampler,
    V5CpuUsageSnapshot *snapshot,
    uint64_t now_ns,
    const char *sysfs_root,
    const char *proc_path);

#ifdef __cplusplus
}
#endif

#endif
