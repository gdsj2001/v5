#ifndef V5_STATUS_SHM_MMAP_H
#define V5_STATUS_SHM_MMAP_H

#include "v5_status_shm.h"

#include <stddef.h>
#include <stdint.h>

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

#define V5_STATUS_SHM_MMAP_PATH_CAP 256U

typedef struct V5StatusShmMmapWriter {
    int fd;
    void *page;
    size_t frame_size;
    char path[V5_STATUS_SHM_MMAP_PATH_CAP];
    unsigned int open_count;
} V5StatusShmMmapWriter;

typedef struct V5StatusShmMmapReader {
    int fd;
    const void *page;
    size_t frame_size;
    char path[V5_STATUS_SHM_MMAP_PATH_CAP];
    unsigned int open_count;
    uint64_t device_id;
    uint64_t inode_id;
} V5StatusShmMmapReader;

void v5_status_shm_mmap_writer_init(V5StatusShmMmapWriter *writer);
int v5_status_shm_mmap_writer_open(V5StatusShmMmapWriter *writer, const char *path);
int v5_status_shm_mmap_writer_publish(
    V5StatusShmMmapWriter *writer,
    const V5StatusShmFrame *frame,
    V5StatusShmMmapPublishReport *publish_report);
void v5_status_shm_mmap_writer_close(V5StatusShmMmapWriter *writer);

void v5_status_shm_mmap_reader_init(V5StatusShmMmapReader *reader);
int v5_status_shm_mmap_reader_open(V5StatusShmMmapReader *reader, const char *path);
int v5_status_shm_mmap_reader_read(V5StatusShmMmapReader *reader, V5StatusShmFrame *frame);
int v5_status_shm_mmap_reader_backing_matches(const V5StatusShmMmapReader *reader);
void v5_status_shm_mmap_reader_close(V5StatusShmMmapReader *reader);

int v5_status_shm_publish_to_path(const char *path, const V5StatusShmFrame *frame, V5StatusShmMmapPublishReport *publish_report);
int v5_status_shm_read_from_path(const char *path, V5StatusShmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
