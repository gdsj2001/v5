#ifndef V5_STATE_PUBLISHER_SERVICE_H
#define V5_STATE_PUBLISHER_SERVICE_H

#include "v5_status_shm.h"
#include "v5_status_shm_mmap.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_STATE_PUBLISHER_INTERVAL_MS 33u

/*
 * Return only the remainder of a start-to-start publish period.  The caller
 * samples start_ns immediately before doing publish work, then samples now_ns
 * after that work.  A workload shorter than the period therefore consumes
 * part of the period instead of being added to it.  An overrun returns zero;
 * the next iteration re-anchors at its own start and cannot busy-loop through
 * a backlog of historical deadlines.
 */
static inline uint64_t v5_state_publisher_cadence_wait_ns(
    uint64_t start_ns,
    uint64_t now_ns,
    unsigned int interval_ms)
{
    uint64_t period_ns =
        (uint64_t)(interval_ms ? interval_ms : V5_STATE_PUBLISHER_INTERVAL_MS) *
        1000000ull;
    uint64_t deadline_ns = start_ns + period_ns;

    if (start_ns == 0ull || now_ns == 0ull || now_ns < start_ns) {
        return period_ns;
    }
    return now_ns < deadline_ns ? deadline_ns - now_ns : 0ull;
}

typedef struct V5StatePublisherReport {
    int sample_available;
    unsigned int valid_mask;
    unsigned int frame_flags;
    unsigned int publish_count;
    unsigned int interval_ms;
    const char *path;
} V5StatePublisherReport;

void v5_state_publisher_request_stop(void);
void v5_state_publisher_reset_stop(void);
int v5_state_publisher_build_frame(V5StatusShmFrame *frame, V5StatePublisherReport *report);
int v5_state_publisher_publish_once(const char *path, V5StatePublisherReport *report);
int v5_state_publisher_run_loop(const char *path, unsigned int interval_ms, unsigned int max_frames, V5StatePublisherReport *report);

#ifdef __cplusplus
}
#endif

#endif
