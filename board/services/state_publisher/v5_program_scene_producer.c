#include "v5_program_scene_producer.h"

#include "v5_program_scene_projection.h"

#include <math.h>
#include <string.h>

#define V5_PROGRAM_SCENE_MODEL_AXIS_LENGTH 40.0

void v5_program_scene_producer_init(V5ProgramSceneProducer *producer)
{
    if (!producer) return;
    memset(producer, 0, sizeof(*producer));
    v5_program_scene_request_init(&producer->request);
    producer->request_valid = 1;
}

void v5_program_scene_producer_set_request(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneRequest *request)
{
    if (!producer || !v5_program_scene_request_valid(request)) return;
    producer->request = *request;
    producer->request_valid = 1;
}

static int unchanged_scene(
    const V5ProgramSceneProducer *producer,
    const V5NativeDisplaySample *sample,
    int static_changed,
    int tool_valid,
    double tool_length)
{
    const uint32_t dynamic_mask =
        V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    const uint32_t valid = sample->valid_mask & dynamic_mask;
    return !static_changed && producer->scene_valid &&
        producer->last_dynamic_valid_mask == valid &&
        ((valid & V5_STATUS_VALID_MCS) == 0U ||
            memcmp(
                producer->last_mcs, sample->mcs,
                sizeof(producer->last_mcs)) == 0) &&
        ((valid & V5_STATUS_VALID_CMD_MCS) == 0U ||
            memcmp(
                producer->last_cmd_mcs, sample->cmd_mcs,
                sizeof(producer->last_cmd_mcs)) == 0) &&
        producer->last_request_view_generation ==
            producer->request.view_generation &&
        producer->scene.plane == producer->request.plane &&
        producer->last_request_fit_generation ==
            producer->request.fit_generation &&
        producer->last_request_page_visible ==
            producer->request.page_visible &&
        producer->last_tool_length_valid == tool_valid &&
        (!tool_valid || producer->last_tool_length == tool_length);
}

static void apply_scene_metadata(
    V5ProgramSceneProducer *producer,
    const V5NativeDisplaySample *sample,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id,
    uint32_t rtcp_enabled,
    int model_valid,
    uint32_t dirty_flags)
{
    V5ProgramSceneModelGeometry geometry;
    unsigned int axis;
    producer->scene.program_source_identity =
        producer->request.program_source_identity;
    producer->scene.program_generation = producer->request.program_generation;
    producer->scene.native_generation = sample->source_generation;
    producer->scene.active_model_generation =
        v5_program_scene_active_model_generation(readback, model_id);
    producer->scene.rtcp_generation = sample->source_generation;
    producer->scene.wcs_generation = readback->wcs_offsets_epoch;
    producer->scene.view_generation = producer->request.view_generation;
    producer->scene.fit_generation = producer->fit_generation;
    producer->scene.plane = producer->request.plane;
    producer->scene.active_model_id = model_id;
    producer->scene.program_wcs_mask = producer->request.program_wcs_mask;
    producer->scene.current_wcs_index =
        readback->wcs_index >= 0 ? (uint8_t)readback->wcs_index : 255U;
    producer->scene.flags = V5_STATUS_SCENE_FLAG_VALID |
        V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
        (dirty_flags & V5_STATUS_SCENE_FLAG_DIRTY_MASK);
    if (producer->scene.point_count) {
        producer->scene.flags |= V5_STATUS_SCENE_FLAG_PROGRAM;
    }
    if (rtcp_enabled) producer->scene.flags |= V5_STATUS_SCENE_FLAG_RTCP;
    if (model_valid) {
        producer->scene.flags |= V5_STATUS_SCENE_FLAG_MODEL;
        producer->scene.primary_axis = model->primary_axis;
        producer->scene.child_axis = model->child_axis;
        if (v5_program_scene_model_geometry(model, &geometry)) {
            for (axis = 0U; axis < 3U; ++axis) {
                producer->scene.primary_center[axis] =
                    (float)geometry.primary_center[axis];
                producer->scene.child_center[axis] =
                    (float)geometry.child_center[axis];
            }
        }
    }
    if (v5_native_readback_wcs_table_known(readback)) {
        producer->scene.flags |= V5_STATUS_SCENE_FLAG_WCS;
    }
}

static int model_geometry_points(
    const V5ProgramSceneModel *model,
    uint32_t model_id,
    double start[2][V5_STATUS_AXIS_COUNT],
    double end[2][V5_STATUS_AXIS_COUNT],
    double center[2][V5_STATUS_AXIS_COUNT])
{
    V5ProgramSceneModelGeometry geometry;
    unsigned int model_axis;
    unsigned int axis;
    if (!model || !model_id ||
        !v5_program_scene_model_geometry(model, &geometry)) return 0;
    for (model_axis = 0U; model_axis < 2U; ++model_axis) {
        const double *source_center = model_axis ?
            geometry.child_center : geometry.primary_center;
        const double *direction = model_axis ?
            geometry.child_direction : geometry.primary_direction;
        memset(start[model_axis], 0, sizeof(start[model_axis]));
        memset(end[model_axis], 0, sizeof(end[model_axis]));
        memset(center[model_axis], 0, sizeof(center[model_axis]));
        for (axis = 0U; axis < 3U; ++axis) {
            center[model_axis][axis] = source_center[axis];
            start[model_axis][axis] = source_center[axis] -
                direction[axis] * V5_PROGRAM_SCENE_MODEL_AXIS_LENGTH;
            end[model_axis][axis] = source_center[axis] +
                direction[axis] * V5_PROGRAM_SCENE_MODEL_AXIS_LENGTH;
        }
    }
    return 1;
}

