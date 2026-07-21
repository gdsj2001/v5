#include "v5_program_scene_producer.h"
#include "v5_toolpath_viewport.h"

#include <math.h>
#include <string.h>

static void prepare_readback(V5NativeReadback *readback, const char *model_name, int rtcp)
{
    double offsets[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT] = {{0}};
    double centers[V5_NATIVE_READBACK_G53_CENTER_COUNT][V5_NATIVE_READBACK_G53_AXIS_COUNT] = {
        {0.0, 20.0, -50.0}, {10.0, 0.0, -40.0}, {50.0, 20.0, 0.0}};
    offsets[0][0] = 10.0;
    offsets[0][1] = 12.0;
    offsets[0][2] = 8.0;
    v5_native_readback_init(readback);
    v5_native_readback_set_rtcp_actual(readback, rtcp);
    v5_native_readback_set_wcs_table(readback, 0, &offsets[0][0],
        V5_NATIVE_READBACK_WCS_COUNT, V5_NATIVE_READBACK_WCS_AXIS_COUNT, 7U);
    v5_native_readback_set_g53_geometry(readback, &centers[0][0],
        V5_NATIVE_READBACK_G53_CENTER_COUNT, V5_NATIVE_READBACK_G53_AXIS_COUNT, 9U);
    v5_native_readback_set_motion_model(readback, model_name);
    v5_native_readback_set_tool_actual(readback, 1, 1, 25.0);
}

static const V5StatusSceneMarker *find_marker(
    const V5StatusDisplayScene *scene,
    uint16_t role)
{
    unsigned int i;
    if (!scene) return 0;
    for (i = 0U; i < scene->marker_count; ++i) {
        if (scene->markers[i].role == role) return &scene->markers[i];
    }
    return 0;
}

static unsigned int count_segments(
    const V5StatusDisplayScene *scene,
    uint16_t role)
{
    unsigned int i;
    unsigned int count = 0U;
    if (!scene) return 0U;
    for (i = 0U; i < scene->segment_count; ++i) {
        if (scene->segments[i].role == role) count += 1U;
    }
    return count;
}

static unsigned int count_markers(
    const V5StatusDisplayScene *scene,
    uint16_t role)
{
    unsigned int i;
    unsigned int count = 0U;
    if (!scene) return 0U;
    for (i = 0U; i < scene->marker_count; ++i) {
        if (scene->markers[i].role == role) count += 1U;
    }
    return count;
}

static const V5StatusSceneSegment *find_segment(
    const V5StatusDisplayScene *scene,
    uint16_t role,
    uint16_t index)
{
    unsigned int i;
    if (!scene) return 0;
    for (i = 0U; i < scene->segment_count; ++i) {
        if (scene->segments[i].role == role &&
            scene->segments[i].index == index) return &scene->segments[i];
    }
    return 0;
}

