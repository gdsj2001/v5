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
#include "v5_settings_apply_internal.h"

int v5_settings_apply_file_exists(const char *path)
{
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char *v5_settings_apply_read_text_file_limited(const char *path)
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

int v5_settings_apply_json_object_for_key(
    const char *start,
    const char *end,
    const char *key,
    const char **object_start,
    const char **object_end)
{
    const char *value;
    const char *close;
    if (!object_start || !object_end) {
        return 0;
    }
    value = json_value_for_key(start, end, key);
    if (!value || value >= end || *value != '{') {
        return 0;
    }
    close = json_matching_delim(value, end, '{', '}');
    if (!close) {
        return 0;
    }
    *object_start = value;
    *object_end = close + 1;
    return 1;
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

int v5_settings_apply_json_number_value(const char *start, const char *end, const char *key, double *out)
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

int v5_settings_apply_runtime_axis_object(const char *json, const char *axis, const char **axis_start, const char **axis_end)
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
