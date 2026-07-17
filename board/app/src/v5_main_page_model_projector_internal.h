#ifndef V5_MAIN_PAGE_MODEL_PROJECTOR_INTERNAL_H
#define V5_MAIN_PAGE_MODEL_PROJECTOR_INTERNAL_H

#include "v5_main_page_model_projector.h"

typedef struct V5MainPageModelProjectorOps {
    unsigned int registry_id;
    int (*descriptor_supported)(const V5MotionModelDescriptor *model);
    int (*snapshot)(
        const V5MotionModelDescriptor *model,
        const V5NativeReadback *readback,
        const V5UiStatusView *status,
        V5MainPageModelScene *scene);
    void (*transform_world_point)(
        const V5MainPageModelScene *scene,
        double point[V5_STATUS_AXIS_COUNT]);
    int (*build_geometry)(
        const V5MainPageModelScene *scene,
        V5MainPageModelGeometry *geometry);
} V5MainPageModelProjectorOps;

extern const V5MainPageModelProjectorOps v5_main_page_model_projector_ac_ops;
extern const V5MainPageModelProjectorOps v5_main_page_model_projector_bc_ops;

int v5_main_page_model_copy_center(
    const V5NativeReadback *readback,
    unsigned int center_index,
    double center[V5_STATUS_AXIS_COUNT]);

#endif
