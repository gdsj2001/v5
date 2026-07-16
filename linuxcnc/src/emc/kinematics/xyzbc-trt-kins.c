/********************************************************************
* xyzbc-trt-kins.c employing switchkins.[ch]
* License: GPL Version 2
*
* NOTEs:
*  1) specify all kparms items
*  2) specify 3 KS,KF,KI functions for switchkins_type=0,1,2
*  3) the 0th switchkins_type is the startup default
*  4) sparm is a module string parameter for configuration
*  5) The directions of the rotational axes are the opposite of the 
*     conventional axis directions.
*/

#include "motion.h"
#include "switchkins.h"
#include "trtfuncs.h"
#include "rtapi_string.h"
#include "rtapi.h"

int xyzbcKinematicsForward(const double *joints,
                           EmcPose *pos,
                           const KINEMATICS_FORWARD_FLAGS *fflags,
                           KINEMATICS_INVERSE_FLAGS *iflags);

int xyzbcKinematicsInverse(const EmcPose *pos,
                           double *joints,
                           const KINEMATICS_INVERSE_FLAGS *iflags,
                           KINEMATICS_FORWARD_FLAGS *fflags);

int switchkinsSetup(kparms* kp,
                    KS* kset0, KS* kset1, KS* kset2,
                    KF* kfwd0, KF* kfwd1, KF* kfwd2,
                    KI* kinv0, KI* kinv1, KI* kinv2
                   )
{
    kp->kinsname    = "xyzbc-trt-kins"; // !!! must agree with filename
    kp->halprefix   = "xyzbc-trt-kins"; // hal pin names
    kp->required_coordinates = "XYZBC";
    kp->allow_duplicates     = 0;
    kp->max_joints           = EMCMOT_MAX_JOINTS;

    if (kp->sparm && strstr(kp->sparm,"identityfirst")) {
        rtapi_print("\n!!! switchkins-type 0 is IDENTITY\n");
        *kset0 = identityKinematicsSetup;
        *kfwd0 = identityKinematicsForward;
        *kinv0 = identityKinematicsInverse;

        *kset1 = trtKinematicsSetup;
        *kfwd1 = xyzbcKinematicsForward;
        *kinv1 = xyzbcKinematicsInverse;
    } else {
        rtapi_print("\n!!! switchkins-type 0 is %s\n",kp->kinsname);
        *kset0 = trtKinematicsSetup;
        *kfwd0 = xyzbcKinematicsForward;
        *kinv0 = xyzbcKinematicsInverse;

        *kset1 = identityKinematicsSetup;
        *kfwd1 = identityKinematicsForward;
        *kinv1 = identityKinematicsInverse;
    }

    *kset2 = userkKinematicsSetup;
    *kfwd2 = userkKinematicsForward;
    *kinv2 = userkKinematicsInverse;

    return 0;
}
