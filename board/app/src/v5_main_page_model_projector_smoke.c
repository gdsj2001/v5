#include "v5_main_page_model_projector.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int nearly_equal(double lhs, double rhs)
{
    return fabs(lhs - rhs) <= 0.000001;
}

static int point_equal(const double lhs[V5_STATUS_AXIS_COUNT], const double rhs[3])
{
    return nearly_equal(lhs[0], rhs[0]) &&
        nearly_equal(lhs[1], rhs[1]) &&
        nearly_equal(lhs[2], rhs[2]);
}

static void expected_ac(
    const double input[3],
    const double a_center[3],
    const double c_center[3],
    double a_deg,
    double c_deg,
    double output[3])
{
    const double a = a_deg * 3.14159265358979323846 / 180.0;
    const double c = c_deg * 3.14159265358979323846 / 180.0;
    const double cx = c_center[0] + ((input[0] - c_center[0]) * cos(c)) -
        ((input[1] - c_center[1]) * sin(c));
    const double cy = c_center[1] + ((input[0] - c_center[0]) * sin(c)) +
        ((input[1] - c_center[1]) * cos(c));
    const double cz = input[2];

    output[0] = cx;
    output[1] = a_center[1] + ((cy - a_center[1]) * cos(a)) -
        ((cz - a_center[2]) * sin(a));
    output[2] = a_center[2] + ((cy - a_center[1]) * sin(a)) +
        ((cz - a_center[2]) * cos(a));
}

static void expected_bc(
    const double input[3],
    const double b_center[3],
    const double c_center[3],
    double b_deg,
    double c_deg,
    double output[3])
{
    const double b = b_deg * 3.14159265358979323846 / 180.0;
    const double c = c_deg * 3.14159265358979323846 / 180.0;
    const double cx = c_center[0] + ((input[0] - c_center[0]) * cos(c)) -
        ((input[1] - c_center[1]) * sin(c));
    const double cy = c_center[1] + ((input[0] - c_center[0]) * sin(c)) +
        ((input[1] - c_center[1]) * cos(c));
    const double cz = input[2];

    output[0] = b_center[0] + ((cx - b_center[0]) * cos(b)) +
        ((cz - b_center[2]) * sin(b));
    output[1] = cy;
    output[2] = b_center[2] - ((cx - b_center[0]) * sin(b)) +
        ((cz - b_center[2]) * cos(b));
}

static void prepare_readback(V5NativeReadback *readback)
{
    double offsets[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT];
    const double centers[V5_NATIVE_READBACK_G53_CENTER_COUNT][V5_NATIVE_READBACK_G53_AXIS_COUNT] = {
        {1.0, 20.0, -50.0},
        {7.0, 8.0, -25.0},
        {50.0, 20.0, 3.0},
    };

    memset(offsets, 0, sizeof(offsets));
    offsets[0][0] = 10.0;
    offsets[0][1] = 12.0;
    offsets[0][2] = 8.0;
    v5_native_readback_init(readback);
    v5_native_readback_set_wcs_table(
        readback,
        0,
        &offsets[0][0],
        V5_NATIVE_READBACK_WCS_COUNT,
        V5_NATIVE_READBACK_WCS_AXIS_COUNT,
        31U);
    v5_native_readback_set_g53_geometry(
        readback,
        &centers[0][0],
        V5_NATIVE_READBACK_G53_CENTER_COUNT,
        V5_NATIVE_READBACK_G53_AXIS_COUNT,
        41U);
}

