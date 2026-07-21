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

int v5_program_scene_model_pose_matrix(
    const V5ProgramSceneModel *model,
    V5ProgramScenePoseMatrix *matrix)
{
    const V5ProgramSceneModelOps *ops = model ? find_ops(model->registry_id) : 0;
    return ops && ops->pose_matrix && matrix && ops->pose_matrix(model, matrix);
}

void v5_program_scene_pose_matrix_identity(V5ProgramScenePoseMatrix *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->value[0][0] = 1.0;
    matrix->value[1][1] = 1.0;
    matrix->value[2][2] = 1.0;
}

void v5_program_scene_pose_matrix_apply(
    const V5ProgramScenePoseMatrix *matrix,
    const double point[V5_STATUS_AXIS_COUNT],
    double transformed[V5_STATUS_AXIS_COUNT])
{
    unsigned int row;
    if (!matrix || !point || !transformed) return;
    memcpy(transformed, point, sizeof(double) * V5_STATUS_AXIS_COUNT);
    for (row = 0U; row < 3U; ++row) {
        transformed[row] = matrix->value[row][0] * point[0] +
            matrix->value[row][1] * point[1] +
            matrix->value[row][2] * point[2] + matrix->value[row][3];
    }
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

int v5_program_scene_model_topology_same(
    const V5ProgramSceneModel *left,
    const V5ProgramSceneModel *right)
{
    return left && right &&
        left->registry_id == right->registry_id &&
        left->primary_axis == right->primary_axis &&
        left->child_axis == right->child_axis &&
        memcmp(
            left->primary_center,
            right->primary_center,
            sizeof(left->primary_center)) == 0 &&
        memcmp(
            left->child_center,
            right->child_center,
            sizeof(left->child_center)) == 0;
}

int v5_program_scene_model_pose_same(
    const V5ProgramSceneModel *left,
    const V5ProgramSceneModel *right)
{
    return left && right &&
        left->registry_id == right->registry_id &&
        left->primary_sin == right->primary_sin &&
        left->primary_cos == right->primary_cos &&
        left->child_sin == right->child_sin &&
        left->child_cos == right->child_cos;
}

int v5_program_scene_model_registry_complete(void)
{
    size_t index;
    if (!v5_motion_model_registry_valid()) return 0;
    for (index = 0U; index < v5_motion_model_registry_count(); ++index) {
        const V5MotionModelDescriptor *descriptor =
            v5_motion_model_registry_at(index);
        const V5ProgramSceneModelOps *ops = descriptor ?
            find_ops(descriptor->registry_id) : 0;
        if (!descriptor || !ops || !ops->resolve || !ops->pose_matrix ||
            !ops->geometry || ops->registry_id != descriptor->registry_id) {
            return 0;
        }
    }
    return 1;
}
