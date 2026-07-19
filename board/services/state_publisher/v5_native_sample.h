#ifndef V5_NATIVE_SAMPLE_H
#define V5_NATIVE_SAMPLE_H

#include "v5_status_shm.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5NativeDisplaySample {
    int available;
    unsigned int valid_mask;
    uint32_t writer_identity;
    uint64_t source_acquired_mono_ns;
    uint64_t source_generation;
    double mcs[V5_STATUS_AXIS_COUNT];
    double cmd_mcs[V5_STATUS_AXIS_COUNT];
    double unit_per_count[V5_STATUS_AXIS_COUNT];
    double following_error[V5_STATUS_AXIS_COUNT];
    uint8_t display_digits[V5_STATUS_AXIS_COUNT];
    V5StatusPoint trajectory[V5_STATUS_TRAJECTORY_POINT_COUNT];
    unsigned int trajectory_count;
    double spindle_speed_rpm;
    double linear_velocity_mm_per_min;
    double feedrate_override;
    double spindle_override;
} V5NativeDisplaySample;

typedef struct V5NativeDisplaySampleReader {
    int fd;
    const void *page;
    size_t mapped_size;
    uint64_t device_id;
    uint64_t inode_id;
    uint32_t writer_identity;
    uint64_t source_generation;
    unsigned int open_count;
    unsigned int failure_count;
} V5NativeDisplaySampleReader;

void v5_native_display_sample_init(V5NativeDisplaySample *sample);
void v5_native_display_sample_reader_init(V5NativeDisplaySampleReader *reader);
void v5_native_display_sample_reader_close(V5NativeDisplaySampleReader *reader);
int v5_native_display_sample_reader_read(
    V5NativeDisplaySampleReader *reader,
    V5NativeDisplaySample *sample);

#ifdef __cplusplus
}
#endif

#endif
