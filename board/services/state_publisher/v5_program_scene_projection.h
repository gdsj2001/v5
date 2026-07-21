#ifndef V5_PROGRAM_SCENE_PROJECTION_H
#define V5_PROGRAM_SCENE_PROJECTION_H

#include "v5_program_scene_producer.h"

void v5_program_scene_bounds_add(
    V5ProgramSceneBounds *bounds,
    const double axis[V5_STATUS_AXIS_COUNT],
    uint32_t plane);
int v5_program_scene_bounds_outside_viewport(
    const V5ProgramSceneBounds *candidate,
    const V5ProgramSceneBounds *frozen,
    const V5ProgramSceneRequest *request);
int v5_program_scene_fit_key_changed(
    const V5ProgramSceneProducer *producer,
    const V5ProgramSceneRequest *request);
void v5_program_scene_commit_fit(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneBounds *bounds);
uint64_t v5_program_scene_active_model_generation(
    const V5NativeReadback *readback,
    uint32_t model_id);
int v5_program_scene_static_key_same(
    const V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id);
int v5_program_scene_build_static_cache(
    V5ProgramSceneProducer *producer,
    const V5NativeReadback *readback,
    const V5ProgramSceneModel *model,
    uint32_t model_id);
int v5_program_scene_pose_cache_same(
    const V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t rtcp_enabled);
int v5_program_scene_prepare_pose_cache(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneModel *model,
    uint32_t rtcp_enabled);
int v5_program_scene_transform_project_program(
    V5ProgramSceneProducer *producer,
    int emit_scene);
void v5_program_scene_prepare_dynamic_update(
    V5ProgramSceneProducer *producer);
void v5_program_scene_add_dynamic_segment(
    V5ProgramSceneProducer *producer,
    const double start[V5_STATUS_AXIS_COUNT],
    const double end[V5_STATUS_AXIS_COUNT],
    uint16_t role);
void v5_program_scene_add_dynamic_marker(
    V5ProgramSceneProducer *producer,
    const double point[V5_STATUS_AXIS_COUNT],
    uint16_t role);

#endif
