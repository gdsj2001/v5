#include "v5_settings_parameter_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define V5_STORE_MAX_ROWS 192U
#define V5_STORE_TEXT_CAP 512U
#define V5_STORE_PATH_CAP 512U

typedef struct V5StoreRow {
    char axis[V5_STORE_TEXT_CAP];
    char field[V5_STORE_TEXT_CAP];
    char value[V5_STORE_TEXT_CAP];
} V5StoreRow;

static const char *table_file_name(V5SettingsParameterDiskTable table)
{
    switch (table) {
    case V5_SETTINGS_PARAMETER_DISK_SELF:
        return "self_parameter_table.tsv";
    case V5_SETTINGS_PARAMETER_DISK_DRIVE:
        return "drive_parameter_table.tsv";
    default:
        return 0;
    }
}

static int ensure_dir(const char *path)
{
    if (!path || !path[0]) return 0;
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 1;
    return 0;
}

static int build_paths(const char *project_root, V5SettingsParameterDiskTable table, char *dir, size_t dir_cap, char *path, size_t path_cap)
{
    const char *name = table_file_name(table);
    const char *root = (project_root && project_root[0]) ? project_root : ".";
    if (!name || !dir || !path || dir_cap == 0U || path_cap == 0U) return 0;
    if (snprintf(dir, dir_cap, "%s/config/settings", root) >= (int)dir_cap) return 0;
    if (snprintf(path, path_cap, "%s/%s", dir, name) >= (int)path_cap) return 0;
    return 1;
}

static void trim_newline(char *text)
{
    size_t len;
    if (!text) return;
    len = strlen(text);
    while (len > 0U && (text[len - 1U] == '\n' || text[len - 1U] == '\r')) {
        text[--len] = '\0';
    }
}

static int load_rows(const char *path, V5StoreRow *rows, size_t *count)
{
    FILE *fp;
    char line[1024];
    size_t n = 0U;
    if (!rows || !count) return 0;
    *count = 0U;
    fp = fopen(path, "rb");
    if (!fp) return 1;
    while (fgets(line, sizeof(line), fp)) {
        char *axis;
        char *field;
        char *value;
        trim_newline(line);
        axis = line;
        field = strchr(axis, '\t');
        if (!field) continue;
        *field++ = '\0';
        value = strchr(field, '\t');
        if (!value) continue;
        *value++ = '\0';
        if (!axis[0] || !field[0] || !value[0]) continue;
        {
            size_t i;
            int updated = 0;
            for (i = 0U; i < n; ++i) {
                if (strcmp(rows[i].axis, axis) == 0 && strcmp(rows[i].field, field) == 0) {
                    snprintf(rows[i].value, sizeof(rows[i].value), "%s", value);
                    updated = 1;
                    break;
                }
            }
            if (updated) continue;
        }
        if (n >= V5_STORE_MAX_ROWS) continue;
        snprintf(rows[n].axis, sizeof(rows[n].axis), "%s", axis);
        snprintf(rows[n].field, sizeof(rows[n].field), "%s", field);
        snprintf(rows[n].value, sizeof(rows[n].value), "%s", value);
        ++n;
    }
    fclose(fp);
    *count = n;
    return 1;
}

static int save_rows(const char *path, const V5StoreRow *rows, size_t count)
{
    char tmp[V5_STORE_PATH_CAP];
    FILE *fp;
    size_t i;
    if (!path || !rows || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return 0;
    }
    fp = fopen(tmp, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "# schema=v5.settings.parameter_table.tsv.v1\n");
    for (i = 0U; i < count; ++i) {
        if (rows[i].axis[0] && rows[i].field[0] && rows[i].value[0]) {
            fprintf(fp, "%s\t%s\t%s\n", rows[i].axis, rows[i].field, rows[i].value);
        }
    }
    if (fclose(fp) != 0) {
        (void)unlink(tmp);
        return 0;
    }
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return 0;
    }
    return 1;
}

