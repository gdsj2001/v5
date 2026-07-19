#ifndef V5_PROGRAM_SCENE_MODEL_H
#define V5_PROGRAM_SCENE_MODEL_H

#include "v5_native_readback.h"
#include "v5_status_shm.h"

#include <stdint.h>

typedef struct V5ProgramSceneModel {
    uint32_t registry_id;
    uint8_t primary_axis;
    uint8_t child_axis;
    uint16_t reserved;
    double primary_sin;
    double primary_cos;
    double child_sin;
    double child_cos;
    double primary_center[V5_STATUS_AXIS_COUNT];
    double child_center[V5_STATUS_AXIS_COUNT];
} V5ProgramSceneModel;

typedef struct V5ProgramSceneModelGeometry {
    uint8_t primary_axis;
    uint8_t child_axis;
    uint16_t reserved;
    double primary_center[V5_STATUS_AXIS_COUNT];
    double child_center[V5_STATUS_AXIS_COUNT];
    double primary_direction[3];
    double child_direction[3];
} V5ProgramSceneModelGeometry;

int v5_program_scene_model_resolve(
    const V5NativeReadback *readback,
    const V5StatusPoint *mcs,
    V5ProgramSceneModel *model);
int v5_program_scene_model_transform(
    const V5ProgramSceneModel *model,
    double point[V5_STATUS_AXIS_COUNT]);
int v5_program_scene_model_geometry(
    const V5ProgramSceneModel *model,
    V5ProgramSceneModelGeometry *geometry);

#endif