static int rotary_pose_cache_smoke(const char *model_name)
{
    V5ProgramSceneProducer producer;
    V5ProgramSceneRequest request;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    V5StatusDisplayScene first;
    V5StatusDisplayScene scene;
    uint64_t generation = 0ULL;
    uint64_t initial_build_count;
    uint64_t initial_transform_count;
    uint64_t initial_project_count;
    uint64_t initial_point_traversal_count;
    uint64_t initial_fit_generation;
    float first_x;
    float first_y;
    int program_moved = 0;
    unsigned int frame;

    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = model_name[3] == 'A' ? 0xa1ULL : 0xb1ULL;
    request.program_generation = 1ULL;
    request.view_generation = 1ULL;
    request.fit_generation = 1ULL;
    request.point_count = 3U;
    request.program_wcs_mask = 1U;
    request.points[0].axis[0] = 5.0;
    request.points[0].axis[1] = 7.0;
    request.points[0].axis[2] = 11.0;
    request.points[1].axis[0] = 20.0;
    request.points[1].axis[1] = -3.0;
    request.points[1].axis[2] = 9.0;
    request.points[2].axis[0] = -4.0;
    request.points[2].axis[1] = 18.0;
    request.points[2].axis[2] = 2.0;
    request.wcs_index[0] = 0;
    request.wcs_index[1] = 0;
    request.wcs_index[2] = 0;
    v5_program_scene_producer_set_request(&producer, &request);

    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 1ULL;
    sample.mcs[3] = 10.0;
    sample.mcs[4] = 20.0;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    prepare_readback(&readback, model_name, 1);
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &first, &generation)) return 0;
    if ((first.flags & (V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
            V5_STATUS_SCENE_FLAG_DIRTY_MASK)) !=
        (V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
            V5_STATUS_SCENE_FLAG_DIRTY_MASK)) return 0;
    initial_build_count = first.build_count;
    initial_transform_count = first.rtcp_transform_count;
    initial_project_count = first.project_count;
    initial_point_traversal_count = producer.point_traversal_count;
    initial_fit_generation = first.fit_generation;
    first_x = first.points[0].x;
    first_y = first.points[0].y;
    for (frame = 0U; frame < 300U; ++frame) {
        const double step = (double)((frame + 1U) % 101U) * 0.0005;
        sample.source_generation += 1ULL;
        sample.mcs[3] = 10.0 + step;
        sample.mcs[4] = 20.0 - step;
        memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &scene, &generation) ||
            scene.build_count != initial_build_count ||
            scene.rtcp_transform_count != initial_transform_count + frame + 1ULL ||
            scene.project_count != initial_project_count + frame + 1ULL ||
            producer.point_traversal_count !=
                initial_point_traversal_count + frame + 1ULL ||
            scene.fit_generation != initial_fit_generation ||
            (scene.flags & (V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
                V5_STATUS_SCENE_FLAG_DIRTY_MASK)) !=
                (V5_STATUS_SCENE_FLAG_DIRTY_KNOWN |
                    V5_STATUS_SCENE_FLAG_DIRTY_MASK)) return 0;
        if (fabs((double)scene.points[0].x - first_x) > 1.0e-5 ||
            fabs((double)scene.points[0].y - first_y) > 1.0e-5) {
            program_moved = 1;
        }
    }
    if (!program_moved) return 0;

    /* RTCP OFF freezes program pixels while model geometry remains dynamic. */
    v5_program_scene_producer_init(&producer);
    v5_program_scene_producer_set_request(&producer, &request);
    sample.source_generation += 1ULL;
    sample.mcs[3] = 0.0;
    sample.mcs[4] = 0.0;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    prepare_readback(&readback, model_name, 0);
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &first, &generation)) return 0;
    {
        const V5StatusSceneSegment *first_model_axis = find_segment(
            &first, V5_STATUS_SCENE_SEGMENT_MODEL_AXIS, 1U);
        V5StatusScreenPoint first_axis_end;
        if (!first_model_axis) return 0;
        first_axis_end = first_model_axis->end;
        sample.source_generation += 1ULL;
        sample.mcs[3] = 17.0;
        sample.mcs[4] = -23.0;
        memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &scene, &generation) ||
            scene.build_count != first.build_count ||
            scene.rtcp_transform_count != first.rtcp_transform_count ||
            scene.project_count != first.project_count ||
            (scene.flags & V5_STATUS_SCENE_FLAG_DIRTY_KNOWN) == 0U ||
            (scene.flags & V5_STATUS_SCENE_FLAG_DIRTY_STATIC) != 0U ||
            (scene.flags & (V5_STATUS_SCENE_FLAG_DIRTY_MODEL |
                V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC)) !=
                (V5_STATUS_SCENE_FLAG_DIRTY_MODEL |
                    V5_STATUS_SCENE_FLAG_DIRTY_DYNAMIC) ||
            memcmp(scene.points, first.points,
                sizeof(first.points[0]) * first.point_count) != 0) return 0;
        {
            const V5StatusSceneSegment *second_model_axis = find_segment(
                &scene, V5_STATUS_SCENE_SEGMENT_MODEL_AXIS, 1U);
            if (!second_model_axis ||
                (second_model_axis->end.x == first_axis_end.x &&
                 second_model_axis->end.y == first_axis_end.y)) return 0;
        }
    }
    return 1;
}

