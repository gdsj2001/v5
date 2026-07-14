#ifndef V5_NATIVE_HOME_MAPPING_H
#define V5_NATIVE_HOME_MAPPING_H

#include "v5_native_motion_parameters.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int v5_native_home_mapping_load(
    const char *settings_project_root,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap);

int v5_native_home_runtime_owner_load_bus(
    const char *settings_project_root,
    const char *settings_runtime_json_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap);

#ifdef __cplusplus
}
#endif

#endif
