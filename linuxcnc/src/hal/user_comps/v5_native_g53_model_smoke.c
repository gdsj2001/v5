#include "hal/user_comps/v5_native_g53_model.h"
#include "hal/user_comps/v5_native_hal_owner_protocol.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(V5NativeG53StatusBlockWire) == 152U,
               "G53 status wire size changed");
_Static_assert(offsetof(V5NativeG53StatusBlockWire, active_field_mask) == 28U,
               "G53 active field mask must reuse reserved0 ABI slot");
_Static_assert(offsetof(V5NativeG53StatusBlockWire, crc32) == 144U,
               "G53 status CRC offset changed");

static int same(double left, double right)
{
    return fabs(left - right) < 1e-12;
}

int main(void)
{
    V5NativeG53GeometryInput input = {
        22.0, -80.0,
        12.0, -25.0,
        50.0, 20.0,
    };
    const V5NativeG53ModelBranch *branch = 0;
    V5NativeG53KinsPins pins;

    memset(&pins, 0, sizeof(pins));
    if (v5_native_g53_model_branch_count() != 2U ||
        !v5_native_g53_model_resolve("XYZAC_TRT", &input, &pins, &branch) ||
        !branch || strcmp(branch->kinematics_module, "xyzac-trt-kins") != 0 ||
        strcmp(branch->logical_axes, "XYZAC") != 0 ||
        branch->active_field_mask != V5_NATIVE_G53_FIELD_MASK_AC ||
        !v5_native_g53_model_field_active(branch, V5_NATIVE_G53_FIELD_A_Y) ||
        v5_native_g53_model_field_active(branch, V5_NATIVE_G53_FIELD_B_X) ||
        !same(pins.x_rot_point, 50.0) || !same(pins.y_rot_point, 20.0) ||
        !same(pins.z_rot_point, 0.0) || !same(pins.x_offset, 0.0) ||
        !same(pins.y_offset, 2.0) || !same(pins.z_offset, -80.0)) {
        return 1;
    }
    memset(&pins, 0, sizeof(pins));
    branch = 0;
    if (!v5_native_g53_model_resolve("XYZBC_TRT", &input, &pins, &branch) ||
        !branch || strcmp(branch->kinematics_module, "xyzbc-trt-kins") != 0 ||
        strcmp(branch->logical_axes, "XYZBC") != 0 ||
        branch->active_field_mask != V5_NATIVE_G53_FIELD_MASK_BC ||
        !v5_native_g53_model_field_active(branch, V5_NATIVE_G53_FIELD_B_X) ||
        v5_native_g53_model_field_active(branch, V5_NATIVE_G53_FIELD_A_Y) ||
        !same(pins.x_rot_point, 50.0) || !same(pins.y_rot_point, 20.0) ||
        !same(pins.z_rot_point, 0.0) || !same(pins.x_offset, -38.0) ||
        !same(pins.y_offset, 0.0) || !same(pins.z_offset, -25.0)) {
        return 2;
    }
    branch = (const V5NativeG53ModelBranch *)1;
    if (v5_native_g53_model_resolve("UNKNOWN", &input, &pins, &branch) || branch) {
        return 3;
    }
    input.b_x = NAN;
    input.b_z = NAN;
    if (!v5_native_g53_model_resolve("XYZAC_TRT", &input, &pins, &branch) ||
        !branch || !same(pins.y_offset, 2.0) || !same(pins.z_offset, -80.0)) {
        return 4;
    }
    input.a_y = NAN;
    if (v5_native_g53_model_resolve("XYZAC_TRT", &input, &pins, &branch)) {
        return 5;
    }
    input.a_y = 22.0;
    input.a_z = NAN;
    input.b_x = 12.0;
    input.b_z = -25.0;
    if (!v5_native_g53_model_resolve("XYZBC_TRT", &input, &pins, &branch) ||
        !branch || !same(pins.x_offset, -38.0) || !same(pins.z_offset, -25.0)) {
        return 6;
    }
    input.b_z = NAN;
    if (v5_native_g53_model_resolve("XYZBC_TRT", &input, &pins, &branch)) {
        return 7;
    }
    puts("v5 native G53 model branch smoke ok");
    return 0;
}
