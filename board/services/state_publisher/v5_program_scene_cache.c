#include "v5_program_scene_projection.h"

#include <string.h>

#define V5_SCENE_AXIS_LENGTH 40.0

static void prepare_axis(
    double out[V5_STATUS_AXIS_COUNT],
    const double origin[V5_STATUS_AXIS_COUNT],
    unsigned int axis)
{
    memcpy(out, origin, sizeof(double) * V5_STATUS_AXIS_COUNT);
    out[axis] += V5_SCENE_AXIS_LENGTH;
}

static int cache_segment(
    V5ProgramSceneProducer *producer,
    const double start[V5_STATUS_AXIS_COUNT],
    const double end[V5_STATUS_AXIS_COUNT],
    uint16_t role,
    uint16_t index)
{
    V5ProgramSceneWorldSegment *segment;
    if (producer->world_segment_count >= V5_STATUS_SCENE_SEGMENT_COUNT) {
        return 0;
    }
    segment = &producer->world_segments[producer->world_segment_count++];
    memcpy(segment->start.axis, start, sizeof(segment->start.axis));
    memcpy(segment->end.axis, end, sizeof(segment->end.axis));
    segment->role = role;
    segment->index = index;
    v5_program_scene_bounds_add(
        &producer->static_bounds, start, producer->request.plane);
    v5_program_scene_bounds_add(
        &producer->static_bounds, end, producer->request.plane);
    return 1;
}

static int cache_marker(
    V5ProgramSceneProducer *producer,
    const double point[V5_STATUS_AXIS_COUNT],
    uint16_t role,
    uint16_t index)
{
    V5ProgramSceneWorldMarker *marker;
    if (producer->world_marker_count >= V5_STATUS_SCENE_MARKER_COUNT) return 0;
    marker = &producer->world_markers[producer->world_marker_count++];
    memcpy(marker->point.axis, point, sizeof(marker->point.axis));
    marker->role = role;
    marker->index = index;
    v5_program_scene_bounds_add(
        &producer->static_bounds, point, producer->request.plane);
    return 1;
}

uint64_t v5_program_scene_active_model_generation(
    const V5NativeReadback *readback,
    uint32_t model_id)
{
    return ((uint64_t)(readback ? readback->g53_geometry_epoch : 0U) << 32) |
        model_id;
}

int v5_program_scene_static_key_same(
    const V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id,
    uint32_t rtcp_enabled)
{
    return producer->static_valid &&
        producer->static_program_source_identity ==
            producer->request.program_source_identity &&
        producer->static_program_generation ==
            producer->request.program_generation &&
        producer->static_program_wcs_mask ==
            producer->request.program_wcs_mask &&
        producer->static_point_count == producer->request.point_count &&
        producer->static_active_model_generation ==
            v5_program_scene_active_model_generation(readback, model_id) &&
        producer->static_wcs_generation == readback->wcs_offsets_epoch &&
        producer->static_rtcp_enabled == rtcp_enabled &&
        memcmp(&producer->static_model, model, sizeof(*model)) == 0;
}

static int cache_mcs_and_wcs_axes(
    V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t rtcp_enabled)
{
    double origin[V5_STATUS_AXIS_COUNT] = {0};
    double endpoint[V5_STATUS_AXIS_COUNT];
    double wcs_origin[V5_STATUS_AXIS_COUNT];
    double wcs_axis[3][V5_STATUS_AXIS_COUNT];
    unsigned int axis;
    for (axis = 0U; axis < 3U; ++axis) {
        prepare_axis(endpoint, origin, axis);
        if (!cache_segment(
                producer, origin, endpoint,
                V5_STATUS_SCENE_SEGMENT_MCS_AXIS, (uint16_t)axis)) return 0;
    }
    if (!cache_marker(
            producer, origin, V5_STATUS_SCENE_MARKER_MCS_ORIGIN, 0U)) return 0;
    if (!v5_native_readback_wcs_offset_known(readback)) return 1;
    memset(wcs_origin, 0, sizeof(wcs_origin));
    {
        const double *active = v5_native_readback_active_wcs_offsets(readback);
        for (axis = 0U; active && axis < 3U; ++axis) {
            wcs_origin[axis] = active[axis];
        }
    }
    for (axis = 0U; axis < 3U; ++axis) {
        prepare_axis(wcs_axis[axis], wcs_origin, axis);
    }
    if (rtcp_enabled) {
        if (!v5_program_scene_model_transform(model, wcs_origin)) return 0;
        for (axis = 0U; axis < 3U; ++axis) {
            if (!v5_program_scene_model_transform(
                    model, wcs_axis[axis])) return 0;
        }
    }
    for (axis = 0U; axis < 3U; ++axis) {
        if (!cache_segment(
                producer, wcs_origin, wcs_axis[axis],
                V5_STATUS_SCENE_SEGMENT_WCS_AXIS, (uint16_t)axis)) return 0;
    }
    return cache_marker(
        producer, wcs_origin, V5_STATUS_SCENE_MARKER_WCS_ORIGIN, 0U);
}

