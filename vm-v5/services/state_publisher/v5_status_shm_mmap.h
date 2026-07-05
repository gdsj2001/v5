#ifndef V5_STATUS_SHM_MMAP_H
#define V5_STATUS_SHM_MMAP_H

#include "v5_status_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5StatusShmMmapPublishReport {
    int ok;
    const char *path;
    unsigned int seq;
    unsigned int valid_mask;
    unsigned int flags;
} V5StatusShmMmapPublishReport;

int v5_status_shm_publish_to_path(const char *path, const V5StatusShmFrame *frame, V5StatusShmMmapPublishReport *publish_report);
int v5_status_shm_read_from_path(const char *path, V5StatusShmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
