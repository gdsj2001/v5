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

static int v5_status_shm_store_path(char *target, size_t capacity, const char *path)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    size_t length = strlen(resolved);
    if (!target || capacity == 0U || length >= capacity) {
        return 0;
    }
    memcpy(target, resolved, length + 1U);
    return 1;
}

void v5_status_shm_mmap_writer_init(V5StatusShmMmapWriter *writer)
{
    if (!writer) {
        return;
    }
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
}

void v5_status_shm_mmap_writer_close(V5StatusShmMmapWriter *writer)
{
    if (!writer) {
        return;
    }
    if (writer->page) {
        munmap(writer->page, writer->frame_size);
    }
    if (writer->fd >= 0) {
        close(writer->fd);
    }
    writer->fd = -1;
    writer->page = 0;
    writer->frame_size = 0U;
    writer->path[0] = '\0';
}

int v5_status_shm_mmap_writer_open(V5StatusShmMmapWriter *writer, const char *path)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    const size_t frame_size = sizeof(V5StatusShmFrame);
    int fd;
    void *page;

    if (!writer) {
        return 0;
    }
    if (writer->fd >= 0 && writer->page && strcmp(writer->path, resolved) == 0) {
        return 1;
    }
    v5_status_shm_mmap_writer_close(writer);
    if (!v5_status_shm_store_path(writer->path, sizeof(writer->path), resolved)) {
        return 0;
    }
    fd = open(resolved, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        writer->path[0] = '\0';
        return 0;
    }
    if (ftruncate(fd, (off_t)frame_size) != 0) {
        close(fd);
        writer->path[0] = '\0';
        return 0;
    }
    page = mmap(0, frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (page == MAP_FAILED) {
        close(fd);
        writer->path[0] = '\0';
        return 0;
    }
    writer->fd = fd;
    writer->page = page;
    writer->frame_size = frame_size;
    writer->open_count += 1U;
    return 1;
}

int v5_status_shm_mmap_writer_publish(
    V5StatusShmMmapWriter *writer,
    const V5StatusShmFrame *frame,
    V5StatusShmMmapPublishReport *publish_report)
{
    int ok = 0;
    unsigned int published_seq = 0U;

    if (publish_report) {
        memset(publish_report, 0, sizeof(*publish_report));
        publish_report->path = writer ? writer->path : 0;
    }
    if (!writer || writer->fd < 0 || !writer->page ||
        !frame || frame->magic != V5_STATUS_SHM_MAGIC ||
        frame->version != V5_STATUS_SHM_VERSION) {
        return 0;
    }
    if (frame->total_size != writer->frame_size) {
        return 0;
    }
    ok = v5_status_shm_publish_to_memory(writer->page, writer->frame_size, frame);
    if (ok) {
        published_seq = ((const V5StatusShmFrame *)writer->page)->seq;
    }
    if (publish_report) {
        publish_report->ok = ok;
        publish_report->seq = published_seq;
        publish_report->valid_mask = frame->typed_valid_mask;
        publish_report->flags = frame->flags;
    }
    return ok;
}

void v5_status_shm_mmap_reader_init(V5StatusShmMmapReader *reader)
{
    if (!reader) {
        return;
    }
    memset(reader, 0, sizeof(*reader));
    reader->fd = -1;
}

void v5_status_shm_mmap_reader_close(V5StatusShmMmapReader *reader)
{
    if (!reader) {
        return;
    }
    if (reader->page) {
        munmap((void *)reader->page, reader->frame_size);
    }
    if (reader->fd >= 0) {
        close(reader->fd);
    }
    reader->fd = -1;
    reader->page = 0;
    reader->frame_size = 0U;
    reader->path[0] = '\0';
    reader->device_id = 0U;
    reader->inode_id = 0U;
}

int v5_status_shm_mmap_reader_open(V5StatusShmMmapReader *reader, const char *path)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    const size_t frame_size = sizeof(V5StatusShmFrame);
    struct stat status;
    int fd;
    void *page;

    if (!reader) {
        return 0;
    }
    if (reader->fd >= 0 && reader->page && strcmp(reader->path, resolved) == 0) {
        return 1;
    }
    v5_status_shm_mmap_reader_close(reader);
    if (!v5_status_shm_store_path(reader->path, sizeof(reader->path), resolved)) {
        return 0;
    }
    fd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        reader->path[0] = '\0';
        return 0;
    }
    if (fstat(fd, &status) != 0 || status.st_size < (off_t)frame_size) {
        close(fd);
        reader->path[0] = '\0';
        return 0;
    }
    page = mmap(0, frame_size, PROT_READ, MAP_SHARED, fd, 0);
    if (page == MAP_FAILED) {
        close(fd);
        reader->path[0] = '\0';
        return 0;
    }
    reader->fd = fd;
    reader->page = page;
    reader->frame_size = frame_size;
    reader->device_id = (uint64_t)status.st_dev;
    reader->inode_id = (uint64_t)status.st_ino;
    reader->open_count += 1U;
    return 1;
}

int v5_status_shm_mmap_reader_read(V5StatusShmMmapReader *reader, V5StatusShmFrame *frame)
{
    if (!reader || reader->fd < 0 || !reader->page || !frame) {
        return 0;
    }
    return v5_status_shm_read_from_memory(frame, reader->page, reader->frame_size);
}

int v5_status_shm_mmap_reader_backing_matches(const V5StatusShmMmapReader *reader)
{
    struct stat descriptor_status;
    struct stat path_status;

    if (!reader || reader->fd < 0 || !reader->page || !reader->path[0] ||
        reader->frame_size != sizeof(V5StatusShmFrame) ||
        fstat(reader->fd, &descriptor_status) != 0 ||
        stat(reader->path, &path_status) != 0) {
        return 0;
    }
    if (descriptor_status.st_size < (off_t)reader->frame_size ||
        path_status.st_size < (off_t)reader->frame_size) {
        return 0;
    }
    return (uint64_t)descriptor_status.st_dev == reader->device_id &&
        (uint64_t)descriptor_status.st_ino == reader->inode_id &&
        descriptor_status.st_dev == path_status.st_dev &&
        descriptor_status.st_ino == path_status.st_ino;
}

int v5_status_shm_publish_to_path(
    const char *path,
    const V5StatusShmFrame *frame,
    V5StatusShmMmapPublishReport *publish_report)
{
    const char *resolved = v5_status_shm_resolve_path(path);
    V5StatusShmMmapWriter writer;
    int ok;

    v5_status_shm_mmap_writer_init(&writer);
    if (!v5_status_shm_mmap_writer_open(&writer, path)) {
        if (publish_report) {
            memset(publish_report, 0, sizeof(*publish_report));
            publish_report->path = v5_status_shm_resolve_path(path);
        }
        return 0;
    }
    ok = v5_status_shm_mmap_writer_publish(&writer, frame, publish_report);
    v5_status_shm_mmap_writer_close(&writer);
    if (publish_report) {
        publish_report->path = resolved;
    }
    return ok;
}

int v5_status_shm_read_from_path(const char *path, V5StatusShmFrame *frame)
{
    V5StatusShmMmapReader reader;
    int ok;

    v5_status_shm_mmap_reader_init(&reader);
    if (!v5_status_shm_mmap_reader_open(&reader, path)) {
        return 0;
    }
    ok = v5_status_shm_mmap_reader_read(&reader, frame);
    v5_status_shm_mmap_reader_close(&reader);
    return ok;
}
