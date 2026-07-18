#ifndef V5_UI_MODEL_H
#define V5_UI_MODEL_H

#include "v5_status_shm_mmap.h"
#include "v5_ui_status_view.h"

typedef struct V5UiModel {
    unsigned int boot_generation;
    int lvgl_initialized;
    unsigned int boot_closure_abi;
    unsigned int command_count;
    unsigned int drive_profile_count;
    unsigned int drive_profile_map_count;
    unsigned int parameter_owner_count;
    unsigned int resource_count;
    V5UiStatusView status_view;
    V5UiStatusView last_good_status_view;
    int has_last_good_status;
    unsigned long long last_good_monotonic_ns;
    V5StatusShmMmapReader status_reader;
    unsigned long long status_reader_retry_after_ns;
    unsigned long long status_reader_last_epoch;
    unsigned int status_reader_failure_count;
    unsigned int status_reader_stagnant_count;
} V5UiModel;

void v5_ui_model_init(V5UiModel *model);
void v5_ui_model_close_status_reader(V5UiModel *model);
int v5_ui_model_apply_status_frame(V5UiModel *model, const V5StatusShmFrame *frame);
int v5_ui_model_refresh_status_from_shm(V5UiModel *model, const char *path);

#endif
