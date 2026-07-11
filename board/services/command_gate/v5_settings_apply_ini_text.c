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

int v5_settings_apply_ini_read_section_text(const char *path, const char *section_name, const char *key, char *out, size_t out_cap)
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
        if (v5_settings_apply_ini_section_line(raw, section, sizeof(section))) {
            in_section = strcmp(section, section_name) == 0;
            continue;
        }
        if (in_section && v5_settings_apply_ini_key_line(raw, key, &value_start)) {
            snprintf(out, out_cap, "%s", value_start);
            v5_settings_apply_trim(out);
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

int v5_settings_apply_ini_write_section_text(
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
        (numeric_required ? !v5_settings_apply_numeric_text(value) : !v5_settings_apply_safe_ini_value(value))) {
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
        if (v5_settings_apply_ini_section_line(raw, section, sizeof(section))) {
            if (in_section && !wrote_key) {
                ini_write_key_text(out, key, value, &last_had_newline);
                wrote_key = 1;
            }
            in_section = strcmp(section, section_name) == 0;
            if (in_section) {
                saw_section = 1;
            }
        }
        if (in_section && v5_settings_apply_ini_key_line(raw, key, 0)) {
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
    if (!v5_settings_apply_ini_read_section_text(path, section_name, key, readback, readback_cap)) {
        return 0;
    }
    return numeric_required ? v5_settings_apply_values_match(readback, value) : strcmp(readback, value) == 0;
}


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

int v5_settings_apply_ini_write_text_updates(
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
            !v5_settings_apply_safe_ini_value(updates[i].value)) {
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
        if (v5_settings_apply_ini_section_line(raw, section, sizeof(section))) {
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
                    v5_settings_apply_ini_key_line(raw, updates[i].key, 0)) {
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

int v5_settings_apply_write_text_file_atomic(const char *path, const char *text)
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

int v5_settings_apply_ini_updates_readback_match(
    const char *path,
    const V5IniTextUpdate *updates,
    size_t update_count)
{
    char readback[128];
    size_t i;
    for (i = 0U; i < update_count; ++i) {
        if (!v5_settings_apply_ini_read_section_text(path, updates[i].section, updates[i].key, readback, sizeof(readback)) ||
            strcmp(readback, updates[i].value) != 0) {
            return 0;
        }
    }
    return 1;
}
