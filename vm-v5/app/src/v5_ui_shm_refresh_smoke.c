#include "v5_ui_model.h"
#include "v5_state_publisher_service.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "/dev/shm/v5_ui_shm_refresh_smoke";
    V5UiModel model;
    V5StatePublisherReport publish_report = {0};

    unlink(path);
    v5_ui_model_init(&model);
    if (!v5_state_publisher_publish_once(path, &publish_report)) {
        return 1;
    }
    if (!v5_ui_model_refresh_status_from_shm(&model, path)) {
        unlink(path);
        return 2;
    }
    unlink(path);

    if ((model.status_view.valid_mask & V5_STATUS_VALID_MODAL) == 0u) {
        return 3;
    }
    if ((model.status_view.valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) == 0u) {
        return 4;
    }
    if (strcmp(model.status_view.runtime_modal_text, "UNAVAILABLE") != 0) {
        return 5;
    }

    printf(
        "v5 ui shm refresh: valid_mask=0x%08x flags=0x%08x epoch=%llu modal=%s\n",
        model.status_view.valid_mask,
        model.status_view.frame_flags,
        (unsigned long long)model.status_view.status_epoch,
        model.status_view.runtime_modal_text);
    return 0;
}
