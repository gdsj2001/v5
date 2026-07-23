#include "v5_native_motion_parameters.h"
#include "v5_native_home_mapping.h"
#include "v5_parameter_owner_map.h"
#include "v5_settings_apply_internal.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V5_LINUXCNCRSH_SECONDS_PER_MINUTE 60.0

#define V5_MOTION_VALUE_VELOCITY 0x01U
#define V5_MOTION_VALUE_ACCELERATION 0x02U
#define V5_MOTION_VALUE_MIN_LIMIT 0x04U
#define V5_MOTION_VALUE_MAX_LIMIT 0x08U
#define V5_MOTION_VALUE_SCALE 0x20U
#define V5_MOTION_VALUE_ALL 0x2fU

static const char k_axes[V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT] = {'X', 'Y', 'Z', 'A', 'B', 'C'};

static void set_code(char *code, size_t code_cap, const char *value)
{
    if (code && code_cap > 0U) {
        snprintf(code, code_cap, "%s", value ? value : "");
    }
}

static char *trim(char *text)
{
    char *end;
    while (text && *text && isspace((unsigned char)*text)) {
        ++text;
    }
    if (!text || !*text) {
        return text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static void uppercase(char *text)
{
    while (text && *text) {
        *text = (char)toupper((unsigned char)*text);
        ++text;
    }
}

static int axis_index(char axis)
{
    unsigned int i;
    axis = (char)toupper((unsigned char)axis);
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        if (k_axes[i] == axis) {
            return (int)i;
        }
    }
    return -1;
}

void v5_native_motion_parameters_init(V5NativeMotionParameters *parameters)
{
    unsigned int i;
    if (!parameters) {
        return;
    }
    memset(parameters, 0, sizeof(*parameters));
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        parameters->axes[i].axis = k_axes[i];
    }
}

static int parse_double(const char *text, double *value)
{
    char *end = 0;
    double parsed;
    if (!text || !value) {
        return 0;
    }
    parsed = strtod(text, &end);
    if (end == text || !isfinite(parsed)) {
        return 0;
    }
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    if (*end && *end != '#' && *end != ';') {
        return 0;
    }
    *value = parsed;
    return 1;
}

static void parse_coordinates(V5NativeMotionParameters *parameters, const char *value)
{
    const char *p = value;
    while (parameters && p && *p) {
        int index;
        if (!isalpha((unsigned char)*p)) {
            ++p;
            continue;
        }
        index = axis_index(*p++);
        if (index >= 0 && !parameters->axes[index].active) {
            parameters->axes[index].active = 1;
            parameters->axes[index].status_slot = parameters->active_axis_count++;
        }
    }
}

static void parse_machine_mode(V5NativeMotionParameters *parameters, const char *value)
{
    char machine[128];
    if (!parameters || !value) {
        return;
    }
    snprintf(machine, sizeof(machine), "%s", value);
    uppercase(machine);
    if (strstr(machine, "PULSE") || strstr(machine, "STEP")) {
        parameters->driver_mode = V5_NATIVE_DRIVER_MODE_PULSE;
    } else if (strstr(machine, "BUS") || strstr(machine, "ETHERCAT")) {
        parameters->driver_mode = V5_NATIVE_DRIVER_MODE_BUS;
    }
}

static void parse_axis_value(
    V5NativeMotionParameters *parameters,
    char section_axis,
    const char *key,
    const char *value)
{
    V5NativeMotionAxisParameters *axis;
    double parsed;
    int index = axis_index(section_axis);
    if (!parameters || index < 0) {
        return;
    }
    axis = &parameters->axes[index];
    if (!parse_double(value, &parsed)) {
        return;
    }
    if (strcmp(key, "MAX_VELOCITY") == 0) {
        axis->max_velocity = parsed;
        axis->valid_mask |= V5_MOTION_VALUE_VELOCITY;
    } else if (strcmp(key, "MAX_ACCELERATION") == 0) {
        axis->max_acceleration = parsed;
        axis->valid_mask |= V5_MOTION_VALUE_ACCELERATION;
    } else if (strcmp(key, "MIN_LIMIT") == 0) {
        axis->min_limit = parsed == 0.0 ? -DBL_MAX : parsed;
        axis->valid_mask |= V5_MOTION_VALUE_MIN_LIMIT;
    } else if (strcmp(key, "MAX_LIMIT") == 0) {
        axis->max_limit = parsed == 0.0 ? DBL_MAX : parsed;
        axis->valid_mask |= V5_MOTION_VALUE_MAX_LIMIT;
    }
}

