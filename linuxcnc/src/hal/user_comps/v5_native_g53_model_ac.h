#ifndef V5_NATIVE_G53_MODEL_AC_H
#define V5_NATIVE_G53_MODEL_AC_H

#include <math.h>

#include "hal/user_comps/v5_native_g53_model_types.h"

static inline int v5_native_g53_model_ac_project(
    const V5NativeG53GeometryInput *input,
    V5NativeG53KinsPins *pins)
{
    if (!input || !pins || !isfinite(input->a_y) ||
        !isfinite(input->a_z) || !isfinite(input->c_x) ||
        !isfinite(input->c_y)) {
        return 0;
    }
    pins->x_rot_point = input->c_x;
    pins->y_rot_point = input->c_y;
    pins->z_rot_point = 0.0;
    pins->x_offset = 0.0;
    pins->y_offset = input->a_y - input->c_y;
    pins->z_offset = input->a_z;
    return 1;
}

#endif
