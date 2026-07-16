#ifndef V5_NATIVE_G53_MODEL_TYPES_H
#define V5_NATIVE_G53_MODEL_TYPES_H

#include <stdint.h>

enum V5NativeG53GeometryField {
    V5_NATIVE_G53_FIELD_A_Y = 1U << 0,
    V5_NATIVE_G53_FIELD_A_Z = 1U << 1,
    V5_NATIVE_G53_FIELD_B_X = 1U << 2,
    V5_NATIVE_G53_FIELD_B_Z = 1U << 3,
    V5_NATIVE_G53_FIELD_C_X = 1U << 4,
    V5_NATIVE_G53_FIELD_C_Y = 1U << 5
};

#define V5_NATIVE_G53_FIELD_MASK_ALL \
    (V5_NATIVE_G53_FIELD_A_Y | V5_NATIVE_G53_FIELD_A_Z | \
     V5_NATIVE_G53_FIELD_B_X | V5_NATIVE_G53_FIELD_B_Z | \
     V5_NATIVE_G53_FIELD_C_X | V5_NATIVE_G53_FIELD_C_Y)

#define V5_NATIVE_G53_FIELD_MASK_AC \
    (V5_NATIVE_G53_FIELD_A_Y | V5_NATIVE_G53_FIELD_A_Z | \
     V5_NATIVE_G53_FIELD_C_X | V5_NATIVE_G53_FIELD_C_Y)

#define V5_NATIVE_G53_FIELD_MASK_BC \
    (V5_NATIVE_G53_FIELD_B_X | V5_NATIVE_G53_FIELD_B_Z | \
     V5_NATIVE_G53_FIELD_C_X | V5_NATIVE_G53_FIELD_C_Y)

typedef struct V5NativeG53GeometryInput {
    double a_y;
    double a_z;
    double b_x;
    double b_z;
    double c_x;
    double c_y;
} V5NativeG53GeometryInput;

typedef struct V5NativeG53KinsPins {
    double x_rot_point;
    double y_rot_point;
    double z_rot_point;
    double x_offset;
    double y_offset;
    double z_offset;
} V5NativeG53KinsPins;

typedef int (*V5NativeG53ModelProjector)(
    const V5NativeG53GeometryInput *input,
    V5NativeG53KinsPins *pins);

typedef struct V5NativeG53ModelBranch {
    const char *canonical;
    const char *kinematics_module;
    uint32_t active_field_mask;
    V5NativeG53ModelProjector project;
} V5NativeG53ModelBranch;

#endif