static int pixel_compaction_smoke(void)
{
    V5ProgramSceneProducer producer;
    V5ProgramSceneRequest request;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    V5StatusDisplayScene scene;
    uint64_t generation = 0ULL;
    unsigned int i;
    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = 0xc1ULL;
    request.program_generation = 1ULL;
    request.point_count = 4U;
    request.program_wcs_mask = 1U;
    for (i = 0U; i < request.point_count; ++i) {
        request.points[i].axis[0] = 1.0;
        request.points[i].axis[1] = 2.0;
        request.points[i].axis[2] = 3.0;
        request.wcs_index[i] = 0;
    }
    request.break_before[2] = 1U;
    v5_program_scene_producer_set_request(&producer, &request);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 1ULL;
    prepare_readback(&readback, "XYZAC_TRT", 0);
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.point_count != 2U || scene.break_before[0] != 0U ||
        scene.break_before[1] != 1U ||
        lroundf(scene.points[0].x) != lroundf(scene.points[1].x) ||
        lroundf(scene.points[0].y) != lroundf(scene.points[1].y)) return 0;
    return 1;
}

static int ipc_validation_smoke(void)
{
    V5ProgramSceneRequest valid;
    V5ProgramSceneRequest invalid;
    unsigned int variant;
    v5_program_scene_request_init(&valid);
    if (valid.plane != V5_STATUS_SCENE_PLANE_3D) return 0;
    valid.program_source_identity = 1ULL;
    valid.program_generation = 1ULL;
    valid.point_count = 1U;
    valid.wcs_index[0] = 0;
    if (!v5_program_scene_request_valid(&valid) ||
        valid.width != (float)v5_toolpath_viewport()->width ||
        valid.height != (float)v5_toolpath_viewport()->height) return 0;
    for (variant = 0U; variant < 9U; ++variant) {
        invalid = valid;
        switch (variant) {
        case 0U: invalid.points[0].axis[0] = NAN; break;
        case 1U: invalid.width = NAN; break;
        case 2U: invalid.scale = NAN; break;
        case 3U: invalid.sine = NAN; break;
        case 4U: invalid.pan_x = NAN; break;
        case 5U: invalid.wcs_index[0] = 9; break;
        case 6U: invalid.break_before[0] = 2U; break;
        case 7U: invalid.page_visible = 2U; break;
        default: invalid.program_wcs_mask = 1U << V5_PROGRAM_SCENE_WCS_COUNT; break;
        }
        if (v5_program_scene_request_valid(&invalid)) return 0;
    }
    return 1;
}

static int transport_loss_recovery_smoke(void)
{
    V5ProgramSceneRequest full;
    V5ProgramSceneRequest retry;
    V5ProgramSceneRequest current;
    V5ProgramSceneRequest view;
    V5StatusDisplayScene acknowledged;
    v5_program_scene_request_init(&full);
    v5_program_scene_request_init(&current);
    full.program_source_identity = 0x51ULL;
    full.program_generation = 0x52ULL;
    full.point_count = 2U;
    full.points[1].axis[0] = 10.0;
    full.wcs_index[0] = 0;
    full.wcs_index[1] = 0;

    retry = full;
    v5_program_scene_request_prepare_transport(&retry, 0);
    if (retry.reserved != V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL ||
        retry.point_count != 2U) return 0;
    /* The first full datagram is intentionally not merged: simulate loss. */
    retry = full;
    v5_program_scene_request_prepare_transport(&retry, 0);
    if (retry.reserved != V5_PROGRAM_SCENE_MESSAGE_PROGRAM_MODEL ||
        retry.point_count != 2U ||
        !v5_program_scene_request_merge(&current, &retry) ||
        current.point_count != 2U) return 0;

    memset(&acknowledged, 0, sizeof(acknowledged));
    acknowledged.flags = V5_STATUS_SCENE_FLAG_VALID;
    acknowledged.program_source_identity = full.program_source_identity;
    acknowledged.program_generation = full.program_generation;
    view = full;
    view.view_generation += 1ULL;
    v5_program_scene_request_prepare_transport(&view, &acknowledged);
    if (view.reserved != V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE ||
        view.point_count != 0U ||
        !v5_program_scene_request_merge(&current, &view) ||
        current.point_count != 2U ||
        current.view_generation != view.view_generation) return 0;
    return v5_program_scene_request_retry_delay_ms(0U) == 100U &&
        v5_program_scene_request_retry_delay_ms(1U) == 200U &&
        v5_program_scene_request_retry_delay_ms(2U) == 400U &&
        v5_program_scene_request_retry_delay_ms(3U) == 0U;
}

