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

static int settings_apply_build_mode_ini_path(
    char *out,
    size_t out_cap,
    const char *project_root,
    const char *relative_path)
{
    int n;
    const char *root = (project_root && project_root[0]) ? project_root : ".";
    const char *separator;
    size_t root_len;
    if (!out || out_cap == 0U || !relative_path || !relative_path[0]) {
        return 0;
    }
    root_len = strlen(root);
    separator = root_len > 0U && (root[root_len - 1U] == '/' || root[root_len - 1U] == '\\')
        ? "" : "/";
    n = snprintf(out, out_cap, "%s%s%s", root, separator, relative_path);
    return n > 0 && (size_t)n < out_cap;
}

int v5_settings_apply_build_runtime_ini_path(
    char *out,
    size_t out_cap,
    const char *project_root,
    const char *runtime_ini_path)
{
    char bus_path[512];
    char pulse_path[512];
    const char *selected;
    int n;
    if (!out || out_cap == 0U ||
        !settings_apply_build_mode_ini_path(
            bus_path, sizeof(bus_path), project_root, "linuxcnc/ini/v5_bus.ini") ||
        !settings_apply_build_mode_ini_path(
            pulse_path, sizeof(pulse_path), project_root, "linuxcnc/ini/v5_pulse.ini")) {
        return 0;
    }
    if (!runtime_ini_path || !runtime_ini_path[0]) {
        selected = bus_path;
    } else if (strcmp(runtime_ini_path, bus_path) == 0) {
        selected = bus_path;
    } else if (strcmp(runtime_ini_path, pulse_path) == 0) {
        selected = pulse_path;
    } else {
        return 0;
    }
    n = snprintf(out, out_cap, "%s", selected);
    return n > 0 && (size_t)n < out_cap;
}

int v5_settings_apply_ini_section_line(const char *raw, char *section, size_t section_cap)
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

int v5_settings_apply_ini_key_line(const char *raw, const char *key, const char **value_start)
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

int v5_settings_apply_ini_read_section_number(
    const char *path,
    const char *section_name,
    const char *key,
    double *out)
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
        if (v5_settings_apply_ini_section_line(raw, section, sizeof(section))) {
            in_section = strcmp(section, section_name) == 0;
            continue;
        }
        if (in_section && v5_settings_apply_ini_key_line(raw, key, &value_start)) {
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

int v5_settings_apply_ini_read_preferred_number(const char *path, const char *primary_section, const char *fallback_section, const char *key, double *out)
{
    if (primary_section && primary_section[0] &&
        v5_settings_apply_ini_read_section_number(path, primary_section, key, out)) {
        return 1;
    }
    return fallback_section && fallback_section[0] &&
        v5_settings_apply_ini_read_section_number(path, fallback_section, key, out);
}

int v5_settings_apply_ini_write_scale_and_limits(
    const char *path,
    const char *axis_section,
    const char *joint_section,
    int joint_active,
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

    if (!path || !axis_section || !axis_section[0] ||
        (joint_active && (!joint_section || !joint_section[0])) ||
        !isfinite(raw_min) || !isfinite(raw_max) ||
        (raw_min != 0.0 && raw_max != 0.0 && raw_min >= raw_max)) {
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
        if (v5_settings_apply_ini_section_line(raw, next_section, sizeof(next_section))) {
            snprintf(section, sizeof(section), "%s", next_section);
            fputs(raw, out);
            continue;
        }
        if (strcmp(section, axis_section) == 0 ||
            (joint_active && strcmp(section, joint_section) == 0)) {
            if (v5_settings_apply_ini_key_line(raw, "MIN_LIMIT", 0)) {
                fprintf(out, "MIN_LIMIT = %.12g\n", raw_min);
                if (strcmp(section, axis_section) == 0) touched_axis_min = 1;
                if (strcmp(section, joint_section) == 0) touched_joint_min = 1;
                continue;
            }
            if (v5_settings_apply_ini_key_line(raw, "MAX_LIMIT", 0)) {
                fprintf(out, "MAX_LIMIT = %.12g\n", raw_max);
                if (strcmp(section, axis_section) == 0) touched_axis_max = 1;
                if (strcmp(section, joint_section) == 0) touched_joint_max = 1;
                continue;
            }
            if (write_scale &&
                ((!joint_active && strcmp(section, axis_section) == 0) ||
                 (joint_active && strcmp(section, joint_section) == 0)) &&
                v5_settings_apply_ini_key_line(raw, "SCALE", 0)) {
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
    if (!(touched_axis_min && touched_axis_max) ||
        (joint_active && !(touched_joint_min && touched_joint_max))) {
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
