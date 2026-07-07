#include "v5_status_shm.h"

#include "v5_native_sample.h"

#include <string.h>

void v5_status_shm_writer_seed_display_frame(V5StatusShmFrame *frame)
{
    if (!frame) {
        return;
    }

    v5_status_shm_frame_init(frame);
    frame->typed_valid_mask = 0U;
    frame->flags = V5_STATUS_FRAME_FLAG_DEGRADED | V5_STATUS_FRAME_FLAG_UNAVAILABLE;
}

void v5_status_shm_writer_apply_sample(V5StatusShmFrame *frame, const V5NativeDisplaySample *sample)
{
    if (!frame || !sample) {
        return;
    }

    v5_status_shm_frame_init(frame);
    frame->typed_valid_mask = sample->valid_mask;
    memcpy(frame->mcs, sample->mcs, sizeof(frame->mcs));
    memcpy(frame->cmd_mcs, sample->cmd_mcs, sizeof(frame->cmd_mcs));
    memcpy(frame->trajectory, sample->trajectory, sizeof(frame->trajectory));
    frame->trajectory_count = sample->trajectory_count;
    frame->spindle_speed_rpm = sample->spindle_speed_rpm;
    frame->linear_velocity_mm_per_min = sample->linear_velocity_mm_per_min;
    frame->feedrate_override = sample->feedrate_override;
    frame->spindle_override = sample->spindle_override;
}
