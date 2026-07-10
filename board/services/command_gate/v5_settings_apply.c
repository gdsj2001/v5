#include "v5_settings_apply.h"

#include "v5_parameter_owner_map.h"
#include "v5_motion_model_registry.h"
#include "v5_settings_parameter_store.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define V5_SETTINGS_APPLY_MAX_FILE_BYTES (512U * 1024U)
#define V5_SETTINGS_RUNTIME_JSON_DEFAULT "/opt/8ax/phase0_bus5/settings_runtime.json"

static int field_is_scale_chain(const char *field_name)
{
    return field_name &&
           (strstr(field_name, "_precision") != 0 ||
            strstr(field_name, "_pitch") != 0 ||
            strstr(field_name, "_motor_rev") != 0 ||
            strstr(field_name, "_load_rev") != 0);
}

static int parse_positive_finite(const char *text)
{
    char *endp;
    double value;
    if (!text || !text[0]) {
        return 0;
    }
    value = strtod(text, &endp);
    return endp != text && *endp == '\0' && isfinite(value) && value > 0.0;
}

static void scale_chain_result_code(V5SettingsApplyScaleChainResult *result, const char *code)
{
    if (!result || !code) {
        return;
    }
    snprintf(result->code, sizeof(result->code), "%s", code);
}

static double settings_apply_nearest_integer(double value);

static int file_exists(const char *path)
{
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *read_text_file_limited(const char *path)
{
    FILE *fp;
    long size;
    char *text;
    if (!path || !path[0]) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size < 0 || (unsigned long)size > V5_SETTINGS_APPLY_MAX_FILE_BYTES) {
        fclose(fp);
        return 0;
    }
    rewind(fp);
    text = (char *)malloc((size_t)size + 1U);
    if (!text) {
        fclose(fp);
        return 0;
    }
    if (size > 0 && fread(text, 1U, (size_t)size, fp) != (size_t)size) {
        free(text);
        fclose(fp);
        return 0;
    }
    text[size] = '\0';
    fclose(fp);
    return text;
}