static int unknown_model_degraded_smoke(void)
{
    V5ProgramSceneProducer producer;
    V5ProgramSceneRequest request;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    V5StatusDisplayScene scene;
    uint64_t generation = 0ULL;
    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = 0x61ULL;
    request.program_generation = 0x62ULL;
    v5_program_scene_producer_set_request(&producer, &request);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 0x63ULL;
    prepare_readback(&readback, "UNKNOWN_MODEL", 0);
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation)) return 0;
    return generation != 0ULL &&
        (scene.flags & V5_STATUS_SCENE_FLAG_VALID) != 0U &&
        (scene.flags & V5_STATUS_SCENE_FLAG_MODEL) == 0U &&
        scene.active_model_id == 0U &&
        scene.primary_axis == 0U && scene.child_axis == 0U;
}

static int hidden_overflow_no_fit_smoke(void)
{
    V5ProgramSceneProducer producer;
    V5ProgramSceneRequest request;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    V5StatusDisplayScene scene;
    uint64_t generation;
    uint64_t fit_generation;
    unsigned int repeat;
    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = 0x71ULL;
    request.program_generation = 0x72ULL;
    v5_program_scene_producer_set_request(&producer, &request);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 1ULL;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    prepare_readback(&readback, "XYZAC_TRT", 0);
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.fit_generation == 0ULL) return 0;
    fit_generation = scene.fit_generation;
    request.page_visible = 0U;
    v5_program_scene_producer_set_request(&producer, &request);
    sample.source_generation += 1ULL;
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.fit_generation != fit_generation) return 0;
    request.page_visible = 1U;
    v5_program_scene_producer_set_request(&producer, &request);
    sample.source_generation += 1ULL;
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.fit_generation != fit_generation) return 0;
    request.page_visible = 0U;
    v5_program_scene_producer_set_request(&producer, &request);
    sample.mcs[0] = 10000.0;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    for (repeat = 0U; repeat < 100U; ++repeat) {
        sample.source_generation += 1ULL;
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &scene, &generation) ||
            scene.fit_generation != fit_generation) return 0;
    }
    request.page_visible = 1U;
    v5_program_scene_producer_set_request(&producer, &request);
    sample.source_generation += 1ULL;
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.fit_generation == fit_generation) return 0;
    fit_generation = scene.fit_generation;
    for (repeat = 0U; repeat < 100U; ++repeat) {
        sample.source_generation += 1ULL;
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &scene, &generation) ||
            scene.fit_generation != fit_generation) return 0;
    }
    sample.source_generation += 1ULL;
    sample.mcs[0] = 20000.0;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &scene, &generation) ||
        scene.fit_generation == fit_generation) return 0;
    return 1;
}

