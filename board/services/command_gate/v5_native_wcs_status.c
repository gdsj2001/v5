#include "v5_native_wcs_status.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_WCS_MAGIC 0x56574353u
#define V5_WCS_VERSION 2u

typedef struct V5NativeWcsStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    int32_t wcs_index;
    uint32_t wcs_count;
    uint32_t axis_count;
    uint32_t table_valid;
    uint32_t wcs_offsets_epoch;
    uint32_t reserved0;
    uint64_t monotonic_ns;
    double offsets[V5_NATIVE_WCS_STATUS_WCS_COUNT][V5_NATIVE_WCS_STATUS_AXIS_COUNT];
    uint32_t crc32;
    uint32_t reserved2;
} V5NativeWcsStatusBlock;

static const char *wcs_status_path(const char *path)
{
    return (path && path[0]) ? path : V5_NATIVE_WCS_STATUS_DEFAULT_PATH;
}

static uint64_t wcs_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t wcs_crc32_like(const V5NativeWcsStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    size_t limit = offsetof(V5NativeWcsStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

int v5_native_wcs_status_from_g5x(int g5x_index, int *wcs_index_out)
{
    if (wcs_index_out) {
        *wcs_index_out = -1;
    }
    if (g5x_index < 1 || g5x_index > 9) {
        return 0;
    }
    if (wcs_index_out) {
        *wcs_index_out = g5x_index - 1;
    }
    return 1;
}

static int wcs_block_values_finite(const V5NativeWcsStatusBlock *block)
{
    size_t w;
    size_t a;
    if (!block) {
        return 0;
    }
    for (w = 0U; w < V5_NATIVE_WCS_STATUS_WCS_COUNT; ++w) {
        for (a = 0U; a < V5_NATIVE_WCS_STATUS_AXIS_COUNT; ++a) {
            if (!isfinite(block->offsets[w][a])) {
                return 0;
            }
        }
    }
    return 1;
}

static int wcs_block_valid(const V5NativeWcsStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    if (!block || block->magic != V5_WCS_MAGIC || block->version != V5_WCS_VERSION ||
        block->size != (uint32_t)sizeof(*block) || block->crc32 != wcs_crc32_like(block) ||
        !block->valid || !block->table_valid || block->wcs_offsets_epoch == 0U ||
        block->wcs_count != V5_NATIVE_WCS_STATUS_WCS_COUNT ||
        block->axis_count != V5_NATIVE_WCS_STATUS_AXIS_COUNT ||
        block->wcs_index < 0 || block->wcs_index >= (int32_t)V5_NATIVE_WCS_STATUS_WCS_COUNT ||
        !wcs_block_values_finite(block)) {
        return 0;
    }
    now = wcs_monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(max_age_ms ? max_age_ms : V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

int v5_native_wcs_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback)
{
    FILE *fp;
    V5NativeWcsStatusBlock block;
    const char *actual_path;
    if (!readback) {
        return 0;
    }
    actual_path = wcs_status_path(path);
    fp = fopen(actual_path, "rb");
    if (!fp) {
        v5_native_readback_set_unavailable(readback, "wcs_status_block_missing");
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        v5_native_readback_set_unavailable(readback, "wcs_status_block_short_read");
        return 0;
    }
    fclose(fp);
    if (!wcs_block_valid(&block, max_age_ms)) {
        v5_native_readback_set_unavailable(readback, "wcs_status_block_invalid_or_stale");
        return 0;
    }
    v5_native_readback_init(readback);
    v5_native_readback_set_wcs_table(
        readback,
        (int)block.wcs_index,
        &block.offsets[0][0],
        block.wcs_count,
        block.axis_count,
        block.wcs_offsets_epoch);
    return v5_native_readback_wcs_table_known(readback);
}

static int wcs_copy_table_values(
    V5NativeWcsStatusBlock *block,
    const double *offsets,
    size_t wcs_count,
    size_t axis_count)
{
    size_t w;
    size_t a;
    if (!block || !offsets || wcs_count != V5_NATIVE_WCS_STATUS_WCS_COUNT ||
        axis_count != V5_NATIVE_WCS_STATUS_AXIS_COUNT) {
        return 0;
    }
    for (w = 0U; w < V5_NATIVE_WCS_STATUS_WCS_COUNT; ++w) {
        for (a = 0U; a < V5_NATIVE_WCS_STATUS_AXIS_COUNT; ++a) {
            double value = offsets[(w * V5_NATIVE_WCS_STATUS_AXIS_COUNT) + a];
            if (!isfinite(value)) {
                return 0;
            }
            block->offsets[w][a] = value;
        }
    }
    return 1;
}

static int wcs_write_block(const char *path, const V5NativeWcsStatusBlock *block)
{
    FILE *fp;
    const char *actual_path = wcs_status_path(path);
    fp = fopen(actual_path, "wb");
    if (!fp) {
        return 0;
    }
    if (fwrite(block, 1U, sizeof(*block), fp) != sizeof(*block)) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

int v5_native_wcs_status_write_table(
    const char *path,
    int valid,
    int wcs_index,
    const double *offsets,
    size_t wcs_count,
    size_t axis_count,
    unsigned int epoch)
{
    V5NativeWcsStatusBlock block;
    int table_ok;

    memset(&block, 0, sizeof(block));
    block.magic = V5_WCS_MAGIC;
    block.version = V5_WCS_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.wcs_count = V5_NATIVE_WCS_STATUS_WCS_COUNT;
    block.axis_count = V5_NATIVE_WCS_STATUS_AXIS_COUNT;
    table_ok = wcs_copy_table_values(&block, offsets, wcs_count, axis_count);
    block.valid = (valid && table_ok && wcs_index >= 0 &&
                   wcs_index < (int)V5_NATIVE_WCS_STATUS_WCS_COUNT) ? 1U : 0U;
    block.wcs_index = block.valid ? (int32_t)wcs_index : -1;
    block.table_valid = block.valid ? 1U : 0U;
    block.wcs_offsets_epoch = block.valid ? (epoch ? epoch : 1U) : 0U;
    block.monotonic_ns = wcs_monotonic_ns();
    block.crc32 = wcs_crc32_like(&block);
    return wcs_write_block(path, &block);
}

int v5_native_wcs_status_write(
    const char *path,
    int valid,
    int wcs_index,
    const double *offsets,
    size_t offset_count)
{
    double table[V5_NATIVE_WCS_STATUS_WCS_COUNT][V5_NATIVE_WCS_STATUS_AXIS_COUNT];
    size_t i;

    memset(table, 0, sizeof(table));
    if (wcs_index >= 0 && wcs_index < (int)V5_NATIVE_WCS_STATUS_WCS_COUNT && offsets) {
        for (i = 0U; i < offset_count && i < V5_NATIVE_WCS_STATUS_AXIS_COUNT; ++i) {
            table[wcs_index][i] = offsets[i];
        }
    }
    return v5_native_wcs_status_write_table(
        path,
        valid,
        wcs_index,
        &table[0][0],
        V5_NATIVE_WCS_STATUS_WCS_COUNT,
        V5_NATIVE_WCS_STATUS_AXIS_COUNT,
        1U);
}
