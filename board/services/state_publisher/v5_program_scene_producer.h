#ifndef V5_PROGRAM_SCENE_PRODUCER_H
#define V5_PROGRAM_SCENE_PRODUCER_H

#include "v5_native_sample.h"
#include "v5_program_scene_ipc.h"
#include "v5_program_scene_model.h"

typedef struct V5ProgramSceneBounds {
    int valid;
    double min_u;
    double max_u;
    double min_v;
    double max_v;
} V5ProgramSceneBounds;

typedef struct V5ProgramSceneWorldSegment {
    V5StatusPoint start;
    V5StatusPoint end;
    uint16_t role;
    uint16_t index;
} V5ProgramSceneWorldSegment;

typedef struct V5ProgramSceneWorldMarker {
    V5StatusPoint point;
    uint16_t role;
    uint16_t index;
} V5ProgramSceneWorldMarker;

typedef struct V5ProgramSceneProducer {
    V5ProgramSceneRequest request;
    V5StatusDisplayScene scene;
    V5StatusPoint world_points[V5_STATUS_SCENE_POINT_COUNT];
    V5ProgramSceneWorldSegment world_segments[V5_STATUS_SCENE_SEGMENT_COUNT];
    V5ProgramSceneWorldMarker world_markers[V5_STATUS_SCENE_MARKER_COUNT];
    V5ProgramSceneModel model;
    V5ProgramSceneModel static_model;
    V5ProgramSceneBounds static_bounds;
    V5ProgramSceneBounds fit_bounds;
    double last_mcs[V5_STATUS_AXIS_COUNT];
    double last_cmd_mcs[V5_STATUS_AXIS_COUNT];
    uint64_t scene_generation;
    uint64_t fit_generation;
    uint64_t fit_program_source_identity;
    uint64_t fit_program_generation;
    uint64_t fit_request_generation;
    uint64_t last_source_generation;
    uint64_t last_request_fit_generation;
    uint64_t last_request_view_generation;
    uint64_t static_active_model_generation;
    uint64_t static_wcs_generation;
    uint64_t static_program_source_identity;
    uint64_t static_program_generation;
    uint64_t build_count;
    uint64_t transform_count;
    uint64_t project_count;
    double last_tool_length;
    float fit_width;
    float fit_height;
    uint32_t fit_plane;
    uint32_t static_program_wcs_mask;
    uint32_t static_rtcp_enabled;
    uint32_t static_point_count;
    uint32_t world_segment_count;
    uint32_t world_marker_count;
    uint32_t last_dynamic_valid_mask;
    uint32_t last_request_page_visible;
    int request_valid;
    int static_valid;
    int scene_valid;
    int last_tool_length_valid;
} V5ProgramSceneProducer;

void v5_program_scene_producer_init(V5ProgramSceneProducer *producer);
void v5_program_scene_producer_set_request(
    V5ProgramSceneProducer *producer,
    const V5ProgramSceneRequest *request);
int v5_program_scene_producer_build(
    V5ProgramSceneProducer *producer,
    const V5NativeDisplaySample *sample,
    const V5NativeReadback *readback,
    V5StatusDisplayScene *scene,
    uint64_t *scene_generation);

#endif
