#include "v5_native_motion_parameters.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int close_enough(double left, double right)
{
    return fabs(left - right) <= 1.0e-6;
}

int main(void)
{
    V5NativeMotionParameters parameters;
    V5CommandRequest request;
    const V5NativeMotionAxisParameters *axis;
    char code[64];

    if (!v5_native_motion_parameters_load(
            "board/linuxcnc/ini/v5_bus.ini", &parameters, code, sizeof(code)) ||
        parameters.driver_mode != V5_NATIVE_DRIVER_MODE_BUS ||
        parameters.active_axis_count != 5U) {
        printf("bus preload failed: %s\n", code);
        return 1;
    }
    if (!v5_native_motion_parameters_load_runtime_owner(
            "board",
            "board/services/command_gate/testdata/v5_home_settings_runtime.json",
            "board/linuxcnc/components/step_ip_v1_5.contract.json",
            &parameters,
            code,
            sizeof(code)) ||
        !parameters.runtime_owner_loaded) {
        printf("bus Home runtime owner preload failed: %s\n", code);
        return 7;
    }
    axis = v5_native_motion_parameters_axis(&parameters, 'X');
    if (!axis || !close_enough(axis->max_velocity, 166.666666667) ||
        !close_enough(axis->max_acceleration, 500.0) || axis->status_slot != 0U ||
        !close_enough(axis->min_limit, -500.0001) ||
        !close_enough(axis->max_limit, 499.9999) ||
        axis->home_sequence != 1 || !axis->bus_zero_evidence_known ||
        !close_enough(axis->bus_zero_counts, 1000.0) ||
        !close_enough(axis->bus_counts_per_unit, 100.0) ||
        !close_enough(axis->bus_home_reference, 10.0) ||
        !axis->slave_mapping_known || axis->slave_position != 0U) {
        return 2;
    }
    axis = v5_native_motion_parameters_axis(&parameters, 'A');
    if (!axis || !close_enough(axis->max_velocity, 833.333333333) ||
        !close_enough(axis->max_acceleration, 2000.0) || axis->status_slot != 3U ||
        axis->home_sequence != 3 || axis->min_limit > -1.0e98 ||
        axis->max_limit < 1.0e98 || !axis->slave_mapping_known || axis->slave_position != 3U) {
        return 3;
    }
    if (v5_native_motion_parameters_axis(&parameters, 'B')) {
        return 4;
    }
    memset(&request, 0, sizeof(request));
    request.kind = V5_COMMAND_JOG_INCREMENT;
    request.text_value = "X";
    request.axis_value = 1.0;
    request.increment_value = 0.001;
    if (!v5_native_motion_parameters_resolve_jog(
            &parameters, &request, code, sizeof(code)) ||
        !close_enough(request.axis_value, 83.3333333335) ||
        strstr(code, "JOG_PARAMS_X_") != code) {
        return 5;
    }
    if (!v5_native_motion_parameters_load(
            "board/linuxcnc/ini/v5_pulse.ini", &parameters, code, sizeof(code)) ||
        parameters.driver_mode != V5_NATIVE_DRIVER_MODE_PULSE ||
        parameters.active_axis_count != 6U ||
        !(axis = v5_native_motion_parameters_axis(&parameters, 'B')) ||
        axis->min_limit > -1.0e98 || axis->max_limit < 1.0e98) {
        printf("pulse preload failed: %s\n", code);
        return 6;
    }
    if (!v5_native_motion_parameters_load_runtime_owner(
            "board",
            "board/services/command_gate/testdata/v5_home_settings_runtime.json",
            "board/linuxcnc/components/step_ip_v1_5.contract.json",
            &parameters,
            code,
            sizeof(code)) ||
        !parameters.runtime_owner_loaded || parameters.pulse_runtime_selectable ||
        strcmp(parameters.pulse_contract_status, "cold_staged_not_runtime_selectable") != 0 ||
        strcmp(code, "PULSE_HOME_NOT_RUNTIME_SELECTABLE") != 0) {
        printf("pulse contract fail-closed preload failed: %s status=%s\n",
               code, parameters.pulse_contract_status);
        return 8;
    }
    printf("v5 native motion parameters smoke passed\n");
    return 0;
}
