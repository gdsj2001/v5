#include "v5_native_g53_geometry_status.h"
#include "v5_motion_model_registry.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_G53_GEOMETRY_MAGIC 0x56354753u
#define V5_G53_GEOMETRY_VERSION 2u

typedef struct V5NativeG53GeometryStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t center_count;
    uint32_t axis_count;
    uint32_t epoch;
    uint32_t active_field_mask;
    uint64_t monotonic_ns;
    double centers[V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT][V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT];
    char motion_model[V5_NATIVE_G53_GEOMETRY_STATUS_MOTION_MODEL_CAP];
    uint32_t crc32;
    uint32_t reserved1;
} V5NativeG53GeometryStatusBlock;

typedef char V5NativeG53GeometryStatusBlockSizeMustStay152[
    sizeof(V5NativeG53GeometryStatusBlock) == 152U ? 1 : -1];
typedef char V5NativeG53GeometryActiveMaskOffsetMustStay28[
    offsetof(V5NativeG53GeometryStatusBlock, active_field_mask) == 28U ? 1 : -1];
typedef char V5NativeG53GeometryCrcOffsetMustStay144[
    offsetof(V5NativeG53GeometryStatusBlock, crc32) == 144U ? 1 : -1];

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

static unsigned int g53_geometry_center_for_rotary_axis(char axis)
{
    switch (axis) {
    case 'A': return V5_NATIVE_READBACK_G53_CENTER_A;
    case 'B': return V5_NATIVE_READBACK_G53_CENTER_B;
    case 'C': return V5_NATIVE_READBACK_G53_CENTER_C;
    default: return V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT;
    }
}

static uint32_t g53_geometry_field_for(unsigned int center, unsigned int axis)
{
    if (center == V5_NATIVE_READBACK_G53_CENTER_A) {
        if (axis == 1U) return V5_NATIVE_G53_GEOMETRY_FIELD_A_Y;
        if (axis == 2U) return V5_NATIVE_G53_GEOMETRY_FIELD_A_Z;
    } else if (center == V5_NATIVE_READBACK_G53_CENTER_B) {
        if (axis == 0U) return V5_NATIVE_G53_GEOMETRY_FIELD_B_X;
        if (axis == 2U) return V5_NATIVE_G53_GEOMETRY_FIELD_B_Z;
    } else if (center == V5_NATIVE_READBACK_G53_CENTER_C) {
        if (axis == 0U) return V5_NATIVE_G53_GEOMETRY_FIELD_C_X;
        if (axis == 1U) return V5_NATIVE_G53_GEOMETRY_FIELD_C_Y;
    }
    return 0U;
}

static int g53_geometry_add_rotary_fields(
    char rotary_axis,
    unsigned int center,
    unsigned int wcs_component,
    uint32_t *mask)
{
    unsigned int component;
    unsigned int expected_center = g53_geometry_center_for_rotary_axis(rotary_axis);
    unsigned int added = 0U;

    if (!mask || expected_center >= V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT ||
        center != expected_center ||
        wcs_component >= V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT) {
        return 0;
    }
    for (component = 0U; component < V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT; ++component) {
        uint32_t field;
        if (component == wcs_component) {
            continue;
        }
        field = g53_geometry_field_for(center, component);
        if (!field || (*mask & field) != 0U) {
            return 0;
        }
        *mask |= field;
        ++added;
    }
    return added == 2U;
}

static int g53_geometry_expected_field_mask(const char *motion_model, uint32_t *mask_out)
{
    const V5MotionModelDescriptor *model;
    uint32_t mask = 0U;

    if (mask_out) {
        *mask_out = 0U;
    }
    model = v5_motion_model_find(motion_model);
    if (!mask_out || !v5_motion_model_descriptor_valid(model) ||
        strcmp(motion_model, model->canonical) != 0 ||
        !g53_geometry_add_rotary_fields(
            model->first_rotary_axis,
            model->first_g53_center,
            model->first_center_wcs_component,
            &mask) ||
        !g53_geometry_add_rotary_fields(
            model->second_rotary_axis,
            model->second_g53_center,
            model->second_center_wcs_component,
            &mask) ||
        (mask & ~V5_NATIVE_G53_GEOMETRY_FIELD_MASK_ALL) != 0U) {
        return 0;
    }
    *mask_out = mask;
    return 1;
}

static int g53_geometry_motion_model_valid(
    const V5NativeG53GeometryStatusBlock *block,
    uint32_t *expected_field_mask)
{
    if (!block || !block->motion_model[0] ||
        memchr(block->motion_model, '\0', sizeof(block->motion_model)) == 0) {
        return 0;
    }
    return g53_geometry_expected_field_mask(block->motion_model, expected_field_mask);
}

static int g53_geometry_block_valid(const V5NativeG53GeometryStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    uint32_t expected_field_mask = 0U;
    if (!block || block->magic != V5_G53_GEOMETRY_MAGIC || block->version != V5_G53_GEOMETRY_VERSION ||
        block->size != (uint32_t)sizeof(*block) || block->crc32 != g53_geometry_crc32_like(block) ||
        !block->valid || block->center_count != V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT ||
        block->axis_count != V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT || block->epoch == 0U ||
        !g53_geometry_values_finite(block) ||
        !g53_geometry_motion_model_valid(block, &expected_field_mask) ||
        block->active_field_mask != expected_field_mask) {
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
    v5_native_readback_set_motion_model(readback, block.motion_model);
    return v5_native_readback_g53_geometry_known(readback) &&
           v5_native_readback_motion_model_known(readback);
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

static int g53_geometry_copy_motion_model(V5NativeG53GeometryStatusBlock *block, const char *motion_model)
{
    V5NativeReadback readback;
    uint32_t expected_field_mask = 0U;
    if (!block) {
        return 0;
    }
    block->motion_model[0] = '\0';
    v5_native_readback_init(&readback);
    v5_native_readback_set_motion_model(&readback, motion_model);
    if (!v5_native_readback_motion_model_known(&readback) ||
        !g53_geometry_expected_field_mask(readback.motion_model, &expected_field_mask)) {
        return 0;
    }
    snprintf(block->motion_model, sizeof(block->motion_model), "%s", readback.motion_model);
    block->active_field_mask = expected_field_mask;
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
    unsigned int epoch,
    const char *motion_model)
{
    V5NativeG53GeometryStatusBlock block;
    int values_ok;
    int model_ok;
    memset(&block, 0, sizeof(block));
    block.magic = V5_G53_GEOMETRY_MAGIC;
    block.version = V5_G53_GEOMETRY_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.center_count = V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT;
    block.axis_count = V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT;
    values_ok = g53_geometry_copy_values(&block, centers, center_count, axis_count);
    model_ok = g53_geometry_copy_motion_model(&block, motion_model);
    block.valid = (valid && values_ok && model_ok) ? 1U : 0U;
    block.epoch = block.valid ? (epoch ? epoch : 1U) : 0U;
    block.monotonic_ns = g53_geometry_monotonic_ns();
    block.crc32 = g53_geometry_crc32_like(&block);
    return g53_geometry_write_block(path, &block);
}