static const char *bounded_strstr(const char *start, const char *end, const char *needle)
{
    size_t needle_len;
    const char *p;
    if (!start || !end || !needle || start > end) {
        return 0;
    }
    needle_len = strlen(needle);
    if (needle_len == 0U) {
        return start;
    }
    for (p = start; p + needle_len <= end; ++p) {
        if (memcmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return 0;
}

static const char *json_skip_ws(const char *p, const char *end)
{
    while (p && p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static const char *json_matching_delim(const char *open, const char *end, char open_ch, char close_ch)
{
    const char *p;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    if (!open || open >= end || *open != open_ch) {
        return 0;
    }
    for (p = open; p < end; ++p) {
        char ch = *p;
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (ch == '\\') {
                escaped = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == open_ch) {
            ++depth;
        } else if (ch == close_ch) {
            --depth;
            if (depth == 0) {
                return p;
            }
        }
    }
    return 0;
}

static const char *json_value_for_key(const char *start, const char *end, const char *key)
{
    char pattern[96];
    const char *p;
    int n;
    if (!start || !end || !key || start >= end) {
        return 0;
    }
    n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(pattern)) {
        return 0;
    }
    p = start;
    while ((p = bounded_strstr(p, end, pattern)) != 0) {
        const char *colon = json_skip_ws(p + strlen(pattern), end);
        if (colon && colon < end && *colon == ':') {
            return json_skip_ws(colon + 1, end);
        }
        p += strlen(pattern);
    }
    return 0;
}

static int json_string_value(const char *start, const char *end, const char *key, char *out, size_t out_cap)
{
    const char *p = json_value_for_key(start, end, key);
    size_t len = 0U;
    if (!p || p >= end || *p != '"' || !out || out_cap == 0U) {
        return 0;
    }
    ++p;
    while (p < end && *p && *p != '"' && len + 1U < out_cap) {
        if (*p == '\\') {
            return 0;
        }
        out[len++] = *p++;
    }
    if (p >= end || *p != '"') {
        return 0;
    }
    out[len] = '\0';
    return 1;
}

static int json_number_value(const char *start, const char *end, const char *key, double *out)
{
    const char *p = json_value_for_key(start, end, key);
    char *after;
    double value;
    if (!p || p >= end || !out) {
        return 0;
    }
    value = strtod(p, &after);
    if (after == p || after > end || !isfinite(value)) {
        return 0;
    }
    *out = value;
    return 1;
}

static int json_object_for_key(const char *start, const char *end, const char *key, const char **obj_start, const char **obj_end)
{
    const char *p = json_value_for_key(start, end, key);
    const char *close;
    if (!p || p >= end || *p != '{' || !obj_start || !obj_end) {
        return 0;
    }
    close = json_matching_delim(p, end, '{', '}');
    if (!close) {
        return 0;
    }
    *obj_start = p;
    *obj_end = close + 1;
    return 1;
}

static int settings_runtime_axis_object(const char *json, const char *axis, const char **axis_start, const char **axis_end)
{
    const char *end;
    const char *axes;
    const char *array_end;
    const char *p;
    if (!json || !axis || !axis[0] || !axis_start || !axis_end) {
        return 0;
    }
    end = json + strlen(json);
    axes = json_value_for_key(json, end, "axes");
    if (!axes || axes >= end || *axes != '[') {
        return 0;
    }
    array_end = json_matching_delim(axes, end, '[', ']');
    if (!array_end) {
        return 0;
    }
    p = axes + 1;
    while (p < array_end) {
        const char *obj_start;
        const char *obj_end;
        char axis_value[32];
        p = json_skip_ws(p, array_end);
        if (!p || p >= array_end) {
            break;
        }
        if (*p != '{') {
            ++p;
            continue;
        }
        obj_start = p;
        obj_end = json_matching_delim(obj_start, array_end, '{', '}');
        if (!obj_end) {
            return 0;
        }
        if (json_string_value(obj_start, obj_end + 1, "axis", axis_value, sizeof(axis_value)) &&
            strcmp(axis_value, axis) == 0) {
            *axis_start = obj_start;
            *axis_end = obj_end + 1;
            return 1;
        }
        p = obj_end + 1;
    }
    return 0;
}

static int build_runtime_ini_path(char *out, size_t out_cap, const char *project_root)
{
    int n;
    const char *root = (project_root && project_root[0]) ? project_root : ".";
    if (!out || out_cap == 0U) {
        return 0;
    }
    n = snprintf(out, out_cap, "%s/%s", root, "linuxcnc/ini/v5_bus.ini");
    return n > 0 && (size_t)n < out_cap;
}

static int ini_section_line(const char *raw, char *section, size_t section_cap)
{
    char probe[128];
    if (!raw || !section || section_cap == 0U) {
        return 0;
    }
    snprintf(probe, sizeof(probe), "%s", raw);
    {
        char *p = probe;
        char *end;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
            --end;
        }
        *end = '\0';
        if (*p != '[') {
            return 0;
        }
        return sscanf(p, "[%63[^]]]", section) == 1;
    }
}

static int ini_key_line(const char *raw, const char *key, const char **value_start)
{
    const char *eq;
    char probe[128];
    char *end;
    if (!raw || !key) {
        return 0;
    }
    eq = strchr(raw, '=');
    if (!eq) {
        return 0;
    }
    if ((size_t)(eq - raw) >= sizeof(probe)) {
        return 0;
    }
    memcpy(probe, raw, (size_t)(eq - raw));
    probe[eq - raw] = '\0';
    end = probe + strlen(probe);
    while (end > probe && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
    while (probe[0] == ' ' || probe[0] == '\t') {
        memmove(probe, probe + 1, strlen(probe));
    }
    if (strcmp(probe, key) != 0) {
        return 0;
    }
    if (value_start) {
        const char *value = eq + 1;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        *value_start = value;
    }
    return 1;
}

static int ini_read_section_number(const char *path, const char *section_name, const char *key, double *out)
{
    FILE *fp;
    char raw[512];
    int in_section = 0;
    if (!path || !section_name || !key || !out) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    while (fgets(raw, sizeof(raw), fp)) {
        char section[64];
        const char *value_start;
        if (ini_section_line(raw, section, sizeof(section))) {
            in_section = strcmp(section, section_name) == 0;
            continue;
        }
        if (in_section && ini_key_line(raw, key, &value_start)) {
            char *after;
            double value = strtod(value_start, &after);
            fclose(fp);
            if (after == value_start || !isfinite(value)) {
                return 0;
            }
            *out = value;
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static int ini_read_preferred_number(const char *path, const char *primary_section, const char *fallback_section, const char *key, double *out)
{
    if (ini_read_section_number(path, primary_section, key, out)) {
        return 1;
    }
    return ini_read_section_number(path, fallback_section, key, out);
}

static int ini_write_scale_and_limits(
    const char *path,
    const char *axis_section,
    const char *joint_section,
    int write_scale,
    double scale,
    double raw_min,
    double raw_max)
{
    FILE *in;
    FILE *out;
    char tmp_path[512];
    char raw[512];
    char section[64] = "";
    int touched_axis_min = 0;
    int touched_axis_max = 0;
    int touched_joint_min = 0;
    int touched_joint_max = 0;
    int touched_scale = 0;
    int n;

    if (!path || !axis_section || !joint_section || !isfinite(raw_min) || !isfinite(raw_max) || raw_min >= raw_max) {
        return 0;
    }
    if (write_scale && (!isfinite(scale) || scale <= 0.0)) {
        return 0;
    }
    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return 0;
    }
    in = fopen(path, "rb");
    if (!in) {
        return 0;
    }
    out = fopen(tmp_path, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while (fgets(raw, sizeof(raw), in)) {
        char next_section[64];
        if (ini_section_line(raw, next_section, sizeof(next_section))) {
            snprintf(section, sizeof(section), "%s", next_section);
            fputs(raw, out);
            continue;
        }
        if (strcmp(section, axis_section) == 0 || strcmp(section, joint_section) == 0) {
            if (ini_key_line(raw, "MIN_LIMIT", 0)) {
                fprintf(out, "MIN_LIMIT = %.12g\n", raw_min);
                if (strcmp(section, axis_section) == 0) touched_axis_min = 1;
                if (strcmp(section, joint_section) == 0) touched_joint_min = 1;
                continue;
            }
            if (ini_key_line(raw, "MAX_LIMIT", 0)) {
                fprintf(out, "MAX_LIMIT = %.12g\n", raw_max);
                if (strcmp(section, axis_section) == 0) touched_axis_max = 1;
                if (strcmp(section, joint_section) == 0) touched_joint_max = 1;
                continue;
            }
            if (write_scale && strcmp(section, joint_section) == 0 && ini_key_line(raw, "SCALE", 0)) {
                fprintf(out, "SCALE = %.12g\n", scale);
                touched_scale = 1;
                continue;
            }
        }
        fputs(raw, out);
    }
    fclose(in);
    if (fclose(out) != 0) {
        remove(tmp_path);
        return 0;
    }
    if (!(touched_axis_min && touched_axis_max) && !(touched_joint_min && touched_joint_max)) {
        remove(tmp_path);
        return 0;
    }
    if (write_scale && !touched_scale) {
        remove(tmp_path);
        return 0;
    }
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return 0;
    }
    return 1;
}

static int axis_is_rotary(const char *axis)
{
    return axis && axis[1] == '\0' && (axis[0] == 'A' || axis[0] == 'B' || axis[0] == 'C');
}

static int compute_scale_from_chain(
    const char *ini_path,
    const char *axis_section,
    const char *joint_section,
    const char *axis_obj_start,
    const char *axis_obj_end,
    const char *axis,
    double *scale_out)
{
    double pitch = 0.0;
    double motor_rev = 0.0;
    double load_rev = 0.0;
    double ratio = 0.0;
    double counts = 0.0;
    double rotary_counts = 0.0;
    if (!scale_out) {
        return 0;
    }
    if (json_number_value(axis_obj_start, axis_obj_end, "motor_revs_per_load_rev", &ratio) ||
        json_number_value(axis_obj_start, axis_obj_end, "reducer_ratio", &ratio)) {
        /* ratio loaded from drive-only evidence */
    }
    if (ini_read_preferred_number(ini_path, axis_section, joint_section, "MOTOR_REV", &motor_rev) &&
        ini_read_preferred_number(ini_path, axis_section, joint_section, "LOAD_REV", &load_rev) &&
        motor_rev > 0.0 && load_rev > 0.0) {
        ratio = motor_rev / load_rev;
    }
    if (axis_is_rotary(axis)) {
        if (json_number_value(axis_obj_start, axis_obj_end, "rotary_load_counts_per_rev", &rotary_counts) &&
            rotary_counts > 0.0) {
            *scale_out = rotary_counts / 360.0;
            return isfinite(*scale_out) && *scale_out > 0.0;
        }
    } else {
        if (!ini_read_preferred_number(ini_path, axis_section, joint_section, "PITCH", &pitch) || pitch <= 0.0) {
            return 0;
        }
    }
    if (!(json_number_value(axis_obj_start, axis_obj_end, "actual_counts_per_motor_rev", &counts) ||
          json_number_value(axis_obj_start, axis_obj_end, "drive_command_counts_per_motor_rev", &counts) ||
          json_number_value(axis_obj_start, axis_obj_end, "target_command_counts_per_motor_rev", &counts) ||
          json_number_value(axis_obj_start, axis_obj_end, "feedback_counts_per_motor_rev", &counts)) ||
        counts <= 0.0 || ratio <= 0.0) {
        return 0;
    }
    *scale_out = axis_is_rotary(axis) ? (counts * ratio) / 360.0 : (counts * ratio) / pitch;
    return isfinite(*scale_out) && *scale_out > 0.0;
}

int v5_settings_apply_prepare(
    const V5SettingsApplyRequest *request,
    V5SettingsApplyResult *result)
{
    V5ParameterOwnerRecord record;

    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!request || !request->field_name || !request->field_name[0] ||
        !request->value_text || !request->value_text[0] ||
        request->owner_generation == 0U || request->readback_token == 0U) {
        return 0;
    }
    if (!v5_parameter_owner_lookup(request->field_name, &record) || !record.field->writable) {
        return 0;
    }
    if (v5_parameter_table_field_uses_shm(request->field_name)) {
        return 0;
    }
    if (field_is_scale_chain(request->field_name) && !parse_positive_finite(request->value_text)) {
        return 0;
    }
    if (result) {
        result->status = V5_SETTINGS_APPLY_ACCEPTED;
        result->write_owner = record.write_owner;
        result->readback_owner = record.readback_owner;
        result->restart_required = record.field->restart_required;
        result->drive_only_allowed = record.field->drive_only_allowed;
        result->scale_chain_transaction_required = field_is_scale_chain(request->field_name);
        result->raw_limits_recompute_required = result->scale_chain_transaction_required;
    }
    return 1;
}

int v5_settings_apply_scale_chain_commit(
    const char *project_root,
    const char *settings_runtime_json_path,
    const char *axis,
    unsigned int axis_index,
    const char *field_name,
    V5SettingsApplyScaleChainResult *result)
{
    char ini_path[512];
    char axis_section[32];
    char joint_section[32];
    const char *runtime_path;
    char *json;
    const char *axis_obj_start = 0;
    const char *axis_obj_end = 0;
    const char *zero_obj_start = 0;
    const char *zero_obj_end = 0;
    double zero_counts = 0.0;
    double old_zero = 0.0;
    double current_scale = 0.0;
    double effective_scale = 0.0;
    double chain_scale = 0.0;
    double raw_min_current = 0.0;
    double raw_max_current = 0.0;
    double min_distance;
    double max_distance;
    double new_zero;
    double new_min;
    double new_max;
    int precision_field;
    int write_scale = 0;

    if (result) {
        memset(result, 0, sizeof(*result));
        scale_chain_result_code(result, "SCALE_CHAIN_NOT_ATTEMPTED");
    }
    if (!axis || !axis[0] || !field_name || !field_is_scale_chain(field_name)) {
        if (result) {
            result->skipped = 1;
            scale_chain_result_code(result, "SCALE_CHAIN_NOT_REQUIRED");
        }
        return 1;
    }
    if (result) {
        result->attempted = 1;
    }
    if (!build_runtime_ini_path(ini_path, sizeof(ini_path), project_root)) {
        scale_chain_result_code(result, "RUNTIME_INI_PATH_INVALID");
        return 0;
    }
    runtime_path = settings_runtime_json_path;
    if (!runtime_path || !runtime_path[0]) {
        runtime_path = getenv("V5_SETTINGS_RUNTIME_JSON");
    }
    if (!runtime_path || !runtime_path[0]) {
        runtime_path = V5_SETTINGS_RUNTIME_JSON_DEFAULT;
    }
    if (!file_exists(runtime_path)) {
        if (result) {
            result->skipped = 1;
            scale_chain_result_code(result, "SETTINGS_RUNTIME_ZERO_MODEL_ABSENT");
        }
        return 1;
    }
    json = read_text_file_limited(runtime_path);
    if (!json) {
        scale_chain_result_code(result, "SETTINGS_RUNTIME_READ_FAILED");
        return 0;
    }
    snprintf(axis_section, sizeof(axis_section), "AXIS_%s", axis);
    snprintf(joint_section, sizeof(joint_section), "JOINT_%u", axis_index);
    if (!settings_runtime_axis_object(json, axis, &axis_obj_start, &axis_obj_end) ||
        !json_object_for_key(axis_obj_start, axis_obj_end, "zero_model", &zero_obj_start, &zero_obj_end)) {
        free(json);
        if (result) {
            result->skipped = 1;
            scale_chain_result_code(result, "SETTINGS_RUNTIME_ZERO_MODEL_ABSENT");
        }
        return 1;
    }
    if (result) {
        result->zero_model_present = 1;
    }
    if (!(json_number_value(zero_obj_start, zero_obj_end, "zero_anchor_counts", &zero_counts) ||
          json_number_value(zero_obj_start, zero_obj_end, "actual_counts", &zero_counts) ||
          json_number_value(zero_obj_start, zero_obj_end, "zero_counts", &zero_counts) ||
          json_number_value(zero_obj_start, zero_obj_end, "actual_position_counts", &zero_counts))) {
        free(json);
        scale_chain_result_code(result, "ZERO_MODEL_COUNTS_MISSING");
        return 0;
    }
    if (!ini_read_preferred_number(ini_path, joint_section, axis_section, "SCALE", &current_scale) ||
        current_scale <= 0.0 || !isfinite(current_scale)) {
        free(json);
        scale_chain_result_code(result, "RUNTIME_SCALE_MISSING");
        return 0;
    }
    effective_scale = current_scale;
    precision_field = strstr(field_name, "_precision") != 0;
    if (!precision_field &&
        compute_scale_from_chain(ini_path, axis_section, joint_section, axis_obj_start, axis_obj_end, axis, &chain_scale)) {
        effective_scale = chain_scale;
        write_scale = fabs(chain_scale - current_scale) > 1.0e-9;
    }
    if (!json_number_value(zero_obj_start, zero_obj_end, "raw_zero_position", &old_zero)) {
        old_zero = zero_counts / current_scale;
    }
    if (!(ini_read_section_number(ini_path, joint_section, "MIN_LIMIT", &raw_min_current) &&
          ini_read_section_number(ini_path, joint_section, "MAX_LIMIT", &raw_max_current)) &&
        !(ini_read_section_number(ini_path, axis_section, "MIN_LIMIT", &raw_min_current) &&
          ini_read_section_number(ini_path, axis_section, "MAX_LIMIT", &raw_max_current))) {
        free(json);
        scale_chain_result_code(result, "RAW_LIMIT_CURRENT_MISSING");
        return 0;
    }
    if (old_zero < raw_min_current || old_zero > raw_max_current) {
        free(json);
        scale_chain_result_code(result, "RAW_ZERO_OUTSIDE_LIMITS");
        return 0;
    }
    min_distance = settings_apply_nearest_integer(raw_min_current - old_zero);
    max_distance = settings_apply_nearest_integer(raw_max_current - old_zero);
    new_zero = zero_counts / effective_scale;
    new_min = settings_apply_nearest_integer(new_zero + min_distance);
    new_max = settings_apply_nearest_integer(new_zero + max_distance);
    if (!isfinite(new_zero) || !isfinite(new_min) || !isfinite(new_max) || new_min >= new_max) {
        free(json);
        scale_chain_result_code(result, "RAW_LIMIT_RECOMPUTE_INVALID");
        return 0;
    }
    if (!ini_write_scale_and_limits(ini_path, axis_section, joint_section, write_scale, effective_scale, new_min, new_max)) {
        free(json);
        scale_chain_result_code(result, "RAW_LIMIT_WRITE_FAILED");
        return 0;
    }
    free(json);
    if (result) {
        result->scale_recomputed = write_scale ? 1 : 0;
        result->raw_limits_recomputed = 1;
        result->effective_scale = effective_scale;
        result->raw_zero_position = new_zero;
        result->raw_min_limit = new_min;
        result->raw_max_limit = new_max;
        scale_chain_result_code(result, "SCALE_CHAIN_RAW_LIMITS_RECOMPUTED");
    }
    return 1;
}

static void settings_apply_trim(char *text)
{
    char *start = text;
    char *end;
    if (!text) {
        return;
    }
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    *end = '\0';
}

static int settings_apply_numeric_text(const char *value)
{
    char buf[64];
    char *end;
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    settings_apply_trim(buf);
    if (!buf[0]) {
        return 0;
    }
    (void)strtod(buf, &end);
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    return *end == '\0';
}

static int settings_apply_integer_text(const char *value)
{
    char buf[64];
    const char *p;
    if (!value || !value[0] || strlen(value) >= sizeof(buf) ||
        strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    settings_apply_trim(buf);
    p = buf;
    if (*p == '+' || *p == '-') {
        ++p;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    while (*p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int settings_apply_axis_field_requires_integer_value(const char *field_key)
{
    return field_key &&
           (strcmp(field_key, "pitch") == 0 ||
            strcmp(field_key, "motor_rev") == 0 ||
            strcmp(field_key, "load_rev") == 0 ||
            strcmp(field_key, "soft_minus") == 0 ||
            strcmp(field_key, "soft_plus") == 0 ||
            strcmp(field_key, "max_velocity") == 0 ||
            strcmp(field_key, "max_acceleration") == 0 ||
            strcmp(field_key, "backlash") == 0);
}

static int settings_apply_safe_ini_value(const char *value)
{
    char buf[64];
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    settings_apply_trim(buf);
    return buf[0] && strchr(buf, '[') == 0 && strchr(buf, ']') == 0;
}

static int settings_apply_values_match(const char *actual, const char *expected)
{
    char *ea;
    char *eb;
    double da;
    double db;
    if (!actual || !expected) {
        return 0;
    }
    if (strcmp(actual, expected) == 0) {
        return 1;
    }
    da = strtod(actual, &ea);
    db = strtod(expected, &eb);
    return ea != actual && eb != expected && *ea == '\0' && *eb == '\0' && da == db;
}

static void settings_apply_format_double(char *out, size_t cap, double value)
{
    if (!out || cap == 0U) {
        return;
    }
    snprintf(out, cap, "%.12g", value);
}

static double settings_apply_nearest_integer(double value)
{
    double rounded = (double)((long long)(value >= 0.0 ? value + 0.5 : value - 0.5));
    if (rounded == 0.0) {
        rounded = 0.0;
    }
    return rounded;
}

static void settings_apply_format_integer(char *out, size_t cap, double value)
{
    if (!out || cap == 0U || !isfinite(value)) {
        return;
    }
    snprintf(out, cap, "%.0f", settings_apply_nearest_integer(value));
}

static int settings_apply_mode_to_ini(const char *value, char *out, size_t out_cap)
{
    if (!value || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(value, "直线") == 0 || strcmp(value, "LINEAR") == 0) {
        snprintf(out, out_cap, "LINEAR");
    } else if (strcmp(value, "旋转") == 0 || strcmp(value, "ANGULAR") == 0) {
        snprintf(out, out_cap, "ANGULAR");
    } else if (strcmp(value, "虚拟") == 0 || strcmp(value, "VIRTUAL") == 0) {
        snprintf(out, out_cap, "VIRTUAL");
    } else {
        return 0;
    }
    return out[0] != '\0';
}

static int settings_apply_mode_to_display(const char *value, char *out, size_t out_cap)
{
    if (!value || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(value, "ANGULAR") == 0) {
        snprintf(out, out_cap, "旋转");
    } else if (strcmp(value, "VIRTUAL") == 0) {
        snprintf(out, out_cap, "虚拟");
    } else if (strcmp(value, "LINEAR") == 0) {
        snprintf(out, out_cap, "直线");
    } else {
        snprintf(out, out_cap, "%s", value);
    }
    return out[0] != '\0';
}

static int settings_apply_ini_value_for_field(
    const char *field_key,
    const char *value,
    char *ini_value,
    size_t ini_cap,
    char *expected_display,
    size_t display_cap)
{
    char local[64];
    char *end;
    double numeric;
    if (!field_key || !value || !value[0] || !ini_value || ini_cap == 0U ||
        !expected_display || display_cap == 0U) {
        return 0;
    }
    ini_value[0] = '\0';
    expected_display[0] = '\0';
    snprintf(local, sizeof(local), "%s", value);
    settings_apply_trim(local);
    if (!local[0]) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        if (!settings_apply_mode_to_ini(local, ini_value, ini_cap)) {
            return 0;
        }
        return settings_apply_mode_to_display(ini_value, expected_display, display_cap);
    }
    if (strcmp(field_key, "home_direction") == 0) {
        if (strcmp(local, "+") == 0) {
            snprintf(ini_value, ini_cap, "1");
        } else if (strcmp(local, "-") == 0) {
            snprintf(ini_value, ini_cap, "-1");
        } else if (strcmp(local, "0") == 0) {
            snprintf(ini_value, ini_cap, "0");
        } else {
            return 0;
        }
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (strcmp(field_key, "home_order") == 0) {
        if (strcmp(local, "禁用") == 0) {
            snprintf(ini_value, ini_cap, "-1");
        } else if (!settings_apply_numeric_text(local)) {
            return 0;
        } else {
            snprintf(ini_value, ini_cap, "%s", local);
        }
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (strcmp(field_key, "precision") == 0) {
        if (!settings_apply_numeric_text(local)) {
            return 0;
        }
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || numeric <= 0.0) {
            return 0;
        }
        settings_apply_format_double(ini_value, ini_cap, 1.0 / numeric);
        settings_apply_format_double(expected_display, display_cap, numeric);
        return 1;
    }
    if (strcmp(field_key, "direction_mode") == 0) {
        if (strcmp(local, "cw") != 0 && strcmp(local, "ccw") != 0) {
            return 0;
        }
        snprintf(ini_value, ini_cap, "%s", local);
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (!settings_apply_numeric_text(local)) {
        return 0;
    }
    if (settings_apply_axis_field_requires_integer_value(field_key) &&
        !settings_apply_integer_text(local)) {
        return 0;
    }
    if (strcmp(field_key, "max_velocity") == 0) {
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_double(ini_value, ini_cap, numeric / 60.0);
        settings_apply_format_integer(expected_display, display_cap, numeric);
        return 1;
    }
    snprintf(ini_value, ini_cap, "%s", local);
    if (settings_apply_axis_field_requires_integer_value(field_key)) {
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(expected_display, display_cap, numeric);
    } else {
        snprintf(expected_display, display_cap, "%s", local);
    }
    return 1;
}

static int settings_apply_display_from_raw(const char *field_key, const char *raw, char *out, size_t out_cap)
{
    char *end;
    double numeric;
    if (!field_key || !raw || !raw[0] || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        return settings_apply_mode_to_display(raw, out, out_cap);
    }
    if (strcmp(field_key, "home_direction") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw) {
            return 0;
        }
        if (numeric > 0.0) {
            snprintf(out, out_cap, "+");
        } else if (numeric < 0.0) {
            snprintf(out, out_cap, "-");
        } else {
            snprintf(out, out_cap, "0");
        }
        return 1;
    }
    if (strcmp(field_key, "home_order") == 0 && strcmp(raw, "-1") == 0) {
        snprintf(out, out_cap, "禁用");
        return 1;
    }
    if (strcmp(field_key, "precision") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw || numeric <= 0.0) {
            return 0;
        }
        settings_apply_format_double(out, out_cap, 1.0 / numeric);
        return 1;
    }
    if (strcmp(field_key, "max_velocity") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(out, out_cap, numeric * 60.0);
        return 1;
    }
    if (settings_apply_axis_field_requires_integer_value(field_key)) {
        numeric = strtod(raw, &end);
        if (end == raw || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(out, out_cap, numeric);
        return 1;
    }
    snprintf(out, out_cap, "%s", raw);
    settings_apply_trim(out);
    return out[0] != '\0';
}

static int settings_apply_display_values_match(const char *field_key, const char *actual, const char *expected)
{
    if (!field_key || !actual || !expected) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0 || strcmp(field_key, "direction_mode") == 0 ||
        strcmp(field_key, "home_direction") == 0 || strcmp(expected, "禁用") == 0) {
        return strcmp(actual, expected) == 0;
    }
    return settings_apply_values_match(actual, expected);
}

static int ini_read_section_text(const char *path, const char *section_name, const char *key, char *out, size_t out_cap)
{
    FILE *fp;
    char raw[512];
    int in_section = 0;
    if (!path || !section_name || !key || !out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    while (fgets(raw, sizeof(raw), fp)) {
        char section[64];
        const char *value_start;
        if (ini_section_line(raw, section, sizeof(section))) {
            in_section = strcmp(section, section_name) == 0;
            continue;
        }
        if (in_section && ini_key_line(raw, key, &value_start)) {
            snprintf(out, out_cap, "%s", value_start);
            settings_apply_trim(out);
            fclose(fp);
            return out[0] != '\0';
        }
    }
    fclose(fp);
    return 0;
}

static void ini_write_key_text(FILE *out, const char *key, const char *value, int *last_had_newline)
{
    if (!out || !key || !value) {
        return;
    }
    if (last_had_newline && !*last_had_newline) {
        fputc('\n', out);
    }
    fprintf(out, "%s = %s\n", key, value);
    if (last_had_newline) {
        *last_had_newline = 1;
    }
}

static int ini_write_section_text(
    const char *path,
    const char *section_name,
    const char *key,
    const char *value,
    int numeric_required,
    char *readback,
    size_t readback_cap)
{
    FILE *in;
    FILE *out;
    char tmp_path[512];
    char raw[512];
    int in_section = 0;
    int saw_section = 0;
    int wrote_key = 0;
    int last_had_newline = 1;
    int n;
    if (!path || !section_name || !section_name[0] || !key || !key[0] ||
        (numeric_required ? !settings_apply_numeric_text(value) : !settings_apply_safe_ini_value(value))) {
        return 0;
    }
    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return 0;
    }
    in = fopen(path, "rb");
    if (!in) {
        return 0;
    }
    out = fopen(tmp_path, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while (fgets(raw, sizeof(raw), in)) {
        char section[64];
        size_t len = strlen(raw);
        if (ini_section_line(raw, section, sizeof(section))) {
            if (in_section && !wrote_key) {
                ini_write_key_text(out, key, value, &last_had_newline);
                wrote_key = 1;
            }
            in_section = strcmp(section, section_name) == 0;
            if (in_section) {
                saw_section = 1;
            }
        }
        if (in_section && ini_key_line(raw, key, 0)) {
            ini_write_key_text(out, key, value, &last_had_newline);
            wrote_key = 1;
            continue;
        }
        fputs(raw, out);
        last_had_newline = (len == 0U || raw[len - 1U] == '\n') ? 1 : 0;
    }
    if (saw_section && in_section && !wrote_key) {
        ini_write_key_text(out, key, value, &last_had_newline);
        wrote_key = 1;
    }
    fclose(in);
    if (fclose(out) != 0 || !saw_section || !wrote_key) {
        remove(tmp_path);
        return 0;
    }
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return 0;
    }
    if (!ini_read_section_text(path, section_name, key, readback, readback_cap)) {
        return 0;
    }
    return numeric_required ? settings_apply_values_match(readback, value) : strcmp(readback, value) == 0;
}

typedef struct V5IniTextUpdate {
    const char *section;
    const char *key;
    const char *value;
    int section_seen;
    int written;
} V5IniTextUpdate;

static void ini_write_missing_updates(
    FILE *out,
    const char *section,
    V5IniTextUpdate *updates,
    size_t update_count,
    int *last_had_newline)
{
    size_t i;
    if (!out || !section || !section[0] || !updates) {
        return;
    }
    for (i = 0U; i < update_count; ++i) {
        if (!updates[i].written && strcmp(updates[i].section, section) == 0) {
            ini_write_key_text(out, updates[i].key, updates[i].value, last_had_newline);
            updates[i].written = 1;
        }
    }
}

static int ini_write_text_updates(
    const char *path,
    V5IniTextUpdate *updates,
    size_t update_count)
{
    FILE *in;
    FILE *out;
    char tmp_path[512];
    char raw[512];
    char current_section[64] = "";
    int last_had_newline = 1;
    size_t i;
    int n;
    if (!path || !updates || update_count == 0U) {
        return 0;
    }
    for (i = 0U; i < update_count; ++i) {
        size_t j;
        if (!updates[i].section || !updates[i].section[0] || !updates[i].key || !updates[i].key[0] ||
            !settings_apply_safe_ini_value(updates[i].value)) {
            return 0;
        }
        for (j = 0U; j < i; ++j) {
            if (strcmp(updates[i].section, updates[j].section) == 0 &&
                strcmp(updates[i].key, updates[j].key) == 0) {
                return 0;
            }
        }
        updates[i].section_seen = 0;
        updates[i].written = 0;
    }
    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return 0;
    }
    in = fopen(path, "rb");
    if (!in) {
        return 0;
    }
    out = fopen(tmp_path, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while (fgets(raw, sizeof(raw), in)) {
        char section[64];
        size_t len = strlen(raw);
        int replaced = 0;
        if (ini_section_line(raw, section, sizeof(section))) {
            ini_write_missing_updates(out, current_section, updates, update_count, &last_had_newline);
            snprintf(current_section, sizeof(current_section), "%s", section);
            for (i = 0U; i < update_count; ++i) {
                if (strcmp(updates[i].section, current_section) == 0) {
                    updates[i].section_seen = 1;
                }
            }
        } else {
            for (i = 0U; i < update_count; ++i) {
                if (strcmp(updates[i].section, current_section) == 0 &&
                    ini_key_line(raw, updates[i].key, 0)) {
                    ini_write_key_text(out, updates[i].key, updates[i].value, &last_had_newline);
                    updates[i].written = 1;
                    replaced = 1;
                    break;
                }
            }
        }
        if (!replaced) {
            fputs(raw, out);
            last_had_newline = (len == 0U || raw[len - 1U] == '\n') ? 1 : 0;
        }
    }
    ini_write_missing_updates(out, current_section, updates, update_count, &last_had_newline);
    fclose(in);
    if (fclose(out) != 0) {
        remove(tmp_path);
        return 0;
    }
    for (i = 0U; i < update_count; ++i) {
        if (!updates[i].section_seen || !updates[i].written) {
            remove(tmp_path);
            return 0;
        }
    }
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return 0;
    }
    return 1;
}

static int write_text_file_atomic(const char *path, const char *text)
{
    char tmp_path[512];
    FILE *fp;
    size_t size;
    int n;
    int ok;
    if (!path || !text) {
        return 0;
    }
    n = snprintf(tmp_path, sizeof(tmp_path), "%s.rollback.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return 0;
    }
    fp = fopen(tmp_path, "wb");
    if (!fp) {
        return 0;
    }
    size = strlen(text);
    ok = size == 0U || fwrite(text, 1U, size, fp) == size;
    if (fclose(fp) != 0) {
        ok = 0;
    }
    if (!ok) {
        remove(tmp_path);
        return 0;
    }
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return 0;
    }
    return 1;
}

static int ini_updates_readback_match(
    const char *path,
    const V5IniTextUpdate *updates,
    size_t update_count)
{
    char readback[128];
    size_t i;
    for (i = 0U; i < update_count; ++i) {
        if (!ini_read_section_text(path, updates[i].section, updates[i].key, readback, sizeof(readback)) ||
            strcmp(readback, updates[i].value) != 0) {
            return 0;
        }
    }
    return 1;
}

static int settings_apply_joint_index_from_ini(const char *ini_path, const char *axis, unsigned int *joint_out)
{
    FILE *fp;
    char raw[512];
    char wanted;
    int in_traj = 0;
    if (!ini_path || !axis || strlen(axis) != 1U || !joint_out) {
        return 0;
    }
    wanted = (char)toupper((unsigned char)axis[0]);
    fp = fopen(ini_path, "rb");
    if (!fp) {
        return 0;
    }
    while (fgets(raw, sizeof(raw), fp)) {
        char probe[512];
        char section[64];
        char *eq;
        snprintf(probe, sizeof(probe), "%s", raw);
        settings_apply_trim(probe);
        if (!probe[0] || probe[0] == '#' || probe[0] == ';') {
            continue;
        }
        if (ini_section_line(probe, section, sizeof(section))) {
            in_traj = strcmp(section, "TRAJ") == 0;
            continue;
        }
        eq = strchr(probe, '=');
        if (!in_traj || !eq) {
            continue;
        }
        *eq = '\0';
        settings_apply_trim(probe);
        settings_apply_trim(eq + 1);
        if (strcmp(probe, "COORDINATES") == 0) {
            unsigned int joint = 0U;
            const char *p = eq + 1;
            while (*p) {
                if (isalpha((unsigned char)*p)) {
                    if ((char)toupper((unsigned char)*p) == wanted) {
                        *joint_out = joint;
                        fclose(fp);
                        return 1;
                    }
                    ++joint;
                }
                ++p;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int settings_apply_runtime_target(
    const char *ini_path,
    const char *axis,
    const char *field_key,
    unsigned int fallback_axis_index,
    char *primary_section,
    size_t primary_cap,
    char *mirror_section,
    size_t mirror_cap,
    const char **ini_key,
    unsigned int *joint_index_out)
{
    char axis_section[32];
    char joint_section[32];
    unsigned int joint_index = fallback_axis_index;
    int has_joint;
    const char *key = 0;
    int primary_is_joint = 0;
    int mirror_joint = 0;
    if (!ini_path || !axis || !field_key || !primary_section || primary_cap == 0U ||
        !mirror_section || mirror_cap == 0U || !ini_key) {
        return 0;
    }
    primary_section[0] = '\0';
    mirror_section[0] = '\0';
    *ini_key = 0;
    snprintf(axis_section, sizeof(axis_section), "AXIS_%s", axis);
    has_joint = settings_apply_joint_index_from_ini(ini_path, axis, &joint_index);
    if (has_joint) {
        snprintf(joint_section, sizeof(joint_section), "JOINT_%u", joint_index);
    } else {
        joint_section[0] = '\0';
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        key = "TYPE";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "direction_mode") == 0) {
        key = "DIRECTION_MODE";
    } else if (strcmp(field_key, "precision") == 0) {
        key = "SCALE";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "pitch") == 0) {
        key = "PITCH";
    } else if (strcmp(field_key, "motor_rev") == 0) {
        key = "MOTOR_REV";
    } else if (strcmp(field_key, "load_rev") == 0) {
        key = "LOAD_REV";
    } else if (strcmp(field_key, "home_order") == 0) {
        key = "HOME_SEQUENCE";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "home_direction") == 0) {
        key = "HOME_SEARCH_VEL";
        primary_is_joint = has_joint;
    } else if (strcmp(field_key, "soft_minus") == 0) {
        key = "MIN_LIMIT";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "soft_plus") == 0) {
        key = "MAX_LIMIT";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "max_velocity") == 0) {
        key = "MAX_VELOCITY";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "max_acceleration") == 0) {
        key = "MAX_ACCELERATION";
        mirror_joint = has_joint;
    } else if (strcmp(field_key, "backlash") == 0) {
        key = "BACKLASH";
        mirror_joint = has_joint;
    } else {
        return 0;
    }
    snprintf(primary_section, primary_cap, "%s", primary_is_joint ? joint_section : axis_section);
    if (mirror_joint && has_joint) {
        snprintf(mirror_section, mirror_cap, "%s", joint_section);
    }
    *ini_key = key;
    if (joint_index_out) {
        *joint_index_out = joint_index;
    }
    return primary_section[0] != '\0' && *ini_key != 0;
}

static int settings_apply_commit_runtime_ini(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    char ini_path[512];
    char primary_section[32];
    char mirror_section[32];
    const char *ini_key;
    char ini_value[64];
    char expected_display[64];
    char raw[64];
    char display[64];
    unsigned int joint_index = request->axis_index;
    if (!build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    if (!settings_apply_runtime_target(ini_path, request->axis, request->field_key, request->axis_index,
                                       primary_section, sizeof(primary_section),
                                       mirror_section, sizeof(mirror_section),
                                       &ini_key, &joint_index)) {
        return 0;
    }
    if (!settings_apply_ini_value_for_field(request->field_key, request->value_text,
                                            ini_value, sizeof(ini_value),
                                            expected_display, sizeof(expected_display))) {
        return 0;
    }
    if (!ini_write_section_text(ini_path, primary_section, ini_key, ini_value, 0, raw, sizeof(raw))) {
        return 0;
    }
    if (mirror_section[0] && strcmp(mirror_section, primary_section) != 0) {
        char mirror_readback[64];
        if (!ini_write_section_text(ini_path, mirror_section, ini_key, ini_value, 0,
                                    mirror_readback, sizeof(mirror_readback))) {
            return 0;
        }
    }
    if (!ini_read_section_text(ini_path, primary_section, ini_key, raw, sizeof(raw))) {
        return 0;
    }
    if (!settings_apply_display_from_raw(request->field_key, raw, display, sizeof(display))) {
        return 0;
    }
    if (!settings_apply_display_values_match(request->field_key, display, expected_display)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", display);
    }
    if (result && result->apply.raw_limits_recompute_required) {
        if (!v5_settings_apply_scale_chain_commit(request->project_root, 0, request->axis, joint_index,
                                                  request->field_name, &result->scale_chain)) {
            return 0;
        }
    }
    return 1;
}

static const char *settings_apply_g53_rtcp_key(const char *field_name)
{
    if (!field_name) return 0;
    if (strcmp(field_name, "g53_A_center_y") == 0) return "G53_A_Y";
    if (strcmp(field_name, "g53_A_center_z") == 0) return "G53_A_Z";
    if (strcmp(field_name, "g53_B_center_x") == 0) return "G53_B_X";
    if (strcmp(field_name, "g53_B_center_z") == 0) return "G53_B_Z";
    if (strcmp(field_name, "g53_C_center_x") == 0) return "G53_C_X";
    if (strcmp(field_name, "g53_C_center_y") == 0) return "G53_C_Y";
    return 0;
}

static int settings_apply_motion_model_values(
    const char *value_text,
    char *canonical,
    size_t canonical_cap,
    char *display,
    size_t display_cap,
    char *kins_module,
    size_t kins_module_cap,
    char *kins_coordinates,
    size_t kins_coordinates_cap,
    char *traj_coordinates,
    size_t traj_coordinates_cap,
    unsigned int *wrapped_rotary_mask)
{
    const V5MotionModelDescriptor *model;
    if (!value_text || !value_text[0] || !canonical || canonical_cap == 0U ||
        !display || display_cap == 0U ||
        !kins_module || kins_module_cap == 0U ||
        !kins_coordinates || kins_coordinates_cap == 0U ||
        !traj_coordinates || traj_coordinates_cap == 0U ||
        !wrapped_rotary_mask) {
        return 0;
    }
    model = v5_motion_model_find(value_text);
    if (!model) {
        return 0;
    }
    snprintf(canonical, canonical_cap, "%s", model->canonical);
    snprintf(display, display_cap, "%s", model->display);
    snprintf(kins_module, kins_module_cap, "%s", model->kins_module);
    snprintf(kins_coordinates, kins_coordinates_cap, "%s", model->kins_coordinates);
    snprintf(traj_coordinates, traj_coordinates_cap, "%s", model->traj_coordinates);
    *wrapped_rotary_mask = model->wrapped_rotary_mask;
    return 1;
}

#define V5_MODEL_JOINT_KEY_COUNT 10U
#define V5_MODEL_INI_UPDATE_MAX 48U

static const char *const k_model_joint_keys[V5_MODEL_JOINT_KEY_COUNT] = {
    "TYPE",
    "HOME",
    "MIN_LIMIT",
    "MAX_LIMIT",
    "MAX_VELOCITY",
    "MAX_ACCELERATION",
    "SCALE",
    "HOME_SEARCH_VEL",
    "HOME_SEQUENCE",
    "BACKLASH",
};

static int ini_update_add(
    V5IniTextUpdate *updates,
    size_t *count,
    size_t cap,
    const char *section,
    const char *key,
    const char *value)
{
    if (!updates || !count || *count >= cap || !section || !key || !value) {
        return 0;
    }
    updates[*count].section = section;
    updates[*count].key = key;
    updates[*count].value = value;
    updates[*count].section_seen = 0;
    updates[*count].written = 0;
    ++*count;
    return 1;
}

static const V5MotionModelDescriptor *settings_apply_alternate_model(
    const V5MotionModelDescriptor *target,
    const V5MotionModelDescriptor *current)
{
    size_t i;
    if (!target) {
        return 0;
    }
    if (current && current->first_status_slot == target->first_status_slot &&
        current->first_rotary_axis != target->first_rotary_axis) {
        return current;
    }
    for (i = 0U; i < v5_motion_model_registry_count(); ++i) {
        const V5MotionModelDescriptor *candidate = v5_motion_model_registry_at(i);
        if (candidate && candidate->first_status_slot == target->first_status_slot &&
            candidate->first_rotary_axis != target->first_rotary_axis) {
            return candidate;
        }
    }
    return 0;
}

static int settings_apply_commit_motion_model(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    const V5MotionModelDescriptor *target_model;
    const V5MotionModelDescriptor *current_model = 0;
    const V5MotionModelDescriptor *alternate_model;
    V5IniTextUpdate updates[V5_MODEL_INI_UPDATE_MAX];
    size_t update_count = 0U;
    char ini_path[512];
    char current_model_text[64];
    char canonical[32];
    char display[32];
    char kins_module[32];
    char kins_coordinates[16];
    char kins_prefix[32];
    char kins_tool_offset_pin[64];
    char kins_x_rot_point_pin[64];
    char kins_y_rot_point_pin[64];
    char kins_z_rot_point_pin[64];
    char kins_x_offset_pin[64];
    char kins_y_offset_pin[64];
    char kins_z_offset_pin[64];
    char traj_coordinates[32];
    char kinematics[96];
    char wrapped_mask[16];
    char target_axis[2];
    char alternate_axis[2] = "";
    char snapshot_axis[2];
    char target_axis_section[32];
    char snapshot_axis_section[32];
    char joint_section[32];
    char target_slave[64] = "";
    char alternate_slave[64] = "";
    char final_target_slave[64] = "";
    char final_alternate_slave[64] = "";
    char joint_snapshot[V5_MODEL_JOINT_KEY_COUNT][64];
    char target_joint[V5_MODEL_JOINT_KEY_COUNT][64];
    char target_slave_readback[64];
    char alternate_slave_readback[64];
    char *original_ini;
    unsigned int wrapped_rotary_mask = 0U;
    unsigned int i;
    int pair_needed = 0;
    int pair_written = 0;
    int target_is_nat = 0;
    int alternate_is_nat = 1;
    int ok = 0;
    if (!request || !request->value_text ||
        !settings_apply_motion_model_values(
            request->value_text,
            canonical,
            sizeof(canonical),
            display,
            sizeof(display),
            kins_module,
            sizeof(kins_module),
            kins_coordinates,
            sizeof(kins_coordinates),
            traj_coordinates,
            sizeof(traj_coordinates),
            &wrapped_rotary_mask) ||
        !build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    target_model = v5_motion_model_find(canonical);
    if (!target_model) {
        return 0;
    }
    if (ini_read_section_text(ini_path, "RTCP", "MODEL", current_model_text, sizeof(current_model_text))) {
        current_model = v5_motion_model_find(current_model_text);
    }
    alternate_model = settings_apply_alternate_model(target_model, current_model);
    target_axis[0] = target_model->first_rotary_axis;
    target_axis[1] = '\0';
    snapshot_axis[0] = target_axis[0];
    snapshot_axis[1] = '\0';
    if (alternate_model) {
        alternate_axis[0] = alternate_model->first_rotary_axis;
        alternate_axis[1] = '\0';
        if (!v5_settings_parameter_store_read_axis(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                target_axis, "slave", target_slave, sizeof(target_slave)) ||
            !v5_settings_parameter_store_read_axis(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                alternate_axis, "slave", alternate_slave, sizeof(alternate_slave))) {
            return 0;
        }
        snprintf(final_target_slave, sizeof(final_target_slave), "%s", target_slave);
        snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", alternate_slave);
        target_is_nat = strcmp(target_slave, "NAT") == 0;
        alternate_is_nat = strcmp(alternate_slave, "NAT") == 0;
        if (target_is_nat && !alternate_is_nat) {
            snprintf(final_target_slave, sizeof(final_target_slave), "%s", alternate_slave);
            snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", "NAT");
            snapshot_axis[0] = alternate_axis[0];
            pair_needed = 1;
        } else if (!target_is_nat && alternate_is_nat) {
            snapshot_axis[0] = target_axis[0];
        } else if (!target_is_nat && !alternate_is_nat) {
            if (strcmp(target_slave, alternate_slave) != 0) {
                return 0;
            }
            snprintf(final_alternate_slave, sizeof(final_alternate_slave), "%s", "NAT");
            snapshot_axis[0] = current_model &&
                current_model->first_rotary_axis == alternate_axis[0] ? alternate_axis[0] : target_axis[0];
            pair_needed = 1;
        } else if (current_model && current_model->first_status_slot == target_model->first_status_slot) {
            snapshot_axis[0] = current_model->first_rotary_axis;
        }
    }
    snprintf(target_axis_section, sizeof(target_axis_section), "AXIS_%c", target_axis[0]);
    snprintf(snapshot_axis_section, sizeof(snapshot_axis_section), "AXIS_%c", snapshot_axis[0]);
    snprintf(joint_section, sizeof(joint_section), "JOINT_%u", target_model->first_status_slot);
    for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
        if (!ini_read_section_text(
                ini_path, joint_section, k_model_joint_keys[i],
                joint_snapshot[i], sizeof(joint_snapshot[i]))) {
            return 0;
        }
        if (snapshot_axis[0] == target_axis[0]) {
            snprintf(target_joint[i], sizeof(target_joint[i]), "%s", joint_snapshot[i]);
        } else if (!ini_read_section_text(
                       ini_path, target_axis_section, k_model_joint_keys[i],
                       target_joint[i], sizeof(target_joint[i]))) {
            return 0;
        }
    }
    snprintf(kins_prefix, sizeof(kins_prefix), "%s", kins_module);
    snprintf(kins_tool_offset_pin, sizeof(kins_tool_offset_pin), "%s.tool-offset", kins_prefix);
    snprintf(kins_x_rot_point_pin, sizeof(kins_x_rot_point_pin), "%s.x-rot-point", kins_prefix);
    snprintf(kins_y_rot_point_pin, sizeof(kins_y_rot_point_pin), "%s.y-rot-point", kins_prefix);
    snprintf(kins_z_rot_point_pin, sizeof(kins_z_rot_point_pin), "%s.z-rot-point", kins_prefix);
    snprintf(kins_x_offset_pin, sizeof(kins_x_offset_pin), "%s.x-offset", kins_prefix);
    snprintf(kins_y_offset_pin, sizeof(kins_y_offset_pin), "%s.y-offset", kins_prefix);
    snprintf(kins_z_offset_pin, sizeof(kins_z_offset_pin), "%s.z-offset", kins_prefix);
    snprintf(kinematics, sizeof(kinematics), "%s coordinates=%s sparm=identityfirst", kins_module, kins_coordinates);
    snprintf(wrapped_mask, sizeof(wrapped_mask), "%u", wrapped_rotary_mask);
    if (!ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "MODEL", canonical) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "MOTION_MODEL", display) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_MODULE", kins_module) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_COORDINATES", kins_coordinates) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_PREFIX", kins_prefix) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_TOOL_OFFSET_PIN", kins_tool_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_X_ROT_POINT_PIN", kins_x_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Y_ROT_POINT_PIN", kins_y_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Z_ROT_POINT_PIN", kins_z_rot_point_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_X_OFFSET_PIN", kins_x_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Y_OFFSET_PIN", kins_y_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "KINS_Z_OFFSET_PIN", kins_z_offset_pin) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "RTCP", "WRAPPED_ROTARY_MASK", wrapped_mask) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "KINS", "KINEMATICS", kinematics) ||
        !ini_update_add(updates, &update_count, V5_MODEL_INI_UPDATE_MAX, "TRAJ", "COORDINATES", traj_coordinates)) {
        return 0;
    }
    for (i = 0U; i < V5_MODEL_JOINT_KEY_COUNT; ++i) {
        if (!ini_update_add(
                updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                snapshot_axis_section, k_model_joint_keys[i], joint_snapshot[i]) ||
            !ini_update_add(
                updates, &update_count, V5_MODEL_INI_UPDATE_MAX,
                joint_section, k_model_joint_keys[i], target_joint[i])) {
            return 0;
        }
    }
    original_ini = read_text_file_limited(ini_path);
    if (!original_ini || !ini_write_text_updates(ini_path, updates, update_count)) {
        free(original_ini);
        return 0;
    }
    if (pair_needed) {
        pair_written = v5_settings_parameter_store_write_axis_pair(
            request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
            target_axis, alternate_axis, "slave", final_target_slave, final_alternate_slave);
        if (!pair_written) {
            (void)write_text_file_atomic(ini_path, original_ini);
            free(original_ini);
            return 0;
        }
    }
    ok = ini_updates_readback_match(ini_path, updates, update_count);
    if (ok && alternate_model) {
        ok = v5_settings_parameter_store_read_axis(
                 request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                 target_axis, "slave", target_slave_readback, sizeof(target_slave_readback)) &&
             v5_settings_parameter_store_read_axis(
                 request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                 alternate_axis, "slave", alternate_slave_readback, sizeof(alternate_slave_readback)) &&
             strcmp(target_slave_readback, final_target_slave) == 0 &&
             strcmp(alternate_slave_readback, final_alternate_slave) == 0;
    }
    if (!ok) {
        (void)write_text_file_atomic(ini_path, original_ini);
        if (pair_written) {
            (void)v5_settings_parameter_store_write_axis_pair(
                request->project_root, V5_SETTINGS_PARAMETER_DISK_SELF,
                target_axis, alternate_axis, "slave", target_slave, alternate_slave);
        }
        free(original_ini);
        return 0;
    }
    free(original_ini);
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", display);
    }
    return 1;
}