static void add_model_geometry_bounds(
    V5ProgramSceneBounds *bounds,
    const V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t model_id)
{
    double start[2][V5_STATUS_AXIS_COUNT];
    double end[2][V5_STATUS_AXIS_COUNT];
    double center[2][V5_STATUS_AXIS_COUNT];
    unsigned int model_axis;
    if (!bounds || !producer || !model_geometry_points(
            model, model_id, start, end, center)) return;
    for (model_axis = 0U; model_axis < 2U; ++model_axis) {
        v5_program_scene_bounds_add(
            bounds, start[model_axis], producer->request.plane);
        v5_program_scene_bounds_add(
            bounds, end[model_axis], producer->request.plane);
        v5_program_scene_bounds_add(
            bounds, center[model_axis], producer->request.plane);
    }
}

static void add_model_geometry_scene(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t model_id)
{
    double start[2][V5_STATUS_AXIS_COUNT];
    double end[2][V5_STATUS_AXIS_COUNT];
    double center[2][V5_STATUS_AXIS_COUNT];
    unsigned int model_axis;
    if (!producer || !model_geometry_points(
            model, model_id, start, end, center)) return;
    for (model_axis = 0U; model_axis < 2U; ++model_axis) {
        unsigned int segment_count = producer->scene.segment_count;
        unsigned int marker_count = producer->scene.marker_count;
        v5_program_scene_add_dynamic_segment(
            producer, start[model_axis], end[model_axis],
            V5_STATUS_SCENE_SEGMENT_MODEL_AXIS);
        if (producer->scene.segment_count == segment_count + 1U) {
            V5StatusSceneSegment *segment =
                &producer->scene.segments[producer->scene.segment_count - 1U];
            segment->index = (uint16_t)model_axis;
        }
        v5_program_scene_add_dynamic_marker(
            producer, center[model_axis],
            V5_STATUS_SCENE_MARKER_MODEL_CENTER);
        if (producer->scene.marker_count == marker_count + 1U) {
            V5StatusSceneMarker *marker =
                &producer->scene.markers[producer->scene.marker_count - 1U];
            marker->index = (uint16_t)model_axis;
        }
    }
}

