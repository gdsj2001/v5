#include "v5_bus_status_reader.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define V5_BUS_STATUS_MAGIC 0x56425553U
#define V5_BUS_STATUS_VERSION 1U

typedef struct V5BusJointStatusBlock {
    uint32_t valid;
    uint32_t axis_code;
    uint32_t slave_position;
    uint32_t flags;
    uint32_t statusword;
} V5BusJointStatusBlock;

#pragma pack(push, 1)
typedef struct V5BusStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t sequence;
    uint32_t writer_identity;
    uint32_t mapping_generation;
    uint32_t active_mask;
    uint32_t master_flags;
    uint32_t slaves_responding;
    uint32_t joint_count;
    uint32_t source_generation;
    uint64_t monotonic_ns;
    V5BusJointStatusBlock joints[V5_BUS_STATUS_JOINT_COUNT];
    uint32_t crc32;
    uint32_t reserved;
} V5BusStatusBlock;
#pragma pack(pop)

static uint64_t bus_monotonic_ns(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64() * 1000000ULL;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0ULL;
    }
    return ((uint64_t)now.tv_sec * 1000000000ULL) + (uint64_t)now.tv_nsec;
#endif
}

static uint32_t bus_crc32_like(const V5BusStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    const size_t limit = offsetof(V5BusStatusBlock, crc32);
    uint32_t hash = 2166136261U;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619U;
    }
    return hash;
}

static int bus_axis_valid(uint32_t axis_code)
{
    return axis_code == (uint32_t)'X' || axis_code == (uint32_t)'Y' ||
        axis_code == (uint32_t)'Z' || axis_code == (uint32_t)'A' ||
        axis_code == (uint32_t)'B' || axis_code == (uint32_t)'C';
}

static int bus_block_valid(
    const V5BusStatusBlock *block,
    unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    uint32_t active_limit = (1U << V5_BUS_STATUS_JOINT_COUNT) - 1U;
    uint32_t seen_axes = 0U;
    uint32_t seen_slaves = 0U;
    size_t joint;

    if (!block || block->magic != V5_BUS_STATUS_MAGIC ||
        block->version != V5_BUS_STATUS_VERSION ||
        block->size != (uint32_t)sizeof(*block) || !block->valid ||
        !block->sequence || (block->sequence & 1U) ||
        !block->writer_identity || !block->mapping_generation ||
        !block->source_generation ||
        block->joint_count != V5_BUS_STATUS_JOINT_COUNT ||
        !block->active_mask || (block->active_mask & ~active_limit) ||
        block->crc32 != bus_crc32_like(block)) {
        return 0;
    }
    for (joint = 0U; joint < V5_BUS_STATUS_JOINT_COUNT; ++joint) {
        const V5BusJointStatusBlock *entry = &block->joints[joint];
        uint32_t active = block->active_mask & (1U << joint);
        uint32_t axis_bit;
        uint32_t slave_bit;
        if (!active) {
            if (entry->valid) {
                return 0;
            }
            continue;
        }
        if (!entry->valid || !bus_axis_valid(entry->axis_code) ||
            entry->slave_position >= V5_BUS_STATUS_JOINT_COUNT ||
            entry->statusword > 0xffffU ||
            (entry->flags & ~V5_BUS_JOINT_SLAVE_OP)) {
            return 0;
        }
        axis_bit = 1U << (entry->axis_code - (uint32_t)'A');
        slave_bit = 1U << entry->slave_position;
        if ((seen_axes & axis_bit) || (seen_slaves & slave_bit)) {
            return 0;
        }
        seen_axes |= axis_bit;
        seen_slaves |= slave_bit;
    }
    now = bus_monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(
        max_age_ms ? max_age_ms : V5_BUS_STATUS_DEFAULT_MAX_AGE_MS
    ) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

void v5_bus_status_init(V5BusStatus *status)
{
    if (status) {
        memset(status, 0, sizeof(*status));
    }
}

int v5_bus_status_read(
    const char *path,
    unsigned int max_age_ms,
    V5BusStatus *status)
{
    const char *actual_path =
        (path && path[0]) ? path : V5_BUS_STATUS_DEFAULT_PATH;
    V5BusStatusBlock block;
    FILE *stream;
    size_t joint;
    int attempt;

    if (!status) {
        return 0;
    }
    v5_bus_status_init(status);
    for (attempt = 0; attempt < 3; ++attempt) {
        stream = fopen(actual_path, "rb");
        if (!stream) {
            return 0;
        }
        memset(&block, 0, sizeof(block));
        if (fread(&block, 1U, sizeof(block), stream) != sizeof(block)) {
            fclose(stream);
            return 0;
        }
        fclose(stream);
        if (!(block.sequence & 1U)) {
            break;
        }
    }
    if (!bus_block_valid(&block, max_age_ms)) {
        return 0;
    }

    status->valid = 1;
    status->writer_identity = block.writer_identity;
    status->mapping_generation = block.mapping_generation;
    status->active_mask = block.active_mask;
    status->master_flags = block.master_flags;
    status->slaves_responding = block.slaves_responding;
    status->source_generation = block.source_generation;
    status->monotonic_ns = block.monotonic_ns;
    for (joint = 0U; joint < V5_BUS_STATUS_JOINT_COUNT; ++joint) {
        const V5BusJointStatusBlock *source = &block.joints[joint];
        V5BusJointStatus *target = &status->joints[joint];
        if (!source->valid) {
            continue;
        }
        target->valid = 1;
        target->axis = (char)source->axis_code;
        target->slave_position = source->slave_position;
        target->flags = source->flags;
        target->statusword = source->statusword;
        status->active_count += 1U;
    }
    return 1;
}
