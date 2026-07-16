/**************************************************************************
* XYZBC table-rotary-tilting kinematics branch.
* License: GPL Version 2
**************************************************************************/

#include "motion.h"
#include "trtfuncs.h"
#include "rtapi_math.h"

int xyzbcKinematicsForward(const double *joints,
                           EmcPose *pos,
                           const KINEMATICS_FORWARD_FLAGS *fflags,
                           KINEMATICS_INVERSE_FLAGS *iflags)
{
    const TrtKinematicsContext *ctx = trtKinematicsGetContext();
    double x_rot_point = *(ctx->x_rot_point);
    double y_rot_point = *(ctx->y_rot_point);
    double z_rot_point = *(ctx->z_rot_point);
    double dx = *(ctx->x_offset);
    double dz = *(ctx->z_offset);
    double dt = *(ctx->tool_offset);
    double b_rad = joints[ctx->joint_b] * TO_RAD;
    double c_rad = joints[ctx->joint_c] * TO_RAD;

    dz = dz + dt;

    pos->tran.x =   cos(c_rad) * cos(b_rad) * (joints[ctx->joint_x] - dx - x_rot_point)
                  + sin(c_rad) *              (joints[ctx->joint_y]      - y_rot_point)
                  - cos(c_rad) * sin(b_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  + cos(c_rad) * dx
                  + x_rot_point;

    pos->tran.y = - sin(c_rad) * cos(b_rad) * (joints[ctx->joint_x] - dx - x_rot_point)
                  + cos(c_rad) *              (joints[ctx->joint_y]      - y_rot_point)
                  + sin(c_rad) * sin(b_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  - sin(c_rad) * dx
                  + y_rot_point;

    pos->tran.z =   sin(b_rad) * (joints[ctx->joint_x] - dx - x_rot_point)
                  + cos(b_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  + dz
                  + z_rot_point;

    pos->b = joints[ctx->joint_b];
    pos->c = joints[ctx->joint_c];

    pos->a = 0;
    pos->u = 0;
    pos->v = 0;
    pos->w = 0;

    return 0;
}

int xyzbcKinematicsInverse(const EmcPose *pos,
                           double *joints,
                           const KINEMATICS_INVERSE_FLAGS *iflags,
                           KINEMATICS_FORWARD_FLAGS *fflags)
{
    const TrtKinematicsContext *ctx = trtKinematicsGetContext();
    double x_rot_point = *(ctx->x_rot_point);
    double y_rot_point = *(ctx->y_rot_point);
    double z_rot_point = *(ctx->z_rot_point);
    double dx = *(ctx->x_offset);
    double dz = *(ctx->z_offset);
    double dt = *(ctx->tool_offset);
    double b_rad = pos->b * TO_RAD;
    double c_rad = pos->c * TO_RAD;
    double dpx;
    double dpz;
    EmcPose computed;

    dz = dz + dt;
    dpx = -cos(b_rad) * dx - sin(b_rad) * dz + dx;
    dpz =  sin(b_rad) * dx - cos(b_rad) * dz + dz;

    computed.tran.x = + cos(c_rad) * cos(b_rad) * (pos->tran.x - x_rot_point)
                      - sin(c_rad) * cos(b_rad) * (pos->tran.y - y_rot_point)
                      + sin(b_rad) * (pos->tran.z - z_rot_point)
                      + dpx
                      + x_rot_point;

    computed.tran.y = + sin(c_rad) * (pos->tran.x - x_rot_point)
                      + cos(c_rad) * (pos->tran.y - y_rot_point)
                      + y_rot_point;

    computed.tran.z = - cos(c_rad) * sin(b_rad) * (pos->tran.x - x_rot_point)
                      + sin(c_rad) * sin(b_rad) * (pos->tran.y - y_rot_point)
                      + cos(b_rad) * (pos->tran.z - z_rot_point)
                      + dpz
                      + z_rot_point;

    computed.b = pos->b;
    computed.c = pos->c;
    computed.a = 0;
    computed.u = 0;
    computed.v = 0;
    computed.w = 0;

    position_to_mapped_joints(ctx->max_joints, &computed, joints);
    return 0;
}
