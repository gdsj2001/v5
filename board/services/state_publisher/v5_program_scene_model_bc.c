#include "v5_program_scene_model_internal.h"

#include "v5_motion_model_registry.h"

#include <math.h>
#include <string.h>

static int resolve_bc(
    const V5NativeReadback *readback,
    const V5StatusPoint *mcs,
    V5ProgramSceneModel *model)
{
    const double *wcs;
    double b;
    double c;
    if (!readback || !mcs || !model || !isfinite(mcs->axis[3]) ||
        !isfinite(mcs->axis[4])) return 0;
    wcs = v5_native_readback_active_wcs_offsets(readback);
    if (!wcs || !v5_program_scene_model_copy_center(readback, 1U, model->primary_center) ||
        !v5_program_scene_model_copy_center(readback, 2U, model->child_center)) return 0;
    model->registry_id = V5_MOTION_MODEL_ID_XYZBC_TRT;
    model->primary_axis = 'B';
    model->child_axis = 'C';
    model->primary_center[1] = wcs[1];
    model->child_center[2] = wcs[2];
    b = mcs->axis[3] * 3.14159265358979323846 / 180.0;
    c = mcs->axis[4] * 3.14159265358979323846 / 180.0;
    model->primary_sin = sin(b); model->primary_cos = cos(b);
    model->child_sin = sin(c); model->child_cos = cos(c);
    return 1;
}

static void transform_bc(const V5ProgramSceneModel *model, double point[V5_STATUS_AXIS_COUNT])
{
    const double x = point[0] - model->child_center[0];
    const double y = point[1] - model->child_center[1];
    const double cx = model->child_center[0] + x * model->child_cos - y * model->child_sin;
    const double cy = model->child_center[1] + x * model->child_sin + y * model->child_cos;
    const double bx = cx - model->primary_center[0];
    const double bz = point[2] - model->primary_center[2];
    point[0] = model->primary_center[0] + bx * model->primary_cos + bz * model->primary_sin;
    point[1] = cy;
    point[2] = model->primary_center[2] - bx * model->primary_sin + bz * model->primary_cos;
}

static int geometry_bc(const V5ProgramSceneModel *model, V5ProgramSceneModelGeometry *geometry)
{
    const double dx = model->child_center[0] - model->primary_center[0];
    const double dz = model->child_center[2] - model->primary_center[2];
    memcpy(geometry->primary_center, model->primary_center, sizeof(geometry->primary_center));
    memcpy(geometry->child_center, model->child_center, sizeof(geometry->child_center));
    geometry->primary_axis = 'B'; geometry->child_axis = 'C';
    geometry->primary_direction[1] = 1.0;
    geometry->child_center[0] = model->primary_center[0] + dx * model->primary_cos + dz * model->primary_sin;
    geometry->child_center[2] = model->primary_center[2] - dx * model->primary_sin + dz * model->primary_cos;
    geometry->child_direction[0] = model->primary_sin;
    geometry->child_direction[2] = model->primary_cos;
    return 1;
}

const V5ProgramSceneModelOps v5_program_scene_model_bc_ops = {
    V5_MOTION_MODEL_ID_XYZBC_TRT, resolve_bc, transform_bc, geometry_bc};
