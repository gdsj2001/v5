#include "v5_main_page_model_projector_internal.h"

#include <math.h>
#include <string.h>

static const V5MainPageModelProjectorOps *const v5_main_page_model_projectors[] = {
    &v5_main_page_model_projector_ac_ops,
    &v5_main_page_model_projector_bc_ops,
};

static const V5MainPageModelProjectorOps *v5_main_page_model_projector_find(
    const V5MotionModelDescriptor *model)
{
    const V5MainPageModelProjectorOps *found = 0;
    size_t i;

    if (!model || model->registry_id == 0U) {
        return 0;
    }
    for (i = 0U; i < v5_main_page_model_projector_registered_count(); ++i) {
        const V5MainPageModelProjectorOps *candidate = v5_main_page_model_projectors[i];
        if (!candidate || candidate->registry_id != model->registry_id) {
            continue;
        }
        if (found) {
            return 0;
        }
        found = candidate;
    }
    return found;
}

size_t v5_main_page_model_projector_registered_count(void)
{
    return sizeof(v5_main_page_model_projectors) / sizeof(v5_main_page_model_projectors[0]);
}

int v5_main_page_model_projector_descriptor_supported(
    const V5MotionModelDescriptor *model)
{
    const V5MainPageModelProjectorOps *ops = v5_main_page_model_projector_find(model);

    return ops && ops->descriptor_supported &&
        ops->descriptor_supported(model);
}

int v5_main_page_model_scene_resolve(
    const V5MotionModelDescriptor *model,
    const V5NativeReadback *readback,
    const V5UiStatusView *status,
    V5MainPageModelScene *scene)
{
    const V5MainPageModelProjectorOps *ops;

    if (!scene) {
        return 0;
    }
    memset(scene, 0, sizeof(*scene));
    ops = v5_main_page_model_projector_find(model);
    if (!ops || !ops->descriptor_supported ||
        !ops->descriptor_supported(model) ||
        !ops->snapshot || !ops->transform_world_point || !ops->build_geometry ||
        !ops->snapshot(model, readback, status, scene) ||
        scene->registry_id != model->registry_id ||
        ops->registry_id != model->registry_id) {
        memset(scene, 0, sizeof(*scene));
        return 0;
    }
    scene->ops = ops;
    return 1;
}

int v5_main_page_model_scene_transform_world_point(
    const V5MainPageModelScene *scene,
    double point[V5_STATUS_AXIS_COUNT])
{
    if (!scene || !point || !scene->ops ||
        scene->ops->registry_id != scene->registry_id ||
        !scene->ops->transform_world_point) {
        return 0;
    }
    scene->ops->transform_world_point(scene, point);
    return 1;
}

int v5_main_page_model_scene_build_geometry(
    const V5MainPageModelScene *scene,
    V5MainPageModelGeometry *geometry)
{
    if (!scene || !geometry || !scene->ops ||
        scene->ops->registry_id != scene->registry_id ||
        !scene->ops->build_geometry) {
        return 0;
    }
    memset(geometry, 0, sizeof(*geometry));
    return scene->ops->build_geometry(scene, geometry);
}

int v5_main_page_model_scene_pose_equal(
    const V5MainPageModelScene *lhs,
    const V5MainPageModelScene *rhs,
    double tolerance)
{
    unsigned int i;

    if (!lhs || !rhs || !lhs->ops || !rhs->ops ||
        lhs->registry_id != rhs->registry_id ||
        lhs->ops != rhs->ops ||
        lhs->primary_axis != rhs->primary_axis ||
        lhs->child_axis != rhs->child_axis ||
        lhs->primary_status_slot != rhs->primary_status_slot ||
        lhs->child_status_slot != rhs->child_status_slot ||
        !isfinite(tolerance) || tolerance < 0.0 ||
        fabs(lhs->primary_deg - rhs->primary_deg) > tolerance ||
        fabs(lhs->child_deg - rhs->child_deg) > tolerance) {
        return 0;
    }
    for (i = 0U; i < V5_STATUS_AXIS_COUNT; ++i) {
        if (fabs(lhs->primary_center[i] - rhs->primary_center[i]) > tolerance ||
            fabs(lhs->child_center[i] - rhs->child_center[i]) > tolerance) {
            return 0;
        }
    }
    return 1;
}

void v5_main_page_model_rotate_point_about_axis(
    double point[V5_STATUS_AXIS_COUNT],
    const double center[V5_STATUS_AXIS_COUNT],
    const double axis[3],
    double angle_deg)
{
    double vx;
    double vy;
    double vz;
    double kx;
    double ky;
    double kz;
    double norm;
    double angle_rad;
    double c;
    double s;
    double dot;
    double cross_x;
    double cross_y;
    double cross_z;

    if (!point || !center || !axis || !isfinite(angle_deg)) {
        return;
    }
    norm = sqrt((axis[0] * axis[0]) + (axis[1] * axis[1]) + (axis[2] * axis[2]));
    if (norm <= 1.0e-12 || !isfinite(norm)) {
        return;
    }
    kx = axis[0] / norm;
    ky = axis[1] / norm;
    kz = axis[2] / norm;
    vx = point[0] - center[0];
    vy = point[1] - center[1];
    vz = point[2] - center[2];
    angle_rad = angle_deg * 3.14159265358979323846 / 180.0;
    c = cos(angle_rad);
    s = sin(angle_rad);
    dot = (kx * vx) + (ky * vy) + (kz * vz);
    cross_x = (ky * vz) - (kz * vy);
    cross_y = (kz * vx) - (kx * vz);
    cross_z = (kx * vy) - (ky * vx);
    point[0] = center[0] + (vx * c) + (cross_x * s) + (kx * dot * (1.0 - c));
    point[1] = center[1] + (vy * c) + (cross_y * s) + (ky * dot * (1.0 - c));
    point[2] = center[2] + (vz * c) + (cross_z * s) + (kz * dot * (1.0 - c));
}

int v5_main_page_model_copy_center(
    const V5NativeReadback *readback,
    unsigned int center_index,
    double center[V5_STATUS_AXIS_COUNT])
{
    const double *source;
    unsigned int i;

    if (!readback || !center) {
        return 0;
    }
    source = v5_native_readback_g53_center(readback, center_index);
    if (!source) {
        return 0;
    }
    memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
    for (i = 0U; i < V5_NATIVE_READBACK_G53_AXIS_COUNT; ++i) {
        if (!isfinite(source[i])) {
            memset(center, 0, sizeof(double) * V5_STATUS_AXIS_COUNT);
            return 0;
        }
        center[i] = source[i];
    }
    return 1;
}