static int settings_apply_commit_g53_geometry(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    char ini_path[512];
    char raw[64];
    const char *ini_key;
    if (!request || !request->field_name || !request->value_text) {
        return 0;
    }
    if (strcmp(request->field_name, "motion_model") == 0) {
        return settings_apply_commit_motion_model(request, result);
    }
    if (!settings_apply_numeric_text(request->value_text) ||
        !build_runtime_ini_path(ini_path, sizeof(ini_path), request->project_root)) {
        return 0;
    }
    ini_key = settings_apply_g53_rtcp_key(request->field_name);
    if (!ini_key) {
        return 0;
    }
    if (!ini_write_section_text(ini_path, "RTCP", ini_key, request->value_text, 0, raw, sizeof(raw))) {
        return 0;
    }
    if (!ini_read_section_text(ini_path, "RTCP", ini_key, raw, sizeof(raw))) {
        return 0;
    }
    if (!settings_apply_values_match(raw, request->value_text)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", raw);
    }
    return 1;
}

static V5SettingsParameterDiskTable settings_apply_disk_table_for_owner(
    const char *field_key,
    V5ParameterOwnerKind owner)
{
    if (field_key && strcmp(field_key, "slave") == 0) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    if (field_key && strcmp(field_key, "encoder_bits") == 0) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (owner == V5_PARAMETER_OWNER_DRIVE_ONLY) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (owner == V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    return V5_SETTINGS_PARAMETER_DISK_NONE;
}

static int settings_apply_commit_parameter_table(
    const V5SettingsApplyAxisCommitRequest *request,
    V5ParameterOwnerKind owner,
    V5SettingsApplyAxisCommitResult *result)
{
    V5SettingsParameterDiskTable table = settings_apply_disk_table_for_owner(request->field_key, owner);
    char readback[64];
    if (table == V5_SETTINGS_PARAMETER_DISK_NONE) {
        return 0;
    }
    if (!v5_settings_parameter_store_write_axis(request->project_root, table,
                                                request->axis, request->field_key, request->value_text)) {
        return 0;
    }
    if (!v5_settings_parameter_store_read_axis(request->project_root, table,
                                               request->axis, request->field_key, readback, sizeof(readback))) {
        return 0;
    }
    if (!settings_apply_values_match(readback, request->value_text)) {
        return 0;
    }
    if (result) {
        snprintf(result->readback_value, sizeof(result->readback_value), "%s", readback);
    }
    return 1;
}

int v5_settings_apply_commit_axis_value(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result)
{
    V5SettingsApplyRequest prepare_request;
    V5SettingsApplyResult apply_result;
    V5ParameterOwnerRecord owner_record;
    int ok = 0;
    if (result) {
        memset(result, 0, sizeof(*result));
        scale_chain_result_code(&result->scale_chain, "SCALE_CHAIN_NOT_ATTEMPTED");
    }
    if (!request || !request->project_root || !request->axis || !request->axis[0] ||
        !request->field_key || !request->field_key[0] || !request->field_name || !request->field_name[0] ||
        !request->value_text || !request->value_text[0]) {
        return 0;
    }
    memset(&prepare_request, 0, sizeof(prepare_request));
    prepare_request.field_name = request->field_name;
    prepare_request.value_text = request->value_text;
    prepare_request.owner_generation = request->owner_generation;
    prepare_request.readback_token = request->readback_token;
    if (!v5_settings_apply_prepare(&prepare_request, &apply_result)) {
        return 0;
    }
    if (!v5_parameter_owner_lookup(request->field_name, &owner_record) || !owner_record.field) {
        return 0;
    }
    if (result) {
        result->apply = apply_result;
    }
    switch (owner_record.field->owner) {
    case V5_PARAMETER_OWNER_RUNTIME_INI:
        ok = settings_apply_commit_runtime_ini(request, result);
        break;
    case V5_PARAMETER_OWNER_KINEMATICS_NATIVE:
        ok = settings_apply_commit_g53_geometry(request, result);
        break;
    case V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE:
    case V5_PARAMETER_OWNER_DRIVE_ONLY:
        ok = settings_apply_commit_parameter_table(request, owner_record.field->owner, result);
        break;
    default:
        ok = 0;
        break;
    }
    if (!ok || !result) {
        return ok;
    }
    result->owner_written = 1;
    result->source_readback_confirmed = result->readback_value[0] ? 1 : 0;
    result->restart_pending = apply_result.restart_required ? 1 : 0;
    return result->source_readback_confirmed;
}
