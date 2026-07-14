#ifndef V5_NATIVE_MOTION_PARAMETERS_H
#define V5_NATIVE_MOTION_PARAMETERS_H

#include "v5_command_gate.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT 6U

typedef enum V5NativeDriverMode {
    V5_NATIVE_DRIVER_MODE_UNKNOWN = 0,
    V5_NATIVE_DRIVER_MODE_BUS,
    V5_NATIVE_DRIVER_MODE_PULSE,
} V5NativeDriverMode;

typedef struct V5NativeMotionAxisParameters {
    char axis;
    int active;
    unsigned int status_slot;
    unsigned int slave_position;
    int slave_mapping_known;
    double max_velocity;
    double max_acceleration;
    double min_limit;
    double max_limit;
    double bus_zero_counts;
    double bus_counts_per_unit;
    double bus_home_reference;
    double arrival_tolerance_units;
    int home_sequence;
    int bus_zero_evidence_known;
    unsigned int valid_mask;
} V5NativeMotionAxisParameters;

typedef struct V5NativeMotionParameters {
    int loaded;
    V5NativeDriverMode driver_mode;
    int runtime_owner_loaded;
    int pulse_runtime_selectable;
    char pulse_contract_status[64];
    unsigned int active_axis_count;
    V5NativeMotionAxisParameters axes[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT];
} V5NativeMotionParameters;

void v5_native_motion_parameters_init(V5NativeMotionParameters *parameters);
int v5_native_motion_parameters_load(
    const char *ini_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap);
int v5_native_motion_parameters_load_runtime_owner(
    const char *settings_project_root,
    const char *settings_runtime_json_path,
    const char *pulse_contract_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap);
const V5NativeMotionAxisParameters *v5_native_motion_parameters_axis(
    const V5NativeMotionParameters *parameters,
    char axis);
int v5_native_motion_parameters_resolve_jog(
    const V5NativeMotionParameters *parameters,
    V5CommandRequest *request,
    char *code,
    size_t code_cap);
const char *v5_native_driver_mode_text(V5NativeDriverMode mode);

#ifdef __cplusplus
}
#endif

#endif
