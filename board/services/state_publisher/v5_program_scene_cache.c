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
    if (producer->base_segment_count >= V5_STATUS_SCENE_SEGMENT_COUNT) {
        return 0;
    }
    segment = &producer->base_segments[producer->base_segment_count++];
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
    if (producer->base_marker_count >= V5_STATUS_SCENE_MARKER_COUNT) return 0;
    marker = &producer->base_markers[producer->base_marker_count++];
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
    uint32_t model_id)
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
        v5_program_scene_model_topology_same(
            &producer->static_model, model);
}

static int cache_mcs_and_wcs_axes(
    V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback)
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
    for (axis = 0U; axis < 3U; ++axis) {
        if (!cache_segment(
                producer, wcs_origin, wcs_axis[axis],
                V5_STATUS_SCENE_SEGMENT_WCS_AXIS, (uint16_t)axis)) return 0;
    }
    return cache_marker(
        producer, wcs_origin, V5_STATUS_SCENE_MARKER_WCS_ORIGIN, 0U);
}

int v5_program_scene_build_static_cache(
    V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id)
{
    unsigned int i;
    unsigned int axis;
    memset(&producer->static_bounds, 0, sizeof(producer->static_bounds));
    producer->base_segment_count = 0U;
    producer->base_marker_count = 0U;
    for (i = 0U; i < producer->request.point_count; ++i) {
        int wcs = producer->request.wcs_index[i];
        producer->base_points[i] = producer->request.points[i];
        if (wcs < 0 || wcs >= (int)V5_NATIVE_READBACK_WCS_COUNT ||
            !v5_native_readback_wcs_table_known(readback)) return 0;
        for (axis = 0U; axis < 3U; ++axis) {
            producer->base_points[i].axis[axis] +=
                readback->wcs_offsets[wcs][axis];
        }
        v5_program_scene_bounds_add(
            &producer->static_bounds, producer->base_points[i].axis,
            producer->request.plane);
    }
    if (!cache_mcs_and_wcs_axes(producer, readback)) return 0;
    producer->static_model = *model;
    producer->static_program_source_identity =
        producer->request.program_source_identity;
    producer->static_program_generation = producer->request.program_generation;
    producer->static_program_wcs_mask = producer->request.program_wcs_mask;
    producer->static_point_count = producer->request.point_count;
    producer->static_active_model_generation =
        v5_program_scene_active_model_generation(readback, model_id);
    producer->static_wcs_generation = readback->wcs_offsets_epoch;
    producer->static_valid = producer->static_bounds.valid;
    producer->pose_valid = 0;
    producer->build_count += 1ULL;
    return producer->static_valid;
}

int v5_program_scene_pose_cache_same(
    const V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t rtcp_enabled)
{
    return producer && model && producer->pose_valid &&
        producer->pose_rtcp_enabled == (rtcp_enabled ? 1 : 0) &&
        producer->pose_plane == producer->request.plane &&
        (!rtcp_enabled || v5_program_scene_model_pose_same(
            &producer->pose_model, model));
}

int v5_program_scene_prepare_pose_cache(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t rtcp_enabled)
{
    unsigned int i;
    if (!producer || !model || !producer->static_valid) return 0;
    memset(&producer->pose_bounds, 0, sizeof(producer->pose_bounds));
    v5_program_scene_pose_matrix_identity(&producer->pose_matrix);
    if (rtcp_enabled && !v5_program_scene_model_pose_matrix(
            model, &producer->pose_matrix)) return 0;
    producer->world_segment_count = producer->base_segment_count;
    for (i = 0U; i < producer->base_segment_count; ++i) {
        producer->world_segments[i] = producer->base_segments[i];
        if (rtcp_enabled &&
            producer->world_segments[i].role ==
                V5_STATUS_SCENE_SEGMENT_WCS_AXIS) {
            double start[V5_STATUS_AXIS_COUNT];
            double end[V5_STATUS_AXIS_COUNT];
            v5_program_scene_pose_matrix_apply(
                &producer->pose_matrix,
                producer->world_segments[i].start.axis, start);
            v5_program_scene_pose_matrix_apply(
                &producer->pose_matrix,
                producer->world_segments[i].end.axis, end);
            memcpy(producer->world_segments[i].start.axis, start, sizeof(start));
            memcpy(producer->world_segments[i].end.axis, end, sizeof(end));
        }
        v5_program_scene_bounds_add(
            &producer->pose_bounds,
            producer->world_segments[i].start.axis,
            producer->request.plane);
        v5_program_scene_bounds_add(
            &producer->pose_bounds,
            producer->world_segments[i].end.axis,
            producer->request.plane);
    }
    producer->world_marker_count = producer->base_marker_count;
    for (i = 0U; i < producer->base_marker_count; ++i) {
        producer->world_markers[i] = producer->base_markers[i];
        if (rtcp_enabled &&
            producer->world_markers[i].role ==
                V5_STATUS_SCENE_MARKER_WCS_ORIGIN) {
            double transformed[V5_STATUS_AXIS_COUNT];
            v5_program_scene_pose_matrix_apply(
                &producer->pose_matrix,
                producer->world_markers[i].point.axis, transformed);
            memcpy(
                producer->world_markers[i].point.axis,
                transformed, sizeof(transformed));
        }
        v5_program_scene_bounds_add(
            &producer->pose_bounds,
            producer->world_markers[i].point.axis,
            producer->request.plane);
    }
    producer->pose_model = *model;
    producer->pose_rtcp_enabled = rtcp_enabled ? 1 : 0;
    producer->pose_plane = producer->request.plane;
    producer->pose_valid = producer->pose_bounds.valid;
    if (rtcp_enabled) producer->transform_count += 1ULL;
    return producer->pose_valid;
}
