#include "v5_native_rtcp_status.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_RTCP_MAGIC 0x56525443u
#define V5_RTCP_VERSION 1u

typedef struct V5NativeRtcpStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t active;
    uint32_t reserved;
    uint64_t monotonic_ns;
    uint32_t crc32;
    uint32_t reserved2;
} V5NativeRtcpStatusBlock;

static const char *rtcp_status_path(const char *path)
{
    return (path && path[0]) ? path : V5_NATIVE_RTCP_STATUS_DEFAULT_PATH;
}

static uint64_t rtcp_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t rtcp_crc32_like(const V5NativeRtcpStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    size_t limit = offsetof(V5NativeRtcpStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int rtcp_block_valid(const V5NativeRtcpStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    if (!block || block->magic != V5_RTCP_MAGIC || block->version != V5_RTCP_VERSION ||
        block->size != (uint32_t)sizeof(*block) || block->crc32 != rtcp_crc32_like(block) || !block->valid) {
        return 0;
    }
    now = rtcp_monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(max_age_ms ? max_age_ms : V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

int v5_native_rtcp_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback)
{
    FILE *fp;
    V5NativeRtcpStatusBlock block;
    const char *actual_path;
    if (!readback) {
        return 0;
    }
    actual_path = rtcp_status_path(path);
    fp = fopen(actual_path, "rb");
    if (!fp) {
        v5_native_readback_set_unavailable(readback, "rtcp_status_block_missing");
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        v5_native_readback_set_unavailable(readback, "rtcp_status_block_short_read");
        return 0;
    }
    fclose(fp);
    if (!rtcp_block_valid(&block, max_age_ms)) {
        v5_native_readback_set_unavailable(readback, "rtcp_status_block_invalid_or_stale");
        return 0;
    }
    v5_native_readback_init(readback);
    v5_native_readback_set_rtcp_actual(readback, block.active ? 1 : 0);
    return 1;
}

int v5_native_rtcp_status_from_mcodes(const int *mcodes, size_t count, int *valid_out, int *active_out)
{
    size_t i;
    int saw_m128 = 0;
    int saw_m129 = 0;

    if (valid_out) {
        *valid_out = 0;
    }
    if (active_out) {
        *active_out = 0;
    }
    if (!mcodes || count == 0U) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (mcodes[i] == 128) {
            saw_m128 = 1;
        } else if (mcodes[i] == 129) {
            saw_m129 = 1;
        }
    }
    if (saw_m128 == saw_m129) {
        return 0;
    }
    if (valid_out) {
        *valid_out = 1;
    }
    if (active_out) {
        *active_out = saw_m128 ? 1 : 0;
    }
    return 1;
}

int v5_native_rtcp_status_write_from_mcodes(const char *path, const int *mcodes, size_t count)
{
    int valid = 0;
    int active = 0;
    (void)v5_native_rtcp_status_from_mcodes(mcodes, count, &valid, &active);
    return v5_native_rtcp_status_write(path, valid, active);
}

int v5_native_rtcp_status_write(const char *path, int valid, int active)
{
    FILE *fp;
    V5NativeRtcpStatusBlock block;
    const char *actual_path = rtcp_status_path(path);
    memset(&block, 0, sizeof(block));
    block.magic = V5_RTCP_MAGIC;
    block.version = V5_RTCP_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.valid = valid ? 1U : 0U;
    block.active = active ? 1U : 0U;
    block.monotonic_ns = rtcp_monotonic_ns();
    block.crc32 = rtcp_crc32_like(&block);
    fp = fopen(actual_path, "wb");
    if (!fp) {
        return 0;
    }
    if (fwrite(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}
