#include "v5_status_shm.h"

#include <string.h>

void v5_status_shm_frame_init(V5StatusShmFrame *frame)
{
    if (!frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->magic = V5_STATUS_SHM_MAGIC;
    frame->version = V5_STATUS_SHM_VERSION;
    frame->header_size = (uint32_t)sizeof(V5StatusShmFrame);
    frame->total_size = (uint32_t)sizeof(V5StatusShmFrame);
    frame->payload_size = (uint32_t)(sizeof(V5StatusShmFrame) - sizeof(uint32_t) * 8U);
}

int v5_status_shm_frame_copy(V5StatusShmFrame *dst, const V5StatusShmFrame *src)
{
    if (!dst || !src) {
        return 0;
    }
    if (src->magic != V5_STATUS_SHM_MAGIC || src->version != V5_STATUS_SHM_VERSION) {
        return 0;
    }

    memcpy(dst, src, sizeof(*dst));
    return 1;
}

int v5_status_shm_publish_to_memory(void *dst, size_t dst_size, const V5StatusShmFrame *frame)
{
    V5StatusShmFrame *page;

    if (!dst || !frame || dst_size < sizeof(*frame)) {
        return 0;
    }
    if (frame->magic != V5_STATUS_SHM_MAGIC || frame->version != V5_STATUS_SHM_VERSION) {
        return 0;
    }
    if (frame->total_size != sizeof(*frame)) {
        return 0;
    }

    page = (V5StatusShmFrame *)dst;
    memcpy(page, frame, sizeof(*page));
    return 1;
}

int v5_status_shm_read_from_memory(V5StatusShmFrame *dst, const void *src, size_t src_size)
{
    const V5StatusShmFrame *page;

    if (!dst || !src || src_size < sizeof(*dst)) {
        return 0;
    }

    page = (const V5StatusShmFrame *)src;
    return v5_status_shm_frame_copy(dst, page);
}
