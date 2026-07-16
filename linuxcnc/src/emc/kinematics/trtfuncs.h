#ifndef TRT_KINEMATICS_COMMON_H
#define TRT_KINEMATICS_COMMON_H

#include "hal.h"
#include "kinematics.h"

typedef struct TrtKinematicsContext {
    int max_joints;
    int joint_x;
    int joint_y;
    int joint_z;
    int joint_a;
    int joint_b;
    int joint_c;
    int joint_u;
    int joint_v;
    int joint_w;
    hal_float_t *x_rot_point;
    hal_float_t *y_rot_point;
    hal_float_t *z_rot_point;
    hal_float_t *x_offset;
    hal_float_t *y_offset;
    hal_float_t *z_offset;
    hal_float_t *tool_offset;
} TrtKinematicsContext;

int trtKinematicsSetup(const int comp_id,
                       const char *coordinates,
                       kparms *ksetup_parms);

const TrtKinematicsContext *trtKinematicsGetContext(void);

#endif
