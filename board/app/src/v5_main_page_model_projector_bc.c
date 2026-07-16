#include "v5_main_page_model_projector_internal.h"

#include <math.h>
#include <string.h>

static int v5_main_page_model_bc_descriptor_supported(
    const V5MotionModelDescriptor *model)
{
    return model &&
        model->registry_id == V5_MOTION_MODEL_ID_XYZBC_TRT &&
        model->first_rotary_axis == 'B' &&
        model->second_rotary_axis == 'C' &&
        model->first_status_slot == 3U &&
        model->second_status_slot == 4U &&
        model->first_g53_center == V5_NATIVE_READBACK_G53_CENTER_B &&
        model->second_g53_center == V5_NATIVE_READBACK_G53_CENTER_C &&
        model->first_center_wcs_component == 1U &&
        model->second_center_wcs_component == 2U;
}

static int v5_main_page_model_bc_snapshot(
    const V5MotionModelDescriptor *model,
    const V5NativeReadback *readback,
    const V5UiStatusView *status,
    V5MainPageModelScene *scene)
{
    const double *active_offsets;

    if (!v5_main_page_model_bc_descriptor_supported(model) ||
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
            V5_NATIVE_READBACK_G53_CENTER_B,
            scene->primary_center) ||
        !v5_main_page_model_copy_center(
            readback,
            V5_NATIVE_READBACK_G53_CENTER_C,
            scene->child_center)) {
        return 0;
    }
    scene->registry_id = V5_MOTION_MODEL_ID_XYZBC_TRT;
    scene->status_epoch = status->status_epoch;
    scene->g53_geometry_epoch = readback->g53_geometry_epoch;
    scene->wcs_offsets_epoch = readback->wcs_offsets_epoch;
    scene->primary_axis = 'B';
    scene->child_axis = 'C';
    scene->primary_status_slot = 3U;
    scene->child_status_slot = 4U;
    scene->primary_deg = status->mcs[3];
    scene->child_deg = status->mcs[4];
    scene->primary_center[1] = active_offsets[1];
    scene->child_center[2] = active_offsets[2];
    return 1;
}

static void v5_main_page_model_bc_transform_world_point(
    const V5MainPageModelScene *scene,
    double point[V5_STATUS_AXIS_COUNT])
{
    static const double c_axis[3] = {0.0, 0.0, 1.0};
    static const double b_axis[3] = {0.0, 1.0, 0.0};

    v5_main_page_model_rotate_point_about_axis(
        point,
        scene->child_center,
        c_axis,
        scene->child_deg);
    v5_main_page_model_rotate_point_about_axis(
        point,
        scene->primary_center,
        b_axis,
        scene->primary_deg);
}

static int v5_main_page_model_bc_build_geometry(
    const V5MainPageModelScene *scene,
    V5MainPageModelGeometry *geometry)
{
    static const double b_axis[3] = {0.0, 1.0, 0.0};
    double child_tip[V5_STATUS_AXIS_COUNT];
    unsigned int i;

    if (!scene || !geometry || scene->registry_id != V5_MOTION_MODEL_ID_XYZBC_TRT) {
        return 0;
    }
    geometry->primary_axis = 'B';
    geometry->child_axis = 'C';
    memcpy(geometry->primary_center, scene->primary_center, sizeof(geometry->primary_center));
    memcpy(geometry->child_center, scene->child_center, sizeof(geometry->child_center));
    geometry->primary_direction[1] = 1.0;
    child_tip[0] = scene->child_center[0];
    child_tip[1] = scene->child_center[1];
    child_tip[2] = scene->child_center[2] + 1.0;
    child_tip[3] = 0.0;
    child_tip[4] = 0.0;
    v5_main_page_model_rotate_point_about_axis(
        geometry->child_center,
        scene->primary_center,
        b_axis,
        scene->primary_deg);
    v5_main_page_model_rotate_point_about_axis(
        child_tip,
        scene->primary_center,
        b_axis,
        scene->primary_deg);
    for (i = 0U; i < 3U; ++i) {
        geometry->child_direction[i] = child_tip[i] - geometry->child_center[i];
    }
    return 1;
}

const V5MainPageModelProjectorOps v5_main_page_model_projector_bc_ops = {
    V5_MOTION_MODEL_ID_XYZBC_TRT,
    v5_main_page_model_bc_descriptor_supported,
    v5_main_page_model_bc_snapshot,
    v5_main_page_model_bc_transform_world_point,
    v5_main_page_model_bc_build_geometry,
};