static int cache_model_axes(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t model_id)
{
    V5ProgramSceneModelGeometry geometry;
    unsigned int model_axis;
    unsigned int axis;
    if (!model_id || !v5_program_scene_model_geometry(model, &geometry)) {
        return 1;
    }
    for (model_axis = 0U; model_axis < 2U; ++model_axis) {
        const double *center = model_axis ?
            geometry.child_center : geometry.primary_center;
        const double *direction = model_axis ?
            geometry.child_direction : geometry.primary_direction;
        double start[V5_STATUS_AXIS_COUNT] = {0};
        double end[V5_STATUS_AXIS_COUNT] = {0};
        for (axis = 0U; axis < 3U; ++axis) {
            start[axis] = center[axis] -
                direction[axis] * V5_SCENE_AXIS_LENGTH;
            end[axis] = center[axis] +
                direction[axis] * V5_SCENE_AXIS_LENGTH;
        }
        if (!cache_segment(
                producer, start, end,
                V5_STATUS_SCENE_SEGMENT_MODEL_AXIS,
                (uint16_t)model_axis) ||
            !cache_marker(
                producer, center,
                V5_STATUS_SCENE_MARKER_MODEL_CENTER,
                (uint16_t)model_axis)) return 0;
    }
    return 1;
}

int v5_program_scene_build_static_cache(
    V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id,
    uint32_t rtcp_enabled)
{
    unsigned int i;
    unsigned int axis;
    memset(&producer->static_bounds, 0, sizeof(producer->static_bounds));
    producer->world_segment_count = 0U;
    producer->world_marker_count = 0U;
    for (i = 0U; i < producer->request.point_count; ++i) {
        int wcs = producer->request.wcs_index[i];
        producer->world_points[i] = producer->request.points[i];
        if (wcs < 0 || wcs >= (int)V5_NATIVE_READBACK_WCS_COUNT ||
            !v5_native_readback_wcs_table_known(readback)) return 0;
        for (axis = 0U; axis < 3U; ++axis) {
            producer->world_points[i].axis[axis] +=
                readback->wcs_offsets[wcs][axis];
        }
        if (rtcp_enabled && !v5_program_scene_model_transform(
                model, producer->world_points[i].axis)) return 0;
        v5_program_scene_bounds_add(
            &producer->static_bounds, producer->world_points[i].axis,
            producer->request.plane);
    }
    if (!cache_mcs_and_wcs_axes(
            producer, readback, model, rtcp_enabled) ||
        !cache_model_axes(producer, model, model_id)) return 0;
    producer->static_model = *model;
    producer->static_program_source_identity =
        producer->request.program_source_identity;
    producer->static_program_generation = producer->request.program_generation;
    producer->static_program_wcs_mask = producer->request.program_wcs_mask;
    producer->static_point_count = producer->request.point_count;
    producer->static_active_model_generation =
        v5_program_scene_active_model_generation(readback, model_id);
    producer->static_wcs_generation = readback->wcs_offsets_epoch;
    producer->static_rtcp_enabled = rtcp_enabled;
    producer->static_valid = producer->static_bounds.valid;
    producer->build_count += 1ULL;
    if (rtcp_enabled) producer->transform_count += 1ULL;
    return producer->static_valid;
}
