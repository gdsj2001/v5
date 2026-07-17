#include "v5_main_page_model_projector_internal.h"

#include <math.h>
#include <string.h>

static int v5_main_page_model_ac_descriptor_supported(
    const V5MotionModelDescriptor *model)
{
    return model &&
        model->registry_id == V5_MOTION_MODEL_ID_XYZAC_TRT &&
        model->first_rotary_axis == 'A' &&
        model->second_rotary_axis == 'C' &&
        model->first_status_slot == 3U &&
        model->second_status_slot == 4U &&
        model->first_g53_center == V5_NATIVE_READBACK_G53_CENTER_A &&
        model->second_g53_center == V5_NATIVE_READBACK_G53_CENTER_C &&
        model->first_center_wcs_component == 0U &&
        model->second_center_wcs_component == 2U;
}

static int v5_main_page_model_ac_snapshot(
    const V5MotionModelDescriptor *model,
    const V5NativeReadback *readback,
    const V5UiStatusView *status,
    V5MainPageModelScene *scene)
{
    const double *active_offsets;
    double primary_rad;
    double child_rad;

    if (!v5_main_page_model_ac_descriptor_supported(model) ||
        !readback || !status || !scene ||
        status->status_epoch == 0U ||
        (status->valid_mask & V5_STATUS_VALID_MCS) == 0U ||
        !isfinite(status->mcs[3]) ||
        !isfinite(status->mcs[4])) {
        return 0;
    }
    active_offsets = v5_native_readback_active_wcs_offsets(readback);
    if (!active_offsets ||
        !isfinite(active_offsets[0]) ||
        !isfinite(active_offsets[1]) ||
        !isfinite(active_offsets[2]) ||
        !v5_main_page_model_copy_center(
            readback,
            V5_NATIVE_READBACK_G53_CENTER_A,
            scene->primary_center) ||
        !v5_main_page_model_copy_center(
            readback,
            V5_NATIVE_READBACK_G53_CENTER_C,
            scene->child_center)) {
        return 0;
    }
    scene->registry_id = V5_MOTION_MODEL_ID_XYZAC_TRT;
    scene->status_epoch = status->status_epoch;
    scene->g53_geometry_epoch = readback->g53_geometry_epoch;
    scene->wcs_offsets_epoch = readback->wcs_offsets_epoch;
    scene->primary_axis = 'A';
    scene->child_axis = 'C';
    scene->primary_status_slot = 3U;
    scene->child_status_slot = 4U;
    scene->primary_deg = status->mcs[3];
    scene->child_deg = status->mcs[4];
    primary_rad = scene->primary_deg * 3.14159265358979323846 / 180.0;
    child_rad = scene->child_deg * 3.14159265358979323846 / 180.0;
    scene->primary_sin = sin(primary_rad);
    scene->primary_cos = cos(primary_rad);
    scene->child_sin = sin(child_rad);
    scene->child_cos = cos(child_rad);
    scene->primary_center[0] = active_offsets[0];
    scene->child_center[2] = active_offsets[2];
    return 1;
}

static void v5_main_page_model_ac_transform_world_point(
    const V5MainPageModelScene *scene,
    double point[V5_STATUS_AXIS_COUNT])
{
    const double x = point[0] - scene->child_center[0];
    const double y = point[1] - scene->child_center[1];
    const double c_x = scene->child_center[0] +
        (x * scene->child_cos) - (y * scene->child_sin);
    const double c_y = scene->child_center[1] +
        (x * scene->child_sin) + (y * scene->child_cos);
    const double a_y = c_y - scene->primary_center[1];
    const double a_z = point[2] - scene->primary_center[2];

    point[0] = c_x;
    point[1] = scene->primary_center[1] +
        (a_y * scene->primary_cos) - (a_z * scene->primary_sin);
    point[2] = scene->primary_center[2] +
        (a_y * scene->primary_sin) + (a_z * scene->primary_cos);
}

static int v5_main_page_model_ac_build_geometry(
    const V5MainPageModelScene *scene,
    V5MainPageModelGeometry *geometry)
{
    const double child_y = scene ? scene->child_center[1] - scene->primary_center[1] : 0.0;
    const double child_z = scene ? scene->child_center[2] - scene->primary_center[2] : 0.0;

    if (!scene || !geometry || scene->registry_id != V5_MOTION_MODEL_ID_XYZAC_TRT) {
        return 0;
    }
    geometry->primary_axis = 'A';
    geometry->child_axis = 'C';
    memcpy(geometry->primary_center, scene->primary_center, sizeof(geometry->primary_center));
    memcpy(geometry->child_center, scene->child_center, sizeof(geometry->child_center));
    geometry->primary_direction[0] = 1.0;
    geometry->child_center[1] = scene->primary_center[1] +
        (child_y * scene->primary_cos) - (child_z * scene->primary_sin);
    geometry->child_center[2] = scene->primary_center[2] +
        (child_y * scene->primary_sin) + (child_z * scene->primary_cos);
    geometry->child_direction[1] = -scene->primary_sin;
    geometry->child_direction[2] = scene->primary_cos;
    return 1;
}

const V5MainPageModelProjectorOps v5_main_page_model_projector_ac_ops = {
    V5_MOTION_MODEL_ID_XYZAC_TRT,
    v5_main_page_model_ac_descriptor_supported,
    v5_main_page_model_ac_snapshot,
    v5_main_page_model_ac_transform_world_point,
    v5_main_page_model_ac_build_geometry,
};
