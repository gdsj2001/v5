#define _POSIX_C_SOURCE 200809L

#include "v5_remote_ui_native_packer.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct V5RemoteUiPacker {
    const uint8_t *frame;
    size_t frame_size;
};

static int fail_with(int error_number)
{
    errno = error_number;
    return -error_number;
}

uint32_t v5_remote_ui_packer_abi_version(void)
{
    return V5_REMOTE_UI_PACKER_ABI_VERSION;
}

V5RemoteUiPacker *v5_remote_ui_packer_open(int framebuffer_fd, size_t frame_size)
{
    struct stat status;
    V5RemoteUiPacker *packer;
    void *mapped;

    if (framebuffer_fd < 0 || frame_size == 0u) {
        errno = EINVAL;
        return NULL;
    }
    if (fstat(framebuffer_fd, &status) != 0)
        return NULL;
    if (status.st_size < 0 || (uint64_t)status.st_size < (uint64_t)frame_size) {
        errno = EINVAL;
        return NULL;
    }
    mapped = mmap(NULL, frame_size, PROT_READ, MAP_SHARED, framebuffer_fd, 0);
    if (mapped == MAP_FAILED)
        return NULL;
    packer = (V5RemoteUiPacker *)calloc(1u, sizeof(*packer));
    if (packer == NULL) {
        int saved_errno = errno;
        (void)munmap(mapped, frame_size);
        errno = saved_errno;
        return NULL;
    }
    packer->frame = (const uint8_t *)mapped;
    packer->frame_size = frame_size;
    return packer;
}

void v5_remote_ui_packer_close(V5RemoteUiPacker *packer)
{
    if (packer == NULL)
        return;
    if (packer->frame != NULL && packer->frame_size > 0u)
        (void)munmap((void *)packer->frame, packer->frame_size);
    free(packer);
}

int v5_remote_ui_packer_pack(
    V5RemoteUiPacker *packer,
    uint32_t width,
    uint32_t height,
    size_t stride,
    const V5RemoteUiPackRect *rects,
    size_t rect_count,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    size_t total = 0u;
    size_t cursor = 0u;
    size_t index;

    if (output_size == NULL)
        return fail_with(EINVAL);
    *output_size = 0u;
    if (packer == NULL || packer->frame == NULL || width == 0u || height == 0u)
        return fail_with(EINVAL);
#if SIZE_MAX == UINT32_MAX
    if (width > SIZE_MAX / 4u)
        return fail_with(EOVERFLOW);
#endif
    if (stride < (size_t)width * 4u)
        return fail_with(EOVERFLOW);
    if ((size_t)height > SIZE_MAX / stride || (size_t)height * stride > packer->frame_size)
        return fail_with(EOVERFLOW);
    if (rect_count > 0u && rects == NULL)
        return fail_with(EINVAL);

    for (index = 0u; index < rect_count; ++index) {
        const V5RemoteUiPackRect *rect = &rects[index];
        size_t pixels;
        size_t bytes;

        if (rect->w == 0u || rect->h == 0u || rect->x >= width || rect->y >= height ||
            rect->w > width - rect->x || rect->h > height - rect->y)
            return fail_with(EINVAL);
        if ((size_t)rect->w > SIZE_MAX / (size_t)rect->h)
            return fail_with(EOVERFLOW);
        pixels = (size_t)rect->w * (size_t)rect->h;
        if (pixels > SIZE_MAX / 4u)
            return fail_with(EOVERFLOW);
        bytes = pixels * 4u;
        if (total > SIZE_MAX - bytes)
            return fail_with(EOVERFLOW);
        total += bytes;
    }
    if (total > output_capacity || (total > 0u && output == NULL))
        return fail_with(ENOSPC);

    for (index = 0u; index < rect_count; ++index) {
        const V5RemoteUiPackRect *rect = &rects[index];
        const size_t row_bytes = (size_t)rect->w * 4u;
        size_t row;

        for (row = 0u; row < (size_t)rect->h; ++row) {
            const size_t source_offset = ((size_t)rect->y + row) * stride + (size_t)rect->x * 4u;
            if (source_offset > packer->frame_size || row_bytes > packer->frame_size - source_offset)
                return fail_with(EOVERFLOW);
            memcpy(output + cursor, packer->frame + source_offset, row_bytes);
            cursor += row_bytes;
        }
    }
    *output_size = cursor;
    return 0;
}
