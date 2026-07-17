#include "v5_state_publisher_service.h"

#include <stdint.h>
#include <stdio.h>

#define NS_PER_MS 1000000ull

static int require_equal_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return 1;
    }
    fprintf(
        stderr,
        "%s: actual=%llu expected=%llu\n",
        name,
        (unsigned long long)actual,
        (unsigned long long)expected);
    return 0;
}

int main(void)
{
    const uint64_t workload_ns = 5ull * NS_PER_MS;
    const uint64_t period_ns =
        (uint64_t)V5_STATE_PUBLISHER_INTERVAL_MS * NS_PER_MS;
    const uint64_t cadence_origin_ns = 1000ull * NS_PER_MS;
    uint64_t sample_start_ns = cadence_origin_ns;
    unsigned int interval;

    if (!require_equal_u64(
            "default_interval_ms",
            V5_STATE_PUBLISHER_INTERVAL_MS,
            33ull)) {
        return 1;
    }

    /* Thirty intervals between 31 sample starts must remain 30 * 33ms. */
    for (interval = 0u; interval < 30u; ++interval) {
        uint64_t after_publish_ns = sample_start_ns + workload_ns;
        uint64_t wait_ns = v5_state_publisher_cadence_wait_ns(
            sample_start_ns,
            after_publish_ns,
            V5_STATE_PUBLISHER_INTERVAL_MS);

        if (!require_equal_u64("five_ms_work_wait", wait_ns, 28ull * NS_PER_MS)) {
            return 2;
        }
        sample_start_ns = after_publish_ns + wait_ns;
    }
    if (!require_equal_u64(
            "thirty_start_intervals",
            sample_start_ns,
            cadence_origin_ns + 30ull * period_ns)) {
        return 3;
    }

    if (!require_equal_u64(
            "clock_failure_uses_bounded_sleep",
            v5_state_publisher_cadence_wait_ns(
                0ull, 0ull, V5_STATE_PUBLISHER_INTERVAL_MS),
            period_ns)) {
        return 4;
    }

    /* A single overrun starts the next sample immediately and re-anchors it. */
    sample_start_ns = 100ull * NS_PER_MS;
    if (!require_equal_u64(
            "overrun_has_no_backlog_sleep",
            v5_state_publisher_cadence_wait_ns(
                sample_start_ns,
                sample_start_ns + 40ull * NS_PER_MS,
                V5_STATE_PUBLISHER_INTERVAL_MS),
            0ull)) {
        return 5;
    }
    sample_start_ns += 40ull * NS_PER_MS;
    if (!require_equal_u64(
            "post_overrun_reanchored_wait",
            v5_state_publisher_cadence_wait_ns(
                sample_start_ns,
                sample_start_ns + workload_ns,
                V5_STATE_PUBLISHER_INTERVAL_MS),
            28ull * NS_PER_MS)) {
        return 6;
    }

    printf(
        "V5_STATE_PUBLISHER_CADENCE_OK interval_ms=%u workload_ms=5 "
        "sample_intervals=30 elapsed_ms=%llu\n",
        V5_STATE_PUBLISHER_INTERVAL_MS,
        (unsigned long long)((30ull * period_ns) / NS_PER_MS));
    return 0;
}
