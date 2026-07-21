#ifndef V5_LVGL_REMOTE_DISPLAY_DELTA_H
#define V5_LVGL_REMOTE_DISPLAY_DELTA_H

#include "v5_lvgl_remote_display_capture.h"

#define V5_REMOTE_DISPLAY_DELTA_TILE_SIZE 16U
#define V5_REMOTE_DISPLAY_DELTA_MAX_WIDTH 1024U
#define V5_REMOTE_DISPLAY_DELTA_MAX_HEIGHT 600U

int v5_lvgl_remote_display_delta_commit(
    const unsigned char *frame,
    unsigned char *published_frame,
    unsigned int width,
    unsigned int height,
    const V5RemoteDirtyRect *candidate_rects,
    unsigned int candidate_count,
    V5RemoteDirtyRect *changed_rects,
    unsigned int changed_capacity,
    unsigned int *changed_count);

#endif
