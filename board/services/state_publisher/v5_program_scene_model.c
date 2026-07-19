#include "v5_program_scene_model_internal.h"

#include "v5_motion_model_registry.h"

#include <math.h>
#include <string.h>

static const V5ProgramSceneModelOps *const model_ops[] = {
    &v5_program_scene_model_ac_ops,
    &v5_program_scene_model_bc_ops,
};

static const V5ProgramSceneModelOps *find_ops(uint32_t registry_id)
{
    const V5ProgramSceneModelOps *found = 0;
    size_t i;
    for (i = 0U; i < sizeof(model_ops) / sizeof(model_ops[0]); ++i) {
        if (model_ops[i] && model_ops[i]->registry_id == registry_id) {
            if (found) return 0;
            found = model_ops[i];
        }
    }
    return found;
}

int v5_program_scene_model_copy_center(
    const V5NativeReadback *readback,
    unsigned int center_index,
    double center[V5_STATUS_AXIS_COUNT])
{
    const double *source;
    unsigned int i;
    if (!readback || !center) return 0;
    source = v5_native_readback_g53_center(readback, center_index);
    if (!source) return 0;
    memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
    for (i = 0U; i < V5_NATIVE_READBACK_G53_AXIS_COUNT; ++i) {
        if (!isfinite(source[i])) return 0;
        center[i] = source[i];
    }
    return 1;
}

int v5_program_scene_model_resolve(
    const V5NativeReadback *readback,
    const V5StatusPoint *mcs,
    V5ProgramSceneModel *model)
{
    const V5MotionModelDescriptor *descriptor;
    const V5ProgramSceneModelOps *ops;
    if (!readback || !mcs || !model ||
        !v5_native_readback_motion_model_known(readback)) return 0;
    descriptor = v5_motion_model_find(readback->motion_model);
    ops = descriptor ? find_ops(descriptor->registry_id) : 0;
    memset(model, 0, sizeof(*model));
    return ops && ops->resolve && ops->resolve(readback, mcs, model) &&
        model->registry_id == ops->registry_id;
}

int v5_program_scene_model_transform(
    const V5ProgramSceneModel *model,
    double point[V5_STATUS_AXIS_COUNT])
{
    const V5ProgramSceneModelOps *ops = model ? find_ops(model->registry_id) : 0;
    if (!ops || !ops->transform || !point) return 0;
    ops->transform(model, point);
    return 1;
}

int v5_program_scene_model_geometry(
    const V5ProgramSceneModel *model,
    V5ProgramSceneModelGeometry *geometry)
{
    const V5ProgramSceneModelOps *ops = model ? find_ops(model->registry_id) : 0;
    if (!ops || !ops->geometry || !geometry) return 0;
    memset(geometry, 0, sizeof(*geometry));
    return ops->geometry(model, geometry);
}
