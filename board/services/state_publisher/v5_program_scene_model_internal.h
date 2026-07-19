#ifndef V5_PROGRAM_SCENE_MODEL_INTERNAL_H
#define V5_PROGRAM_SCENE_MODEL_INTERNAL_H

#include "v5_program_scene_model.h"

typedef struct V5ProgramSceneModelOps {
    uint32_t registry_id;
    int (*resolve)(const V5NativeReadback *, const V5StatusPoint *, V5ProgramSceneModel *);
    void (*transform)(const V5ProgramSceneModel *, double[V5_STATUS_AXIS_COUNT]);
    int (*geometry)(const V5ProgramSceneModel *, V5ProgramSceneModelGeometry *);
} V5ProgramSceneModelOps;

extern const V5ProgramSceneModelOps v5_program_scene_model_ac_ops;
extern const V5ProgramSceneModelOps v5_program_scene_model_bc_ops;

int v5_program_scene_model_copy_center(
    const V5NativeReadback *readback,
    unsigned int center_index,
    double center[V5_STATUS_AXIS_COUNT]);

#endif
