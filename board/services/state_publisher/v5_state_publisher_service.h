#ifndef V5_STATE_PUBLISHER_SERVICE_H
#define V5_STATE_PUBLISHER_SERVICE_H

#include "v5_status_shm.h"
#include "v5_status_shm_mmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define V5_STATE_PUBLISHER_INTERVAL_MS 30u

typedef struct V5StatePublisherReport {
    int sample_available;
    unsigned int valid_mask;
    unsigned int frame_flags;
    unsigned int publish_count;
    unsigned int rtcp_status_published;
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
