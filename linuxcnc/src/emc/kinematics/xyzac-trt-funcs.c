/**************************************************************************
* XYZAC table-rotary-tilting kinematics branch.
* License: GPL Version 2
**************************************************************************/

#include "motion.h"
#include "trtfuncs.h"
#include "rtapi_math.h"

int xyzacKinematicsForward(const double *joints,
                           EmcPose *pos,
                           const KINEMATICS_FORWARD_FLAGS *fflags,
                           KINEMATICS_INVERSE_FLAGS *iflags)
{
    const TrtKinematicsContext *ctx = trtKinematicsGetContext();
    double x_rot_point = *(ctx->x_rot_point);
    double y_rot_point = *(ctx->y_rot_point);
    double z_rot_point = *(ctx->z_rot_point);
    double dt = *(ctx->tool_offset);
    double dy = *(ctx->y_offset);
    double dz = *(ctx->z_offset);
    double a_rad = joints[ctx->joint_a] * TO_RAD;
    double c_rad = joints[ctx->joint_c] * TO_RAD;

    dz = dz + dt;

    pos->tran.x = + cos(c_rad)              * (joints[ctx->joint_x]      - x_rot_point)
                  + sin(c_rad) * cos(a_rad) * (joints[ctx->joint_y] - dy - y_rot_point)
                  + sin(c_rad) * sin(a_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  + sin(c_rad) * dy
                  + x_rot_point;

    pos->tran.y = - sin(c_rad)              * (joints[ctx->joint_x]      - x_rot_point)
                  + cos(c_rad) * cos(a_rad) * (joints[ctx->joint_y] - dy - y_rot_point)
                  + cos(c_rad) * sin(a_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  + cos(c_rad) * dy
                  + y_rot_point;

    pos->tran.z = - sin(a_rad) * (joints[ctx->joint_y] - dy - y_rot_point)
                  + cos(a_rad) * (joints[ctx->joint_z] - dz - z_rot_point)
                  + dz
                  + z_rot_point;

    pos->a = joints[ctx->joint_a];
    pos->c = joints[ctx->joint_c];

    pos->b = 0;
    pos->u = 0;
    pos->v = 0;
    pos->w = 0;

    return 0;
}

int xyzacKinematicsInverse(const EmcPose *pos,
                           double *joints,
                           const KINEMATICS_INVERSE_FLAGS *iflags,
                           KINEMATICS_FORWARD_FLAGS *fflags)
{
    const TrtKinematicsContext *ctx = trtKinematicsGetContext();
    double x_rot_point = *(ctx->x_rot_point);
    double y_rot_point = *(ctx->y_rot_point);
    double z_rot_point = *(ctx->z_rot_point);
    double dy = *(ctx->y_offset);
    double dz = *(ctx->z_offset);
    double dt = *(ctx->tool_offset);
    double a_rad = pos->a * TO_RAD;
    double c_rad = pos->c * TO_RAD;
    EmcPose computed;

    dz = dz + dt;

    computed.tran.x = + cos(c_rad) * (pos->tran.x - x_rot_point)
                      - sin(c_rad) * (pos->tran.y - y_rot_point)
                      + x_rot_point;

    computed.tran.y = + sin(c_rad) * cos(a_rad) * (pos->tran.x - x_rot_point)
                      + cos(c_rad) * cos(a_rad) * (pos->tran.y - y_rot_point)
                      - sin(a_rad) * (pos->tran.z - z_rot_point)
                      - cos(a_rad) * dy
                      + sin(a_rad) * dz
                      + dy
                      + y_rot_point;

    computed.tran.z = + sin(c_rad) * sin(a_rad) * (pos->tran.x - x_rot_point)
                      + cos(c_rad) * sin(a_rad) * (pos->tran.y - y_rot_point)
                      + cos(a_rad) * (pos->tran.z - z_rot_point)
                      - sin(a_rad) * dy
                      - cos(a_rad) * dz
                      + dz
                      + z_rot_point;

    computed.a = pos->a;
    computed.c = pos->c;
    computed.b = 0;
    computed.u = 0;
    computed.v = 0;
    computed.w = 0;

    position_to_mapped_joints(ctx->max_joints, &computed, joints);
    return 0;
}