int v5_settings_parameter_store_read_axis(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    char *out,
    size_t out_cap)
{
    char dir[V5_STORE_PATH_CAP];
    char path[V5_STORE_PATH_CAP];
    V5StoreRow rows[V5_STORE_MAX_ROWS];
    size_t count = 0U;
    size_t i;
    int found = 0;
    if (!axis || !axis[0] || !field_key || !field_key[0] || !out || out_cap == 0U) return 0;
    out[0] = '\0';
    if (!build_paths(project_root, table, dir, sizeof(dir), path, sizeof(path))) return 0;
    (void)dir;
    if (!load_rows(path, rows, &count)) return 0;
    for (i = 0U; i < count; ++i) {
        if (strcmp(rows[i].axis, axis) == 0 && strcmp(rows[i].field, field_key) == 0) {
            snprintf(out, out_cap, "%s", rows[i].value);
            found = 1;
            break;
        }
    }
    return found && out[0];
}

int v5_settings_parameter_store_write_axis(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    const char *value)
{
    char dir[V5_STORE_PATH_CAP];
    char parent[V5_STORE_PATH_CAP];
    char path[V5_STORE_PATH_CAP];
    V5StoreRow rows[V5_STORE_MAX_ROWS];
    size_t count = 0U;
    size_t i;
    int updated = 0;
    if (!axis || !axis[0] || !field_key || !field_key[0] || !value || !value[0]) return 0;
    if (!build_paths(project_root, table, dir, sizeof(dir), path, sizeof(path))) return 0;
    snprintf(parent, sizeof(parent), "%s/config", (project_root && project_root[0]) ? project_root : ".");
    if (!ensure_dir(parent) || !ensure_dir(dir)) return 0;
    if (!load_rows(path, rows, &count)) return 0;
    for (i = 0U; i < count; ++i) {
        if (strcmp(rows[i].axis, axis) == 0 && strcmp(rows[i].field, field_key) == 0) {
            snprintf(rows[i].value, sizeof(rows[i].value), "%s", value);
            updated = 1;
            break;
        }
    }
    if (!updated) return 0;
    return save_rows(path, rows, count);
}

int v5_settings_parameter_store_write_axis_pair(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *first_axis,
    const char *second_axis,
    const char *field_key,
    const char *first_value,
    const char *second_value)
{
    V5SettingsParameterAxisValue values[2];
    values[0].axis = first_axis;
    values[0].value = first_value;
    values[1].axis = second_axis;
    values[1].value = second_value;
    return v5_settings_parameter_store_write_axis_values(
        project_root, table, field_key, values, 2U);
}

int v5_settings_parameter_store_write_axis_values(
    const char *project_root,
    V5SettingsParameterDiskTable table,
    const char *field_key,
    const V5SettingsParameterAxisValue *values,
    size_t value_count)
{
    char dir[V5_STORE_PATH_CAP];
    char parent[V5_STORE_PATH_CAP];
    char path[V5_STORE_PATH_CAP];
    V5StoreRow rows[V5_STORE_MAX_ROWS];
    unsigned char updated[V5_STORE_MAX_ROWS];
    size_t count = 0U;
    size_t i;
    size_t value_i;
    if (!field_key || !field_key[0] || !values || value_count == 0U ||
        value_count > V5_STORE_MAX_ROWS) {
        return 0;
    }
    memset(updated, 0, sizeof(updated));
    for (value_i = 0U; value_i < value_count; ++value_i) {
        size_t other_i;
        if (!values[value_i].axis || !values[value_i].axis[0] ||
            !values[value_i].value || !values[value_i].value[0]) {
            return 0;
        }
        for (other_i = 0U; other_i < value_i; ++other_i) {
            if (strcmp(values[value_i].axis, values[other_i].axis) == 0) {
                return 0;
            }
        }
    }
    if (!build_paths(project_root, table, dir, sizeof(dir), path, sizeof(path))) {
        return 0;
    }
    snprintf(parent, sizeof(parent), "%s/config", (project_root && project_root[0]) ? project_root : ".");
    if (!ensure_dir(parent) || !ensure_dir(dir) || !load_rows(path, rows, &count)) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        size_t match_count = 0U;
        if (strcmp(rows[i].field, field_key) != 0) {
            continue;
        }
        for (value_i = 0U; value_i < value_count; ++value_i) {
            if (strcmp(rows[i].axis, values[value_i].axis) == 0) {
                snprintf(rows[i].value, sizeof(rows[i].value), "%s", values[value_i].value);
                ++updated[value_i];
                ++match_count;
            }
        }
        if (match_count > 1U) {
            return 0;
        }
    }
    for (value_i = 0U; value_i < value_count; ++value_i) {
        if (updated[value_i] != 1U) {
            return 0;
        }
    }
    return save_rows(path, rows, count);
}
