#include "v5_ui_model.h"

#include "v5_status_shm_mmap.h"

void v5_ui_model_init(V5UiModel *model)
{
    if (!model) {
        return;
    }
    model->boot_generation = 1U;
    model->lvgl_initialized = 0;
    model->boot_closure_abi = 0U;
    model->command_count = 0U;
    model->drive_profile_count = 0U;
    model->drive_profile_map_count = 0U;
    model->parameter_owner_count = 0U;
    model->resource_count = 0U;
    v5_ui_status_view_init(&model->status_view);
}

int v5_ui_model_apply_status_frame(V5UiModel *model, const V5StatusShmFrame *frame)
{
    if (!model || !frame) {
        return 0;
    }

    return v5_ui_status_view_from_frame(&model->status_view, frame);
}

int v5_ui_model_refresh_status_from_shm(V5UiModel *model, const char *path)
{
    V5StatusShmFrame frame;

    if (!model) {
        return 0;
    }
    if (!v5_status_shm_read_from_path(path, &frame)) {
        return 0;
    }
    return v5_ui_model_apply_status_frame(model, &frame);
}