int main(void)
{
    V5ProgramSceneProducer producer;
    V5ProgramSceneRequest request;
    V5NativeDisplaySample sample;
    V5NativeReadback readback;
    V5StatusDisplayScene first;
    V5StatusDisplayScene second;
    uint64_t generation = 0ULL;
    uint64_t same_generation = 0ULL;
    unsigned int repeat;
    V5ProgramSceneRequest merged_request;
    V5ProgramSceneRequest view_request;
    if (!v5_program_scene_model_registry_complete()) return 29;
    if (!rotary_pose_cache_smoke("XYZAC_TRT")) return 30;
    if (!rotary_pose_cache_smoke("XYZBC_TRT")) return 31;
    if (!pixel_compaction_smoke()) return 32;
    if (!ipc_validation_smoke()) return 25;
    if (!transport_loss_recovery_smoke()) return 26;
    if (!unknown_model_degraded_smoke()) return 27;
    if (!hidden_overflow_no_fit_smoke()) return 28;
    v5_program_scene_request_init(&merged_request);
    v5_program_scene_request_init(&view_request);
    merged_request.program_source_identity = 91ULL;
    merged_request.program_generation = 92ULL;
    merged_request.point_count = 2U;
    if (!v5_program_scene_request_merge(
            &view_request, &merged_request)) return 22;
    view_request = merged_request;
    view_request.reserved = V5_PROGRAM_SCENE_MESSAGE_VIEW_UPDATE;
    view_request.point_count = 0U;
    view_request.view_generation = 7ULL;
    view_request.scale = 1.5f;
    if (!v5_program_scene_request_merge(
            &merged_request, &view_request) ||
        merged_request.point_count != 2U ||
        merged_request.view_generation != 7ULL ||
        merged_request.scale != 1.5f) return 23;
    view_request.program_generation += 1ULL;
    if (v5_program_scene_request_merge(
            &merged_request, &view_request)) return 24;
    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = 11ULL;
    request.program_generation = 3ULL;
    request.view_generation = 4ULL;
    request.fit_generation = 5ULL;
    request.point_count = 2U;
    request.program_wcs_mask = 1U;
    request.points[0].axis[0] = 0.0;
    request.points[0].axis[1] = 0.0;
    request.points[1].axis[0] = 10.0;
    request.points[1].axis[1] = 5.0;
    request.wcs_index[0] = 0;
    request.wcs_index[1] = 0;
    v5_program_scene_producer_set_request(&producer, &request);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 21ULL;
    sample.source_acquired_mono_ns = 22ULL;
    sample.mcs[3] = 15.0;
    sample.mcs[4] = 20.0;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    prepare_readback(&readback, "XYZAC_TRT", 0);
    if (!v5_program_scene_producer_build(&producer, &sample, &readback, &first, &generation) ||
        generation == 0ULL || first.active_model_id != 1U ||
        first.plane != V5_STATUS_SCENE_PLANE_3D ||
        first.point_count != 2U || first.segment_count == 0U || first.marker_count == 0U ||
        first.build_count != 1ULL || first.project_count != 1ULL ||
        first.rtcp_transform_count != 0ULL ||
        count_segments(&first, V5_STATUS_SCENE_SEGMENT_WCS_AXIS) != 3U ||
        count_markers(&first, V5_STATUS_SCENE_MARKER_WCS_ORIGIN) != 1U) return 1;
    for (repeat = 0U; repeat < 100U; ++repeat) {
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &second, &same_generation) ||
            same_generation != generation ||
            second.build_count != first.build_count ||
            second.project_count != first.project_count) return 2;
    }
    sample.source_generation += 1ULL;
    if (!v5_program_scene_producer_build(
            &producer, &sample, &readback, &second, &same_generation) ||
        same_generation != generation ||
        second.native_generation != first.native_generation ||
        second.build_count != first.build_count ||
        second.project_count != first.project_count) return 22;
    {
        const uint64_t stable_generation = generation;
        const uint64_t stable_build_count = first.build_count;
        const uint64_t stable_transform_count = first.rtcp_transform_count;
        const uint64_t stable_project_count = first.project_count;
        for (repeat = 0U; repeat < 100U; ++repeat) {
            sample.source_generation += 1ULL;
            if (!v5_program_scene_producer_build(
                    &producer, &sample, &readback,
                    &second, &same_generation) ||
                same_generation != stable_generation ||
                second.build_count != stable_build_count ||
                second.rtcp_transform_count != stable_transform_count ||
                second.project_count != stable_project_count) return 27;
        }
    }
    for (repeat = 0U; repeat < 100U; ++repeat) {
        sample.source_generation += 1ULL;
        sample.mcs[0] += 0.01;
        memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
        v5_program_scene_producer_set_request(&producer, &request);
        if (!v5_program_scene_producer_build(
                &producer, &sample, &readback, &second, &generation) ||
            second.build_count != first.build_count ||
            second.rtcp_transform_count != first.rtcp_transform_count ||
            second.project_count != first.project_count) return 15;
    }
    sample.source_generation += 1ULL;
    prepare_readback(&readback, "XYZAC_TRT", 1);
    if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
        second.rtcp_transform_count != 1ULL ||
        (second.flags & V5_STATUS_SCENE_FLAG_RTCP) == 0U) return 3;
    sample.source_generation += 1ULL;
    prepare_readback(&readback, "XYZBC_TRT", 1);
    if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
        second.active_model_id != 2U || second.primary_axis != 'B' || second.child_axis != 'C' ||
        second.rtcp_transform_count != 2ULL) return 4;
    if (!isfinite(second.points[0].x) || !isfinite(second.points[0].y)) return 5;
    v5_program_scene_producer_init(&producer);
    v5_program_scene_request_init(&request);
    request.program_source_identity = 21ULL;
    request.program_generation = 9ULL;
    request.view_generation = 1ULL;
    request.fit_generation = 1ULL;
    request.point_count = 2U;
    request.program_wcs_mask = 1U;
    request.points[1].axis[0] = 10.0;
    request.points[1].axis[1] = 5.0;
    request.wcs_index[0] = 0;
    request.wcs_index[1] = 0;
    v5_program_scene_producer_set_request(&producer, &request);
    memset(&sample, 0, sizeof(sample));
    sample.available = 1;
    sample.valid_mask = V5_STATUS_VALID_MCS | V5_STATUS_VALID_CMD_MCS;
    sample.source_generation = 31ULL;
    memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
    prepare_readback(&readback, "XYZAC_TRT", 0);
    if (!v5_program_scene_producer_build(&producer, &sample, &readback, &first, &generation) ||
        first.fit_generation == 0ULL) return 6;
    {
        const uint64_t initial_fit_generation = first.fit_generation;
        const uint64_t initial_build_count = first.build_count;
        const uint64_t initial_project_count = first.project_count;
        uint64_t first_overflow_fit_generation;
        uint64_t second_overflow_fit_generation;
        const V5StatusSceneMarker *marker;
        sample.source_generation += 1ULL;
        sample.mcs[0] = 10000.0;
        memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
        if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
            second.fit_generation == initial_fit_generation) return 7;
        if (second.build_count != initial_build_count ||
            second.project_count != initial_project_count + 1ULL) return 16;
        marker = find_marker(&second, V5_STATUS_SCENE_MARKER_MCS_ACTUAL);
        if (!marker || marker->point.x < 0.0f || marker->point.x > request.width ||
            marker->point.y < 0.0f || marker->point.y > request.height) return 8;
        first_overflow_fit_generation = second.fit_generation;
        for (repeat = 0U; repeat < 100U; ++repeat) {
            sample.source_generation += 1ULL;
            if (!v5_program_scene_producer_build(
                    &producer, &sample, &readback, &second, &generation) ||
                second.fit_generation != first_overflow_fit_generation) return 9;
            if (second.build_count != initial_build_count ||
                second.project_count != initial_project_count + 1ULL) return 17;
        }
        sample.source_generation += 1ULL;
        sample.mcs[0] = 20000.0;
        memcpy(sample.cmd_mcs, sample.mcs, sizeof(sample.cmd_mcs));
        if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
            second.fit_generation == first_overflow_fit_generation) return 10;
        if (second.build_count != initial_build_count ||
            second.project_count != initial_project_count + 2ULL) return 18;
        marker = find_marker(&second, V5_STATUS_SCENE_MARKER_MCS_ACTUAL);
        if (!marker || marker->point.x < 0.0f || marker->point.x > request.width ||
            marker->point.y < 0.0f || marker->point.y > request.height) return 11;
        second_overflow_fit_generation = second.fit_generation;
        sample.source_generation += 1ULL;
        if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
            second.fit_generation != second_overflow_fit_generation) return 12;
        if (second.build_count != initial_build_count ||
            second.project_count != initial_project_count + 2ULL) return 19;
        request.view_generation += 1ULL;
        v5_program_scene_producer_set_request(&producer, &request);
        if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
            second.fit_generation != second_overflow_fit_generation) return 13;
        if (second.build_count != initial_build_count ||
            second.project_count != initial_project_count + 3ULL) return 20;
        request.view_generation += 1ULL;
        request.fit_generation += 1ULL;
        v5_program_scene_producer_set_request(&producer, &request);
        if (!v5_program_scene_producer_build(&producer, &sample, &readback, &second, &generation) ||
            second.fit_generation == second_overflow_fit_generation) return 14;
        if (second.build_count != initial_build_count ||
            second.project_count != initial_project_count + 4ULL) return 21;
    }
    return 0;
}