int main(void)
{
    const V5MotionModelDescriptor *ac = v5_motion_model_find("XYZAC_TRT");
    const V5MotionModelDescriptor *bc = v5_motion_model_find("XYZBC_TRT");
    V5MotionModelDescriptor invalid;
    V5NativeReadback readback;
    V5UiStatusView status;
    V5MainPageModelScene ac_scene;
    V5MainPageModelScene bc_scene;
    V5MainPageModelScene invalid_scene;
    V5MainPageModelScene alternate_child_scene;
    V5MainPageModelGeometry geometry;
    V5MainPageModelGeometry alternate_child_geometry;
    double point[V5_STATUS_AXIS_COUNT] = {65.0, 35.0, 22.0, 0.0, 0.0};
    double expected[3];
    double input[3] = {65.0, 35.0, 22.0};
    double ac_center[3] = {10.0, 20.0, -50.0};
    double bc_center[3] = {7.0, 12.0, -25.0};
    double c_center[3] = {50.0, 20.0, 8.0};
    double posed_child_center[3];
    double a;
    double b;

    if (!ac || !bc ||
        !v5_main_page_model_projector_descriptor_supported(ac) ||
        !v5_main_page_model_projector_descriptor_supported(bc) ||
        v5_motion_model_registry_count() != v5_main_page_model_projector_registered_count()) {
        return 1;
    }
    prepare_readback(&readback);
    memset(&status, 0, sizeof(status));
    status.valid_mask = V5_STATUS_VALID_MCS;
    status.status_epoch = 51U;
    status.mcs[3] = 30.0;
    status.mcs[4] = 40.0;

    if (!v5_main_page_model_scene_resolve(ac, &readback, &status, &ac_scene) ||
        ac_scene.registry_id != V5_MOTION_MODEL_ID_XYZAC_TRT ||
        ac_scene.primary_axis != 'A' ||
        ac_scene.child_axis != 'C' ||
        ac_scene.primary_status_slot != 3U ||
        ac_scene.child_status_slot != 4U ||
        !point_equal(ac_scene.primary_center, ac_center) ||
        !point_equal(ac_scene.child_center, c_center)) {
        return 2;
    }
    expected_ac(input, ac_center, c_center, status.mcs[3], status.mcs[4], expected);
    if (!v5_main_page_model_scene_transform_world_point(&ac_scene, point) ||
        !point_equal(point, expected)) {
        return 3;
    }
    if (!v5_main_page_model_scene_build_geometry(&ac_scene, &geometry) ||
        geometry.primary_axis != 'A' ||
        geometry.child_axis != 'C' ||
        !nearly_equal(geometry.primary_direction[0], 1.0)) {
        return 4;
    }
    expected_ac(
        c_center,
        ac_center,
        c_center,
        status.mcs[3],
        0.0,
        posed_child_center);
    if (!point_equal(geometry.child_center, posed_child_center)) {
        return 5;
    }
    a = status.mcs[3] * 3.14159265358979323846 / 180.0;
    if (!nearly_equal(geometry.child_direction[0], 0.0) ||
        !nearly_equal(geometry.child_direction[1], -sin(a)) ||
        !nearly_equal(geometry.child_direction[2], cos(a))) {
        return 6;
    }
    alternate_child_scene = ac_scene;
    alternate_child_scene.child_deg += 73.0;
    if (!v5_main_page_model_scene_build_geometry(
            &alternate_child_scene,
            &alternate_child_geometry) ||
        !point_equal(alternate_child_geometry.child_center, posed_child_center) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[0],
            geometry.child_direction[0]) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[1],
            geometry.child_direction[1]) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[2],
            geometry.child_direction[2])) {
        return 7;
    }

    memcpy(point, input, sizeof(input));
    point[3] = 0.0;
    point[4] = 0.0;
    if (!v5_main_page_model_scene_resolve(bc, &readback, &status, &bc_scene) ||
        bc_scene.registry_id != V5_MOTION_MODEL_ID_XYZBC_TRT ||
        bc_scene.primary_axis != 'B' ||
        bc_scene.child_axis != 'C' ||
        bc_scene.primary_status_slot != 3U ||
        bc_scene.child_status_slot != 4U ||
        !point_equal(bc_scene.primary_center, bc_center) ||
        !point_equal(bc_scene.child_center, c_center)) {
        return 8;
    }
    expected_bc(input, bc_center, c_center, status.mcs[3], status.mcs[4], expected);
    if (!v5_main_page_model_scene_transform_world_point(&bc_scene, point) ||
        !point_equal(point, expected) ||
        v5_main_page_model_scene_pose_equal(&ac_scene, &bc_scene, 0.0005)) {
        return 9;
    }
    if (!v5_main_page_model_scene_build_geometry(&bc_scene, &geometry) ||
        geometry.primary_axis != 'B' ||
        geometry.child_axis != 'C' ||
        !nearly_equal(geometry.primary_direction[1], 1.0)) {
        return 10;
    }
    expected_bc(
        c_center,
        bc_center,
        c_center,
        status.mcs[3],
        0.0,
        posed_child_center);
    if (!point_equal(geometry.child_center, posed_child_center)) {
        return 11;
    }
    b = status.mcs[3] * 3.14159265358979323846 / 180.0;
    if (!nearly_equal(geometry.child_direction[0], sin(b)) ||
        !nearly_equal(geometry.child_direction[1], 0.0) ||
        !nearly_equal(geometry.child_direction[2], cos(b))) {
        return 12;
    }
    alternate_child_scene = bc_scene;
    alternate_child_scene.child_deg -= 61.0;
    if (!v5_main_page_model_scene_build_geometry(
            &alternate_child_scene,
            &alternate_child_geometry) ||
        !point_equal(alternate_child_geometry.child_center, posed_child_center) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[0],
            geometry.child_direction[0]) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[1],
            geometry.child_direction[1]) ||
        !nearly_equal(
            alternate_child_geometry.child_direction[2],
            geometry.child_direction[2])) {
        return 13;
    }

    invalid = *ac;
    invalid.registry_id = 99U;
    if (v5_main_page_model_projector_descriptor_supported(&invalid) ||
        v5_main_page_model_scene_resolve(&invalid, &readback, &status, &invalid_scene)) {
        return 14;
    }
    invalid = *ac;
    invalid.first_rotary_axis = 'B';
    if (v5_main_page_model_projector_descriptor_supported(&invalid) ||
        v5_main_page_model_scene_resolve(&invalid, &readback, &status, &invalid_scene)) {
        return 15;
    }
    v5_native_readback_set_motion_model(&readback, "XYZAC_TRT");
    v5_native_readback_set_g53_geometry_stale(&readback);
    v5_native_readback_set_motion_model(&readback, "XYZBC_TRT");
    if (!readback.g53_geometry_stale ||
        readback.g53_geometry_available ||
        readback.g53_geometry_epoch != 0U ||
        !readback.motion_model_available ||
        readback.motion_model[0] == '\0' ||
        v5_native_readback_motion_model_known(&readback) ||
        v5_main_page_model_scene_resolve(ac, &readback, &status, &invalid_scene)) {
        return 16;
    }
    prepare_readback(&readback);
    v5_native_readback_set_motion_model(&readback, "XYZBC_TRT");
    if (readback.g53_geometry_stale ||
        !v5_main_page_model_scene_resolve(bc, &readback, &status, &invalid_scene)) {
        return 17;
    }
    printf("v5 main page model projector smoke ok\n");
    return 0;
}
