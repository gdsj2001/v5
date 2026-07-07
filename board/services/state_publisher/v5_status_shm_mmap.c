#include "v5_status_shm_mmap.h"

#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *v5_status_shm_resolve_path(const char *path)
{
    return (path && path[0]) ? path : V5_STATUS_SHM_PATH;
}

int v5_status_shm_publish_to_path(const char *path, const V5StatusShmFrame *frame, V5StatusShmMmapPublishReport *publish_report)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    size_t frame_size = sizeof(V5StatusShmFrame);
    void *page = MAP_FAILED;
    int fd = -1;
    int ok = 0;
    unsigned int published_seq = 0U;

    if (publish_report) {
        memset(publish_report, 0, sizeof(*publish_report));
        publish_report->path = resolved;
    }
    if (!frame || frame->magic != V5_STATUS_SHM_MAGIC || frame->version != V5_STATUS_SHM_VERSION) {
        return 0;
    }
    if (frame->total_size != frame_size) {
        return 0;
    }

    fd = open(resolved, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return 0;
    }
    if (ftruncate(fd, (off_t)frame_size) != 0) {
        close(fd);
        return 0;
    }

    page = mmap(0, frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (page != MAP_FAILED) {
        ok = v5_status_shm_publish_to_memory(page, frame_size, frame);
        if (ok) {
            published_seq = ((const V5StatusShmFrame *)page)->seq;
            msync(page, frame_size, MS_ASYNC);
        }
        munmap(page, frame_size);
    }
    close(fd);

    if (publish_report) {
        publish_report->ok = ok;
        publish_report->seq = published_seq;
        publish_report->valid_mask = frame->typed_valid_mask;
        publish_report->flags = frame->flags;
    }
    return ok;
}

int v5_status_shm_read_from_path(const char *path, V5StatusShmFrame *frame)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    size_t frame_size = sizeof(V5StatusShmFrame);
    void *page = MAP_FAILED;
    int fd = -1;
    int ok = 0;

    if (!frame) {
        return 0;
    }

    fd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    page = mmap(0, frame_size, PROT_READ, MAP_SHARED, fd, 0);
    if (page != MAP_FAILED) {
        ok = v5_status_shm_read_from_memory(frame, page, frame_size);
        munmap(page, frame_size);
    }
    close(fd);
    return ok;
}
