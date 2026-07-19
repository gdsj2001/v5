#ifndef V5_NATIVE_G53_MODEL_H
#define V5_NATIVE_G53_MODEL_H

#include <stddef.h>
#include <string.h>

#include "hal/user_comps/v5_native_g53_model_ac.h"
#include "hal/user_comps/v5_native_g53_model_bc.h"

static const V5NativeG53ModelBranch v5_native_g53_model_branches[] = {
    {"XYZAC_TRT", "xyzac-trt-kins", "XYZAC", V5_NATIVE_G53_FIELD_MASK_AC,
     v5_native_g53_model_ac_project},
    {"XYZBC_TRT", "xyzbc-trt-kins", "XYZBC", V5_NATIVE_G53_FIELD_MASK_BC,
     v5_native_g53_model_bc_project},
};

static inline size_t v5_native_g53_model_branch_count(void)
{
    return sizeof(v5_native_g53_model_branches) /
           sizeof(v5_native_g53_model_branches[0]);
}

static inline const V5NativeG53ModelBranch *v5_native_g53_model_find(
    const char *canonical)
{
    size_t index;

    if (!canonical || !canonical[0]) {
        return 0;
    }
    for (index = 0U; index < v5_native_g53_model_branch_count(); ++index) {
        if (strcmp(canonical, v5_native_g53_model_branches[index].canonical) == 0) {
            return &v5_native_g53_model_branches[index];
        }
    }
    return 0;
}

static inline int v5_native_g53_model_branch_valid(
    const V5NativeG53ModelBranch *branch)
{
    return branch && branch->canonical && branch->canonical[0] &&
           branch->kinematics_module && branch->kinematics_module[0] &&
           branch->logical_axes && strlen(branch->logical_axes) == 5U &&
           branch->project && branch->active_field_mask != 0U &&
           (branch->active_field_mask & ~V5_NATIVE_G53_FIELD_MASK_ALL) == 0U;
}

static inline int v5_native_g53_model_field_active(
    const V5NativeG53ModelBranch *branch,
    uint32_t field)
{
    return v5_native_g53_model_branch_valid(branch) && field != 0U &&
           (field & (field - 1U)) == 0U &&
           (branch->active_field_mask & field) != 0U;
}

static inline int v5_native_g53_model_resolve(
    const char *canonical,
    const V5NativeG53GeometryInput *input,
    V5NativeG53KinsPins *pins,
    const V5NativeG53ModelBranch **branch_out)
{
    const V5NativeG53ModelBranch *branch = v5_native_g53_model_find(canonical);

    if (branch_out) {
        *branch_out = 0;
    }
    if (!v5_native_g53_model_branch_valid(branch) ||
        !branch->project(input, pins)) {
        return 0;
    }
    if (branch_out) {
        *branch_out = branch;
    }
    return 1;
}

#endif
