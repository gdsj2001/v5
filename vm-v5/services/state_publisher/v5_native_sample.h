#ifndef V5_NATIVE_SAMPLE_H
#define V5_NATIVE_SAMPLE_H

#include "v5_status_shm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeDisplaySample {
    int available;
    unsigned int valid_mask;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    V5StatusPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    unsigned int trajectory_count;
    char runtime_modal_text[V5_STATUS_MODAL_TEXT_CAP];
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
} V5NativeDisplaySample;

void v5_native_display_sample_init(V5NativeDisplaySample *sample);
int v5_native_display_sample_read(V5NativeDisplaySample *sample);

#ifdef __cplusplus
}
#endif

#endif