static void parse_joint_value(
    V5NativeMotionParameters *parameters,
    const char *section,
    const char *key,
    const char *value)
{
    char *end = 0;
    unsigned long joint;
    unsigned int i;
    double scale = 0.0;
    int is_scale;
    if (!parameters || !section || strncmp(section, "JOINT_", 6U) != 0) {
        return;
    }
    is_scale = strcmp(key, "SCALE") == 0 && parse_double(value, &scale) && isfinite(scale) && scale != 0.0;
    if (!is_scale) return;
    joint = strtoul(section + 6, &end, 10);
    if (end == section + 6 || *end || joint >= V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT) {
        return;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        if (axis->active && axis->status_slot == (unsigned int)joint) {
            axis->positioning_resolution_units = 1.0 / fabs(scale);
            axis->valid_mask |= V5_MOTION_VALUE_SCALE;
            return;
        }
    }
}

static int parameters_complete(const V5NativeMotionParameters *parameters)
{
    unsigned int i;
    if (!parameters || parameters->driver_mode == V5_NATIVE_DRIVER_MODE_UNKNOWN ||
        parameters->active_axis_count == 0U) {
        return 0;
    }
    for (i = 0U; i < V5_NATIVE_MOTION_PARAMETER_AXIS_COUNT; ++i) {
        const V5NativeMotionAxisParameters *axis = &parameters->axes[i];
        if (!axis->active) {
            continue;
        }
        if (axis->valid_mask != V5_MOTION_VALUE_ALL || axis->max_velocity <= 0.0 ||
            axis->max_acceleration <= 0.0 || axis->min_limit >= axis->max_limit ||
            axis->positioning_resolution_units <= 0.0) {
            return 0;
        }
    }
    return 1;
}

static int pulse_contract_status(
    const char *json,
    char *status,
    size_t status_cap)
{
    const char *key;
    const char *p;
    const char *end;
    size_t length;
    if (!json || !status || status_cap == 0U) {
        return 0;
    }
    key = strstr(json, "\"status\"");
    if (!key || !(p = strchr(key + 8, ':'))) {
        return 0;
    }
    ++p;
    while (*p && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p++ != '\"') {
        return 0;
    }
    end = strchr(p, '\"');
    if (!end || end == p) {
        return 0;
    }
    length = (size_t)(end - p);
    if (length >= status_cap) {
        return 0;
    }
    memcpy(status, p, length);
    status[length] = '\0';
    return 1;
}

static int load_pulse_contract(
    const char *pulse_contract_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    char *json;
    if (!pulse_contract_path || !pulse_contract_path[0]) {
        set_code(code, code_cap, "PULSE_HOME_CONTRACT_REQUIRED");
        return 0;
    }
    json = v5_settings_apply_read_text_file_limited(pulse_contract_path);
    if (!json || !pulse_contract_status(
            json, parameters->pulse_contract_status,
            sizeof(parameters->pulse_contract_status))) {
        free(json);
        set_code(code, code_cap, "PULSE_HOME_CONTRACT_UNAVAILABLE");
        return 0;
    }
    free(json);
    parameters->pulse_runtime_selectable =
        strcmp(parameters->pulse_contract_status, "runtime_selectable") == 0;
    parameters->runtime_owner_loaded = 1;
    set_code(
        code,
        code_cap,
        parameters->pulse_runtime_selectable
            ? "PULSE_HOME_RUNTIME_OWNER_LOADED"
            : "PULSE_HOME_NOT_RUNTIME_SELECTABLE");
    return 1;
}

int v5_native_motion_parameters_load_runtime_owner(
    const char *settings_project_root,
    const char *settings_runtime_json_path,
    const char *pulse_contract_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    if (!parameters || !parameters->loaded) {
        set_code(code, code_cap, "HOME_RUNTIME_OWNER_PARAMETERS_REQUIRED");
        return 0;
    }
    if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_BUS) {
        return v5_native_home_runtime_owner_load_bus(
            settings_project_root, settings_runtime_json_path,
            parameters, code, code_cap);
    }
    if (parameters->driver_mode == V5_NATIVE_DRIVER_MODE_PULSE) {
        return load_pulse_contract(
            pulse_contract_path, parameters, code, code_cap);
    }
    set_code(code, code_cap, "HOME_RUNTIME_OWNER_MODE_UNKNOWN");
    return 0;
}

