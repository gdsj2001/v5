#include "v5_native_axis_zero_live.h"
#include "v5_native_safety.h"

#include <stdio.h>
#include <string.h>

static V5NativeMotionParameters g_candidate;
static int g_machine_enabled;
static unsigned int g_commit_seq;
static unsigned int g_position_reads;

int usleep(unsigned int delay_us)
{
    (void)delay_us;
    return 0;
}

int v5_native_safety_read_status(V5NativeSafetyResult *result)
{
    memset(result, 0, sizeof(*result));
    result->machine_enable_known = 1;
    result->machine_enabled = g_machine_enabled;
    return V5_NATIVE_SAFETY_SEND_SENT;
}

int v5_native_motion_parameters_load(
    const char *ini_path, V5NativeMotionParameters *parameters,
    char *code, size_t code_cap)
{
    (void)ini_path;
    *parameters = g_candidate;
    snprintf(code, code_cap, "%s", "MOTION_PARAMETERS_LOADED");
    return 1;
}

int v5_native_motion_parameters_load_runtime_owner(
    const char *settings_project_root, const char *settings_runtime_json_path,
    const char *pulse_contract_path, V5NativeMotionParameters *parameters,
    char *code, size_t code_cap)
{
    (void)settings_project_root;
    (void)settings_runtime_json_path;
    (void)pulse_contract_path;
    *parameters = g_candidate;
    snprintf(code, code_cap, "%s", "BUS_HOME_RUNTIME_OWNER_LOADED");
    return 1;
}

int v5_native_home_mapping_project(
    const V5NativeMotionParameters *parameters, unsigned int *commit_seq_out,
    char *code, size_t code_cap)
{
    (void)parameters;
    if (commit_seq_out) *commit_seq_out = ++g_commit_seq;
    snprintf(code, code_cap, "%s", "NATIVE_BUS_MAPPING_PROJECTED");
    return 1;
}

int v5_linuxcncrsh_get_joint_position(
    const V5LinuxcncrshConfig *config, unsigned int joint, double *position_out)
{
    (void)config;
    (void)joint;
    ++g_position_reads;
    *position_out = g_position_reads == 1U ? 12.5 : 0.00005;
    return 1;
}

static void parameters_init(V5NativeMotionParameters *parameters)
{
    unsigned int i;
    memset(parameters, 0, sizeof(*parameters));
    parameters->loaded = 1;
    parameters->driver_mode = V5_NATIVE_DRIVER_MODE_BUS;
    parameters->runtime_owner_loaded = 1;
    parameters->mapping_generation = 17U;
    parameters->active_axis_count = 5U;
    for (i = 0U; i < 5U; ++i) {
        parameters->axes[i].axis = "XYZAC"[i];
        parameters->axes[i].active = 1;
        parameters->axes[i].status_slot = i;
        parameters->axes[i].slave_position = i;
        parameters->axes[i].slave_mapping_known = 1;
        parameters->axes[i].positioning_resolution_units = 0.0001;
        parameters->axes[i].bus_zero_evidence_known = 1;
        parameters->axes[i].bus_counts_per_unit = 10000.0;
    }
    parameters->axes[5].axis = 'B';
}

int main(void)
{
    V5NativeMotionParameters resident;
    V5NativeAxisZeroLiveResult result;
    V5LinuxcncrshConfig config = {0};
    parameters_init(&resident);
    resident.axes[3].min_limit = -5.0;
    g_candidate = resident;
    g_candidate.axes[3].bus_zero_counts = 12345.0;
    g_candidate.axes[3].bus_home_reference = 1.2345;
    g_candidate.axes[3].min_limit = -99.0;
    g_machine_enabled = 1;
    if (v5_native_axis_zero_live_apply(
            &config, "runtime.ini", "project", "settings.json", "pulse.json",
            &resident, 'A', 3U, 0.0, &result) ||
        strcmp(result.code, "SETTINGS_AXIS_ZERO_MACHINE_OFF_REQUIRED") != 0) {
        return 1;
    }
    g_machine_enabled = 0;
    g_position_reads = 0U;
    if (!v5_native_axis_zero_live_apply(
            &config, "runtime.ini", "project", "settings.json", "pulse.json",
            &resident, 'A', 3U, 0.0, &result) ||
        !result.display_verified || result.commit_seq == 0U ||
        g_position_reads < 3U || result.previous_mcs_position != 12.5 ||
        resident.axes[3].bus_zero_counts != 12345.0 ||
        resident.axes[3].bus_home_reference != 1.2345 ||
        resident.axes[3].min_limit != -5.0 ||
        strcmp(result.code, "SETTINGS_COUNT_DOMAIN_ZERO_LIVE_APPLIED_RESTART_REQUIRED") != 0) {
        return 2;
    }
    g_candidate.axes[3].positioning_resolution_units = 0.001;
    if (v5_native_axis_zero_live_apply(
            &config, "runtime.ini", "project", "settings.json", "pulse.json",
            &resident, 'A', 3U, 0.0, &result) ||
        strcmp(result.code, "SETTINGS_AXIS_ZERO_SCALE_OR_MAPPING_CHANGED_RESTART_REQUIRED") != 0) {
        return 3;
    }
    puts("v5 native axis zero live smoke passed");
    return 0;
}
