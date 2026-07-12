#ifndef V5_WCHECKPOINT_H
#define V5_WCHECKPOINT_H

#include "kinematics.h"
#include "posemath.h"

#define V5_WCHECKPOINT_ROTARY_AXES 3

enum v5_wcheckpoint_reason {
    V5_WCHECKPOINT_OK = 0,
    V5_WCHECKPOINT_DISABLED = 1,
    V5_WCHECKPOINT_RAW_WIDTH_MISSING = 2,
    V5_WCHECKPOINT_RAW_TYPE_UNSUPPORTED = 3,
    V5_WCHECKPOINT_RAW_MODULUS_MISSING = 4,
    V5_WCHECKPOINT_CREV_MISSING = 5,
    V5_WCHECKPOINT_REDUCER_RATIO_MISSING = 6,
    V5_WCHECKPOINT_DRIVE_WINDOW_UNPROVEN = 7,
    V5_WCHECKPOINT_LOGICAL_STORAGE_INSUFFICIENT = 8,
    V5_WCHECKPOINT_KINEMATICS_FAILED = 9,
    V5_WCHECKPOINT_PERIODICITY_FAILED = 10,
    V5_WCHECKPOINT_RUNTIME_WINDOW_FAILED = 11,
};

int v5_wcheckpoint_export_hal(int component_id);
void v5_wcheckpoint_reset(void);
void v5_wcheckpoint_update_before_inputs(void);
void v5_wcheckpoint_publish(void);

int v5_wcheckpoint_forward(
    const double *joint,
    EmcPose *logical_pose,
    KINEMATICS_FORWARD_FLAGS *fflags,
    KINEMATICS_INVERSE_FLAGS *iflags);
int v5_wcheckpoint_inverse(
    const EmcPose *logical_pose,
    double *joint,
    KINEMATICS_INVERSE_FLAGS *iflags,
    KINEMATICS_FORWARD_FLAGS *fflags);
int v5_wcheckpoint_target_allowed(unsigned int rotary_index, double target_deg);

#endif
