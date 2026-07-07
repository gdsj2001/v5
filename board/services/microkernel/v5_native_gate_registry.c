#include "v5_native_gate_registry.h"

#include <string.h>

static const V5NativeGateEntry k_gates[] = {
    {"program_start", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Run"},
    {"program_pause", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Pause"},
    {"program_resume", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Resume"},
    {"home", V5_NATIVE_GATE_RUN_CONTROL, "native_home_mode_gate", "active_driver_mode -> Pulse Set Home / BUS G53 machine zero"},
    {"jog_increment", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Jog_Incr"},
    {"jog_continuous", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Jog"},
    {"jog_stop", V5_NATIVE_GATE_RUN_CONTROL, "native_linuxcncrsh", "Set Jog 0"},
    {"wcs_select", V5_NATIVE_GATE_COORDINATE_SYSTEM, "native_linuxcncrsh", "Set MDI G5x"},
    {"work_zero", V5_NATIVE_GATE_COORDINATE_SYSTEM, "native_linuxcncrsh", "Set MDI G10 L20"},
    {"g92_clear", V5_NATIVE_GATE_COORDINATE_SYSTEM, "native_linuxcncrsh", "Set MDI G92.1"},
    {"rtcp_set", V5_NATIVE_GATE_GEOMETRY, "native_linuxcncrsh", "Set MDI M128/M129"},
    {"estop_force", V5_NATIVE_GATE_SAFETY, "native_safety", "Set Machine Off"},
    {"estop_reset", V5_NATIVE_GATE_SAFETY, "native_safety", "Set EStop Off | Get Estop | Set Machine On"},
    {"feed_override_set", V5_NATIVE_GATE_OVERRIDE, "native_linuxcncrsh", "Set Feed_Override"},
    {"spindle_override_set", V5_NATIVE_GATE_OVERRIDE, "native_linuxcncrsh", "Set Spindle_Override"},
    {"first_point", V5_NATIVE_GATE_RUN_CONTROL, "native_first_point", "first point AC_XY_Z"},
    {"settings_apply", V5_NATIVE_GATE_PARAMETER_APPLY, "native_settings_owner", "native memory apply"},
};

const V5NativeGateEntry *v5_native_gate_registry_entries(size_t *count)
{
    if (count) {
        *count = sizeof(k_gates) / sizeof(k_gates[0]);
    }
    return k_gates;
}

size_t v5_native_gate_registry_count(void)
{
    return sizeof(k_gates) / sizeof(k_gates[0]);
}

const V5NativeGateEntry *v5_native_gate_registry_find(const char *id)
{
    size_t i;
    if (!id) {
        return 0;
    }
    for (i = 0; i < v5_native_gate_registry_count(); ++i) {
        if (strcmp(k_gates[i].id, id) == 0) {
            return &k_gates[i];
        }
    }
    return 0;
}
