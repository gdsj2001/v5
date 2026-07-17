#ifndef V5_MAIN_PAGE_MODEL_PROJECTOR_H
#define V5_MAIN_PAGE_MODEL_PROJECTOR_H

#include "v5_motion_model_registry.h"
#include "v5_native_readback.h"
#include "v5_ui_status_view.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct V5MainPageModelProjectorOps;

typedef struct V5MainPageModelScene {
    const struct V5MainPageModelProjectorOps *ops;
    unsigned int registry_id;
    uint64_t status_epoch;
    unsigned int g53_geometry_epoch;
    unsigned int wcs_offsets_epoch;
    char primary_axis;
    char child_axis;
    unsigned int primary_status_slot;
    unsigned int child_status_slot;
    double primary_deg;
    double child_deg;
    double primary_sin;
    double primary_cos;
    double child_sin;
    double child_cos;
    double primary_center[V5_STATUS_AXIS_COUNT];
    double child_center[V5_STATUS_AXIS_COUNT];
} V5MainPageModelScene;

typedef struct V5MainPageModelGeometry {
    char primary_axis;
    char child_axis;
    double primary_center[V5_STATUS_AXIS_COUNT];
    double child_center[V5_STATUS_AXIS_COUNT];
    double primary_direction[3];
    double child_direction[3];
} V5MainPageModelGeometry;

size_t v5_main_page_model_projector_registered_count(void);

int v5_main_page_model_projector_descriptor_supported(
    const V5MotionModelDescriptor *model);

int v5_main_page_model_scene_resolve(
    const V5MotionModelDescriptor *model,
    const V5NativeReadback *readback,
    const V5UiStatusView *status,
    V5MainPageModelScene *scene);

int v5_main_page_model_scene_transform_world_point(
    const V5MainPageModelScene *scene,
    double point[V5_STATUS_AXIS_COUNT]);

int v5_main_page_model_scene_build_geometry(
    const V5MainPageModelScene *scene,
    V5MainPageModelGeometry *geometry);

int v5_main_page_model_scene_pose_equal(
    const V5MainPageModelScene *lhs,
    const V5MainPageModelScene *rhs,
    double tolerance);

#ifdef __cplusplus
}
#endif

#endif
