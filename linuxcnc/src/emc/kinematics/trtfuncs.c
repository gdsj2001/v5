/**************************************************************************
* Copyright 2016 Rudy du Preez <rudy@asmsa.co.za>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**************************************************************************/

/********************************************************************
* Shared TRT coordinate mapping and HAL-pin context.
* Model-specific forward/inverse formulas live in their own branch files.
********************************************************************/

#include "motion.h"
#include "trtfuncs.h"
#include "rtapi_string.h"
#include "rtapi_ctype.h"

static TrtKinematicsContext *trt_context;

const TrtKinematicsContext *trtKinematicsGetContext(void)
{
    return trt_context;
}


int trtKinematicsSetup(const int   comp_id,
                       const char* coordinates,
                       kparms*     kp)
{
    int i,jno,res=0;
    int axis_idx_for_jno[EMCMOT_MAX_JOINTS];
    int rqdjoints = strlen(kp->required_coordinates);

    if (!coordinates || strcmp(coordinates, kp->required_coordinates) != 0) {
        rtapi_print_msg(RTAPI_MSG_ERR,
             "ERROR %s: coordinates must be exactly <%s>, got <%s>\n",
             kp->kinsname,
             kp->required_coordinates,
             coordinates ? coordinates : "(null)");
        goto error;
    }
    if (rqdjoints > kp->max_joints) {
        rtapi_print_msg(RTAPI_MSG_ERR,
             "ERROR %s: supports %d joints, <%s> requires %d\n",
             kp->kinsname,
             kp->max_joints,
             coordinates,
             rqdjoints);
        goto error;
    }
    trt_context = hal_malloc(sizeof(*trt_context));
    if (!trt_context) {
        goto error;
    }
    trt_context->max_joints = kp->max_joints;
    trt_context->joint_x = -1;
    trt_context->joint_y = -1;
    trt_context->joint_z = -1;
    trt_context->joint_a = -1;
    trt_context->joint_b = -1;
    trt_context->joint_c = -1;
    trt_context->joint_u = -1;
    trt_context->joint_v = -1;
    trt_context->joint_w = -1;

    if (map_coordinates_to_jnumbers(coordinates,
                                    kp->max_joints,
                                    kp->allow_duplicates,
                                    axis_idx_for_jno)) {
       goto error;
    }
    // require all chars in reqd_coords (order doesn't matter)
    for (i=0; i < rqdjoints; i++) {
        char  reqd_char;
        reqd_char = *(kp->required_coordinates + i);
        if (   !strchr(coordinates,toupper(reqd_char))
            && !strchr(coordinates,tolower(reqd_char)) ) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                 "ERROR %s:\nrequired  coordinates:%s\n"
                           "specified coordinates:%s\n",
                 kp->kinsname, kp->required_coordinates, coordinates);
            goto error;
        }
    }

    // assign principal joint numbers (first found in coordinates map)
    // duplicates are handled by position_to_mapped_joints()
    for (jno=0; jno < EMCMOT_MAX_JOINTS; jno++) {
       if (axis_idx_for_jno[jno] == 0 && trt_context->joint_x == -1) {trt_context->joint_x = jno;}
       if (axis_idx_for_jno[jno] == 1 && trt_context->joint_y == -1) {trt_context->joint_y = jno;}
       if (axis_idx_for_jno[jno] == 2 && trt_context->joint_z == -1) {trt_context->joint_z = jno;}
       if (axis_idx_for_jno[jno] == 3 && trt_context->joint_a == -1) {trt_context->joint_a = jno;}
       if (axis_idx_for_jno[jno] == 4 && trt_context->joint_b == -1) {trt_context->joint_b = jno;}
       if (axis_idx_for_jno[jno] == 5 && trt_context->joint_c == -1) {trt_context->joint_c = jno;}
       if (axis_idx_for_jno[jno] == 6 && trt_context->joint_u == -1) {trt_context->joint_u = jno;}
       if (axis_idx_for_jno[jno] == 7 && trt_context->joint_v == -1) {trt_context->joint_v = jno;}
       if (axis_idx_for_jno[jno] == 8 && trt_context->joint_w == -1) {trt_context->joint_w = jno;}
    }

    rtapi_print("%s coordinates=%s assigns:\n", kp->kinsname,coordinates);
    for (jno=0; jno<EMCMOT_MAX_JOINTS; jno++) {
        if (axis_idx_for_jno[jno] == -1) break; //fini
        rtapi_print("   Joint %d ==> Axis %c\n",
                   jno,"XYZABCUVW"[axis_idx_for_jno[jno]]);
    }

    res += hal_pin_float_newf(HAL_IN, &(trt_context->x_rot_point), comp_id,
                 "%s.x-rot-point",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->y_rot_point), comp_id,
                 "%s.y-rot-point",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->z_rot_point), comp_id,
                 "%s.z-rot-point",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->x_offset), comp_id,
                 "%s.x-offset",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->y_offset), comp_id,
                 "%s.y-offset",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->z_offset), comp_id,
                 "%s.z-offset",kp->halprefix);
    res += hal_pin_float_newf(HAL_IN, &(trt_context->tool_offset), comp_id,
                 "%s.tool-offset",kp->halprefix);
    if (res) {goto error;}
    return 0;

error:
    rtapi_print_msg(RTAPI_MSG_ERR,"trtKinematicsSetup() FAIL\n");
    return -1;
} // trtKinematicsSetup()
