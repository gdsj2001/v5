#include "v5_remote_metrics.h"

#include <math.h>
#include <stdio.h>

void v5_remote_metrics_display_text(
    const V5UiStatusView *status,
    char *cpu0,
    size_t cpu0_size,
    char *cpu1,
    size_t cpu1_size)
{
    if (!cpu0 || !cpu1) {
        return;
    }
    if (!status || (status->valid_mask & V5_STATUS_VALID_CPU_USAGE) == 0U ||
        !isfinite(status->cpu0_percent) || status->cpu0_percent < 0.0 || status->cpu0_percent > 100.0 ||
        !isfinite(status->cpu1_percent) || status->cpu1_percent < 0.0 || status->cpu1_percent > 100.0 ||
        status->cpu_sample_generation == 0ULL || status->cpu_sample_monotonic_ns == 0ULL) {
        snprintf(cpu0, cpu0_size, "cpu0  --%%");
        snprintf(cpu1, cpu1_size, "cpu1  --%%");
        return;
    }
    snprintf(cpu0, cpu0_size, "cpu0  %.0f%%", status->cpu0_percent);
    snprintf(cpu1, cpu1_size, "cpu1  %.0f%%", status->cpu1_percent);
}