int v5_native_motion_parameters_load(
    const char *ini_path,
    V5NativeMotionParameters *parameters,
    char *code,
    size_t code_cap)
{
    FILE *fp;
    char line[512];
    char section[64] = "";
    V5NativeMotionParameters loaded;

    set_code(code, code_cap, "MOTION_PARAMETERS_NOT_ATTEMPTED");
    if (!ini_path || !ini_path[0] || !parameters) {
        set_code(code, code_cap, "MOTION_PARAMETERS_INI_REQUIRED");
        return 0;
    }
    fp = fopen(ini_path, "rb");
    if (!fp) {
        set_code(code, code_cap, "MOTION_PARAMETERS_INI_OPEN_FAILED");
        return 0;
    }
    v5_native_motion_parameters_init(&loaded);
    while (fgets(line, sizeof(line), fp)) {
        char *value;
        char *key = trim(line);
        if (!key || !key[0] || key[0] == '#' || key[0] == ';') {
            continue;
        }
        if (key[0] == '[') {
            char *end = strchr(key, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", trim(key + 1));
                uppercase(section);
            }
            continue;
        }
        value = strchr(key, '=');
        if (!value) {
            continue;
        }
        *value++ = '\0';
        key = trim(key);
        value = trim(value);
        uppercase(key);
        if (strcmp(section, "TRAJ") == 0 && strcmp(key, "COORDINATES") == 0) {
            parse_coordinates(&loaded, value);
        } else if (strcmp(section, "EMC") == 0 && strcmp(key, "MACHINE") == 0) {
            parse_machine_mode(&loaded, value);
        } else if (strncmp(section, "AXIS_", 5U) == 0 && section[5] && !section[6]) {
            parse_axis_value(&loaded, section[5], key, value);
        } else if (strncmp(section, "JOINT_", 6U) == 0) {
            parse_joint_value(&loaded, section, key, value);
        }
    }
    fclose(fp);
    if (!parameters_complete(&loaded)) {
        set_code(code, code_cap, "MOTION_PARAMETERS_INCOMPLETE");
        return 0;
    }
    loaded.loaded = 1;
    *parameters = loaded;
    set_code(code, code_cap, "MOTION_PARAMETERS_LOADED");
    return 1;
}

const V5NativeMotionAxisParameters *v5_native_motion_parameters_axis(
    const V5NativeMotionParameters *parameters,
    char axis)
{
    int index = axis_index(axis);
    if (!parameters || !parameters->loaded || index < 0 || !parameters->axes[index].active) {
        return 0;
    }
    return &parameters->axes[index];
}

int v5_native_motion_parameters_resolve_jog(
    const V5NativeMotionParameters *parameters,
    V5CommandRequest *request,
    char *code,
    size_t code_cap)
{
    const V5NativeMotionAxisParameters *axis;
    double direction;
    if (!request || (request->kind != V5_COMMAND_JOG_INCREMENT &&
                     request->kind != V5_COMMAND_JOG_CONTINUOUS &&
                     request->kind != V5_COMMAND_JOG_STOP)) {
        set_code(code, code_cap, "JOG_PARAMETERS_BAD_REQUEST");
        return 0;
    }
    axis = v5_native_motion_parameters_axis(parameters, request->text_value ? request->text_value[0] : '\0');
    if (!axis) {
        set_code(code, code_cap, "JOG_AXIS_PARAMETERS_UNAVAILABLE");
        return 0;
    }
    if (request->kind == V5_COMMAND_JOG_STOP) {
        set_code(code, code_cap, "JOG_STOP_AXIS_CONFIRMED");
        return 1;
    }
    direction = request->axis_value < 0.0 ? -1.0 : 1.0;
    /*
     * LinuxCNC INI MAX_VELOCITY is stored in user-units/second, while
     * linuxcncrsh Set Jog accepts user-units/minute and converts it back to
     * user-units/second internally.  Keep the native owner in INI units and
     * convert only at the linuxcncrsh request boundary.
     */
    request->axis_value =
        direction * axis->max_velocity * 0.5 *
        V5_LINUXCNCRSH_SECONDS_PER_MINUTE;
    if (!isfinite(request->axis_value) || request->axis_value == 0.0 ||
        !isfinite(axis->max_acceleration) || axis->max_acceleration <= 0.0) {
        set_code(code, code_cap, "JOG_EFFECTIVE_PARAMETERS_INVALID");
        return 0;
    }
    if (code && code_cap > 0U) {
        snprintf(code, code_cap, "JOG_PARAMS_%c_V%.6f_A%.6f", axis->axis,
                 fabs(request->axis_value), axis->max_acceleration);
    }
    return 1;
}

const char *v5_native_driver_mode_text(V5NativeDriverMode mode)
{
    return mode == V5_NATIVE_DRIVER_MODE_BUS ? "bus" :
           mode == V5_NATIVE_DRIVER_MODE_PULSE ? "pulse" : "unknown";
}
