#include "v5_native_motion_parameters.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int close_enough(double left, double right)
{
    return fabs(left - right) <= 1.0e-6;
}

static int physical_slave_zero_owner_smoke(void)
{
    V5NativeMotionParameters parameters;
    V5NativeMotionAxisParameters *axis_a = 0;
    V5NativeMotionAxisParameters *axis_b = 0;
    const V5NativeMotionAxisParameters *readback_x;
    const V5NativeMotionAxisParameters *readback_y;
    const V5NativeMotionAxisParameters *readback_b;
    char temp_root[] = "/tmp/v5-home-slave-owner-XXXXXX";
    char config_dir[256];
    char settings_dir[256];
    char table_path[256];
    char runtime_path[256];
    char code[64];
    FILE *fp = 0;
    unsigned int i;
    int ok = 0;

    if (!mkdtemp(temp_root)) {
        return 0;
    }
    snprintf(config_dir, sizeof(config_dir), "%s/config", temp_root);
    snprintf(settings_dir, sizeof(settings_dir), "%s/config/settings", temp_root);
    snprintf(table_path, sizeof(table_path), "%s/self_parameter_table.tsv", settings_dir);
    snprintf(runtime_path, sizeof(runtime_path), "%s/settings_runtime.json", temp_root);
    if (mkdir(config_dir, 0755) != 0 || mkdir(settings_dir, 0755) != 0 ||
        !(fp = fopen(table_path, "wb"))) {
        goto cleanup;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "X\tslave\t0\n"
          "Y\tslave\t1\n"
          "Z\tslave\t2\n"
          "A\tslave\tNAT\n"
          "B\tslave\t4\n"
          "C\tslave\t3\n", fp);
    if (fclose(fp) != 0) {
        fp = 0;
        goto cleanup;
    }
    fp = 0;
    if (!v5_native_motion_parameters_load(
            "board/linuxcnc/ini/v5_bus.ini", &parameters, code, sizeof(code))) {
        goto cleanup;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (parameters.axes[i].axis == 'A') {
            axis_a = &parameters.axes[i];
        } else if (parameters.axes[i].axis == 'B') {
            axis_b = &parameters.axes[i];
        }
    }
    if (!axis_a || !axis_b) {
        goto cleanup;
    }
    axis_a->active = 0;
    axis_b->active = 1;
    axis_b->status_slot = 3U;
    if (!v5_native_motion_parameters_load_runtime_owner(
            temp_root,
            "board/services/command_gate/testdata/v5_home_settings_runtime.json",
            "board/linuxcnc/components/step_ip_v1_5.contract.json",
            &parameters,
            code,
            sizeof(code)) ||
        !(readback_b = v5_native_motion_parameters_axis(&parameters, 'B')) ||
        !readback_b->slave_mapping_known ||
        readback_b->slave_position != 4U ||
        !readback_b->bus_zero_evidence_known ||
        !close_enough(readback_b->bus_zero_counts, 5000.0) ||
        !close_enough(readback_b->bus_counts_per_unit, 100.0) ||
        !close_enough(readback_b->bus_home_reference, 50.0)) {
        goto cleanup;
    }
    if (!(fp = fopen(runtime_path, "wb"))) {
        goto cleanup;
    }
    fputs("{\"axes\":["
          "{\"axis\":\"X\",\"zero_model\":{\"zero_counts\":1000,"
          "\"counts_per_unit\":100,\"raw_zero_position\":99,\"slave_position\":0}},"
          "{\"axis\":\"Y\",\"zero_model\":{\"zero_counts\":2000,"
          "\"counts_per_unit\":100,\"raw_zero_position\":20,\"slave_position\":1}}"
          "]}\n", fp);
    if (fclose(fp) != 0) {
        fp = 0;
        goto cleanup;
    }
    fp = 0;
    if (!v5_native_motion_parameters_load(
            "board/linuxcnc/ini/v5_bus.ini", &parameters, code, sizeof(code))) {
        goto cleanup;
    }
    axis_a = 0;
    axis_b = 0;
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (parameters.axes[i].axis == 'A') {
            axis_a = &parameters.axes[i];
        } else if (parameters.axes[i].axis == 'B') {
            axis_b = &parameters.axes[i];
        }
    }
    if (!axis_a || !axis_b) {
        goto cleanup;
    }
    axis_a->active = 0;
    axis_b->active = 1;
    axis_b->status_slot = 3U;
    if (!v5_native_motion_parameters_load_runtime_owner(
            temp_root,
            runtime_path,
            "board/linuxcnc/components/step_ip_v1_5.contract.json",
            &parameters,
            code,
            sizeof(code)) ||
        strcmp(code, "BUS_HOME_RUNTIME_OWNER_LOADED_PARTIAL") != 0 ||
        !(readback_x = v5_native_motion_parameters_axis(&parameters, 'X')) ||
        !(readback_y = v5_native_motion_parameters_axis(&parameters, 'Y')) ||
        readback_x->bus_zero_evidence_known ||
        !readback_y->bus_zero_evidence_known ||
        !close_enough(readback_y->bus_zero_counts, 2000.0) ||
        !close_enough(readback_y->bus_counts_per_unit, 100.0) ||
        !close_enough(readback_y->bus_home_reference, 20.0)) {
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (fp) {
        fclose(fp);
    }
    unlink(runtime_path);
    unlink(table_path);
    rmdir(settings_dir);
    rmdir(config_dir);
    rmdir(temp_root);
    return ok;
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
        !axis->bus_zero_evidence_known ||
        !close_enough(axis->bus_zero_counts, 1000.0) ||
        !close_enough(axis->bus_counts_per_unit, 100.0) ||
        !close_enough(axis->bus_home_reference, 10.0) ||
        !axis->slave_mapping_known || axis->slave_position != 0U) {
        return 2;
    }
    axis = v5_native_motion_parameters_axis(&parameters, 'A');
    if (!axis || !close_enough(axis->max_velocity, 833.333333333) ||
        !close_enough(axis->max_acceleration, 2000.0) || axis->status_slot != 3U ||
        axis->min_limit > -1.0e98 ||
        axis->max_limit < 1.0e98 || !axis->slave_mapping_known || axis->slave_position != 3U) {
        return 3;
    }
    if (v5_native_motion_parameters_axis(&parameters, 'B')) {
        return 4;
    }
    if (!physical_slave_zero_owner_smoke()) {
        printf("physical-slave zero owner remap failed\n");
        return 9;
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
