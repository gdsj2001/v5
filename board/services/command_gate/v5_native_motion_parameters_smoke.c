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
    axis = v5_native_motion_parameters_axis(&parameters, 'X');
    if (!axis || !close_enough(axis->max_velocity, 166.666666667) ||
        !close_enough(axis->max_acceleration, 500.0) || axis->status_slot != 0U) {
        return 2;
    }
    axis = v5_native_motion_parameters_axis(&parameters, 'A');
    if (!axis || !close_enough(axis->max_velocity, 833.333333333) ||
        !close_enough(axis->max_acceleration, 2000.0) || axis->status_slot != 3U) {
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
        !v5_native_motion_parameters_axis(&parameters, 'B')) {
        printf("pulse preload failed: %s\n", code);
        return 6;
    }
    printf("v5 native motion parameters smoke passed\n");
    return 0;
}
