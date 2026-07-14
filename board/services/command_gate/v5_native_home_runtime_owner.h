#ifndef V5_NATIVE_HOME_RUNTIME_OWNER_H
#define V5_NATIVE_HOME_RUNTIME_OWNER_H

#include "v5_native_motion_parameters.h"
#include "v5_native_home.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeWcheckpointSnapshot {
    int valid;
    unsigned int generation;
    int64_t logical_counts;
    int64_t base_counts;
    int64_t runtime_counts;
} V5NativeWcheckpointSnapshot;

typedef struct V5NativeSafeZeroPlan {
    char axis;
    unsigned int generation;
    int64_t zero_abs_counts;
    int64_t counts_per_rev;
    int64_t delta_counts;
    int64_t logical_target_counts;
    int64_t runtime_target_counts;
    int64_t arrival_tolerance_counts;
} V5NativeSafeZeroPlan;

typedef struct V5NativeHomeRuntimeState {
    int active;
    V5NativeHomeProgress progress;
} V5NativeHomeRuntimeState;

int v5_native_home_runtime_begin(
    unsigned long long run_id,
    unsigned int generation,
    const char *kind);
void v5_native_home_runtime_publish(const V5NativeHomeProgress *progress);
int v5_native_home_runtime_snapshot(
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomeRuntimeState *state);
void v5_native_home_runtime_finish(
    unsigned long long run_id,
    unsigned int generation,
    V5NativeHomePhase phase,
    const char *reason,
    int cancelled);

int v5_native_home_force_rtcp_off(char *code, size_t code_cap);
int v5_native_home_wcheckpoint_read(
    char axis,
    V5NativeWcheckpointSnapshot *snapshot,
    char *code,
    size_t code_cap);
int v5_native_home_safe_zero_plan(
    const V5NativeMotionAxisParameters *axis,
    const V5NativeWcheckpointSnapshot *snapshot,
    V5NativeSafeZeroPlan *plan,
    char *code,
    size_t code_cap);
int v5_native_home_safe_zero_arrived(
    const V5NativeSafeZeroPlan *plan,
    const V5NativeWcheckpointSnapshot *snapshot,
    int64_t *logical_error_counts,
    char *code,
    size_t code_cap);
int v5_native_home_safe_zero_remap(
    V5NativeSafeZeroPlan *plan,
    const V5NativeWcheckpointSnapshot *snapshot,
    char *code,
    size_t code_cap);

#ifdef __cplusplus
}
#endif

#endif
