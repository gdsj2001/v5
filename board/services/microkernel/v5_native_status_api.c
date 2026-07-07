#include "v5_native_status_api.h"

#include <string.h>

static const V5NativeStatusEntry k_status_entries[] = {
    {"active_wcs", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.active_wcs", 0},
    {"wcs_offsets", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.wcs_offsets", 0},
    {"g92_offset", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.g92_offset", 0},
    {"tool_offset", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.tool_offset", 0},
    {"active_tlo", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.active_tlo", 0},
    {"runtime_modal", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.runtime_modal", 0},
    {"current_tool", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.current_tool", 0},
    {"tool_length", V5_NATIVE_STATUS_LINUXCNC, "linuxcnc/native/status.tool_length", 0},
    {"rtcp_actual", V5_NATIVE_STATUS_KINEMATICS, "linuxcnc/native/kinematics.rtcp_actual", 0},
    {"g53_geometry", V5_NATIVE_STATUS_KINEMATICS, "microkernel/native/kinematics.g53_geometry", 0},
    {"mode_switch_actual", V5_NATIVE_STATUS_HAL, "hal/native/mode_switch_actual", 0},
    {"rotary_logical_abs_counts64", V5_NATIVE_STATUS_ROTARY_WINDOW, "microkernel/native/rotary.logical_abs_counts64", 0},
    {"rotary_runtime_window_counts", V5_NATIVE_STATUS_ROTARY_WINDOW, "microkernel/native/rotary.runtime_window_counts", 0},
    {"rotary_generation", V5_NATIVE_STATUS_ROTARY_WINDOW, "microkernel/native/rotary.generation", 0},
    {"safety_estop", V5_NATIVE_STATUS_SAFETY, "hal/native/safety.estop", 0},
    {"motion_owner_state", V5_NATIVE_STATUS_MOTION_OWNER, "microkernel/native/motion.owner_state", 0},
};

const V5NativeStatusEntry *v5_native_status_api_entries(size_t *count)
{
    if (count) {
        *count = sizeof(k_status_entries) / sizeof(k_status_entries[0]);
    }
    return k_status_entries;
}

size_t v5_native_status_api_count(void)
{
    return sizeof(k_status_entries) / sizeof(k_status_entries[0]);
}

const V5NativeStatusEntry *v5_native_status_api_find(const char *id)
{
    size_t i;
    if (!id) {
        return 0;
    }
    for (i = 0; i < v5_native_status_api_count(); ++i) {
        if (strcmp(k_status_entries[i].id, id) == 0) {
            return &k_status_entries[i];
        }
    }
    return 0;
}

int v5_native_status_api_has_shm_exports(void)
{
    size_t i;
    for (i = 0; i < v5_native_status_api_count(); ++i) {
        if (k_status_entries[i].export_to_shm) {
            return 1;
        }
    }
    return 0;
}
