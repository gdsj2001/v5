#include "v5_native_g53_geometry_status.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_G53_GEOMETRY_MAGIC 0x56354753u
#define V5_G53_GEOMETRY_VERSION 1u

typedef struct V5NativeG53GeometryStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t center_count;
    uint32_t axis_count;
    uint32_t epoch;
    uint32_t reserved0;
    uint64_t monotonic_ns;
    double centers[V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT][V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT];
    uint32_t crc32;
    uint32_t reserved1;
} V5NativeG53GeometryStatusBlock;

static const char *g53_geometry_status_path(const char *path)
{
    return (path && path[0]) ? path : V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_PATH;
}

static uint64_t g53_geometry_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t g53_geometry_crc32_like(const V5NativeG53GeometryStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    size_t limit = offsetof(V5NativeG53GeometryStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int g53_geometry_values_finite(const V5NativeG53GeometryStatusBlock *block)
{
    size_t c;
    size_t a;
    if (!block) {
        return 0;
    }
    for (c = 0U; c < V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT; ++c) {
        for (a = 0U; a < V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT; ++a) {
            if (!isfinite(block->centers[c][a])) {
                return 0;
            }
        }
    }
    return 1;
}

static int g53_geometry_block_valid(const V5NativeG53GeometryStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    if (!block || block->magic != V5_G53_GEOMETRY_MAGIC || block->version != V5_G53_GEOMETRY_VERSION ||
        block->size != (uint32_t)sizeof(*block) || block->crc32 != g53_geometry_crc32_like(block) ||
        !block->valid || block->center_count != V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT ||
        block->axis_count != V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT || block->epoch == 0U ||
        !g53_geometry_values_finite(block)) {
        return 0;
    }
    now = g53_geometry_monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(max_age_ms ? max_age_ms : V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

int v5_native_g53_geometry_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback)
{
    FILE *fp;
    V5NativeG53GeometryStatusBlock block;
    const char *actual_path;
    if (!readback) {
        return 0;
    }
    actual_path = g53_geometry_status_path(path);
    fp = fopen(actual_path, "rb");
    if (!fp) {
        v5_native_readback_set_unavailable(readback, "g53_geometry_status_block_missing");
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        v5_native_readback_set_unavailable(readback, "g53_geometry_status_block_short_read");
        return 0;
    }
    fclose(fp);
    if (!g53_geometry_block_valid(&block, max_age_ms)) {
        v5_native_readback_set_unavailable(readback, "g53_geometry_status_block_invalid_or_stale");
        return 0;
    }
    v5_native_readback_init(readback);
    v5_native_readback_set_g53_geometry(
        readback,
        &block.centers[0][0],
        block.center_count,
        block.axis_count,
        block.epoch);
    return v5_native_readback_g53_geometry_known(readback);
}

static int g53_geometry_copy_values(V5NativeG53GeometryStatusBlock *block, const double *centers, size_t center_count, size_t axis_count)
{
    size_t c;
    size_t a;
    if (!block || !centers || center_count != V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT ||
        axis_count != V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT) {
        return 0;
    }
    for (c = 0U; c < V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT; ++c) {
        for (a = 0U; a < V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT; ++a) {
            double value = centers[(c * V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT) + a];
            if (!isfinite(value)) {
                return 0;
            }
            block->centers[c][a] = value;
        }
    }
    return 1;
}

static int g53_geometry_write_block(const char *path, const V5NativeG53GeometryStatusBlock *block)
{
    FILE *fp;
    const char *actual_path = g53_geometry_status_path(path);
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

int v5_native_g53_geometry_status_write(
    const char *path,
    int valid,
    const double *centers,
    size_t center_count,
    size_t axis_count,
    unsigned int epoch)
{
    V5NativeG53GeometryStatusBlock block;
    int values_ok;
    memset(&block, 0, sizeof(block));
    block.magic = V5_G53_GEOMETRY_MAGIC;
    block.version = V5_G53_GEOMETRY_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.center_count = V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT;
    block.axis_count = V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT;
    values_ok = g53_geometry_copy_values(&block, centers, center_count, axis_count);
    block.valid = (valid && values_ok) ? 1U : 0U;
    block.epoch = block.valid ? (epoch ? epoch : 1U) : 0U;
    block.monotonic_ns = g53_geometry_monotonic_ns();
    block.crc32 = g53_geometry_crc32_like(&block);
    return g53_geometry_write_block(path, &block);
}