int v5_program_scene_producer_build(
    V5ProgramSceneProducer *producer,
    const V5NativeDisplaySample *sample,
    const V5NativeReadback *readback,
    V5StatusDisplayScene *scene_out,
    uint64_t *generation_out)
{
    V5ProgramSceneBounds candidate;
    V5ProgramSceneModel model;
    double holder_end[V5_STATUS_AXIS_COUNT];
    double cmd_tip[V5_STATUS_AXIS_COUNT];
    double tool_length = 0.0;
    uint32_t model_id = 0U;
    uint32_t rtcp_enabled;
    int model_valid;
    int static_changed;
    int pose_changed;
    int model_pose_changed;
    int dynamic_changed;
    int reproject;
    int fit_key_changed;
    int program_projected = 0;
    int tool_valid;
    uint32_t dirty_flags;
    if (generation_out) *generation_out = 0ULL;
    if (!producer || !sample || !readback || !scene_out ||
        !sample->available || sample->source_generation == 0ULL ||
        !producer->request_valid) return 0;
    memset(&model, 0, sizeof(model));
    rtcp_enabled = v5_native_readback_rtcp_known(readback) &&
        readback->rtcp_enabled ? 1U : 0U;
    model_valid = v5_program_scene_model_resolve(
        readback, (const V5StatusPoint *)sample->mcs, &model);
    if (model_valid) model_id = model.registry_id;
    if (rtcp_enabled && !model_valid) return 0;
    static_changed = !v5_program_scene_static_key_same(
        producer, readback, &model, model_id);
    pose_changed = static_changed || !v5_program_scene_pose_cache_same(
        producer, &model, rtcp_enabled);
    model_pose_changed = !producer->scene_valid ||
        !v5_program_scene_model_pose_same(&producer->model, &model);
    tool_valid = v5_native_readback_tool_length_known(readback);
    if (tool_valid) tool_length = readback->tool_length_mm;
    if (unchanged_scene(
            producer, sample, static_changed || pose_changed,
            tool_valid, tool_length)) {
        *scene_out = producer->scene;
        if (generation_out) *generation_out = producer->scene_generation;
        return 1;
    }
    if (static_changed && !v5_program_scene_build_static_cache(
            producer, readback, &model, model_id)) return 0;
    if (pose_changed && !v5_program_scene_prepare_pose_cache(
            producer, &model, rtcp_enabled)) return 0;
    fit_key_changed = v5_program_scene_fit_key_changed(
        producer, &producer->request);
    if (pose_changed) {
        if (producer->fit_bounds.valid && !fit_key_changed) {
            if (!v5_program_scene_transform_project_program(
                    producer, 1)) return 0;
            program_projected = 1;
        } else if (!v5_program_scene_transform_project_program(
                producer, 0)) return 0;
    }
    candidate = producer->pose_bounds;
    add_model_geometry_bounds(&candidate, producer, &model, model_id);
    memcpy(holder_end, sample->mcs, sizeof(holder_end));
    holder_end[2] -= tool_length;
    memcpy(cmd_tip, sample->cmd_mcs, sizeof(cmd_tip));
    cmd_tip[2] -= tool_length;
    if (sample->valid_mask & V5_STATUS_VALID_MCS) {
        v5_program_scene_bounds_add(
            &candidate, sample->mcs, producer->request.plane);
        v5_program_scene_bounds_add(
            &candidate, holder_end, producer->request.plane);
    }
    if (sample->valid_mask & V5_STATUS_VALID_CMD_MCS) {
        v5_program_scene_bounds_add(
            &candidate, cmd_tip, producer->request.plane);
    }
    reproject = static_changed || pose_changed || !producer->scene_valid ||
        producer->last_request_view_generation !=
            producer->request.view_generation ||
        producer->scene.plane != producer->request.plane;
    if (producer->request.page_visible &&
        (fit_key_changed ||
         v5_program_scene_bounds_outside_viewport(
             &candidate, &producer->fit_bounds, &producer->request))) {
        v5_program_scene_commit_fit(producer, &candidate);
        if (!v5_program_scene_transform_project_program(
                producer, 1)) return 0;
        program_projected = 1;
        reproject = 1;
    }
    dynamic_changed = !producer->scene_valid ||
        producer->last_dynamic_valid_mask != (sample->valid_mask &
            (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS)) ||
        ((sample->valid_mask & V5_STATUS_VALID_MCS) != 0U &&
            memcmp(producer->last_mcs, sample->mcs,
                sizeof(producer->last_mcs)) != 0) ||
        ((sample->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0U &&
            memcmp(producer->last_cmd_mcs, sample->cmd_mcs,
                sizeof(producer->last_cmd_mcs)) != 0) ||
        producer->last_tool_length_valid != tool_valid ||
        (tool_valid && producer->last_tool_length != tool_length);
    if (reproject && !program_projected) {
        if (!v5_program_scene_transform_project_program(
                producer, 1)) return 0;
    } else if (!reproject) {
        v5_program_scene_prepare_dynamic_update(producer);
    }
    producer->model = model;
    dirty_flags = reproject ? V5_STATUS_SCENE_FLAG_DIRTY_MASK :
        ((model_pose_changed ? V5_STATUS_SCENE_FLAG_DIRTY_MODEL : 0U) |
         (dynamic_changed ? V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC : 0U));
    apply_scene_metadata(
        producer, sample, readback, &model,
        model_id, rtcp_enabled, model_valid,
        dirty_flags);
    add_model_geometry_scene(producer, &model, model_id);
    if (sample->valid_mask & V5_STATUS_VALID_MCS) {
        v5_program_scene_add_dynamic_marker(
            producer, sample->mcs, V5_STATUS_SCENE_MARKER_MCS_ACTUAL);
        if (fabs(tool_length) > 1.0e-9) {
            v5_program_scene_add_dynamic_segment(
                producer, sample->mcs, holder_end,
                V5_STATUS_SCENE_SEGMENT_HOLDER);
        }
    }
    if (sample->valid_mask & V5_STATUS_VALID_CMD_MCS) {
        v5_program_scene_add_dynamic_marker(
            producer, cmd_tip, V5_STATUS_SCENE_MARKER_CMD_TIP);
    }
    producer->scene.build_count = producer->build_count;
    producer->scene.rtcp_transform_count = producer->transform_count;
    producer->scene.project_count = producer->project_count;
    if (++producer->scene_generation == 0ULL) producer->scene_generation = 1ULL;
    producer->last_source_generation = sample->source_generation;
    producer->last_dynamic_valid_mask = sample->valid_mask &
        (V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS);
    memcpy(producer->last_mcs, sample->mcs, sizeof(producer->last_mcs));
    memcpy(
        producer->last_cmd_mcs, sample->cmd_mcs,
        sizeof(producer->last_cmd_mcs));
    producer->last_request_view_generation =
        producer->request.view_generation;
    producer->last_request_fit_generation =
        producer->request.fit_generation;
    producer->last_request_page_visible =
        producer->request.page_visible;
    producer->last_tool_length = tool_length;
    producer->last_tool_length_valid = tool_valid;
    producer->scene_valid = 1;
    *scene_out = producer->scene;
    if (generation_out) *generation_out = producer->scene_generation;
    return 1;
}
