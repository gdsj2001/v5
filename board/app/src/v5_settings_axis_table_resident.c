#include "v5_settings_axis_table.h"
#include "v5_boot_closure.h"
#include "v5_button_visuals.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_parameter_store.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_axis_table_internal.h"

void v5_settings_axis_clear_values(void)
{
    unsigned int r;
    unsigned int c;
    snprintf(g_v5_axis_table_motion_model_value, V5_AXIS_VALUE_CAP, "--");
    g_v5_axis_table_motion_model_real = 0U;
    snprintf(g_v5_axis_table_bus_pulse_value, V5_AXIS_VALUE_CAP, "--");
    g_v5_axis_table_bus_pulse_real = 0U;
    v5_settings_axis_clear_slave_options();
    v5_settings_axis_clear_slave_drive_display();
    for (r = 0U; r < V5_G53_ROW_COUNT; ++r) {
        for (c = 0U; c < 3U; ++c) {
            snprintf(g_v5_axis_table_g53_values[r][c], V5_AXIS_VALUE_CAP, "--");
            g_v5_axis_table_g53_value_real[r][c] = 0U;
        }
    }
    for (r = 0U; r < V5_AXIS_TABLE_MAX_ROWS; ++r) {
        for (c = 0U; c < V5_AXIS_TABLE_MAX_COLS; ++c) {
            snprintf(g_v5_axis_table_values[r][c], V5_AXIS_VALUE_CAP, "--");
            g_v5_axis_table_value_real[r][c] = 0U;
        }
    }
    g_v5_axis_table_loaded = 0;
}

void v5_settings_axis_copy_axis_value(char *dst, size_t cap, const char *value)
{
    if (!dst || cap == 0U || !value || !value[0]) {
        return;
    }
    snprintf(dst, cap, "%s", value);
}

int v5_settings_axis_next_text_line(const char **cursor, char *line, size_t cap)
{
    const char *p;
    size_t n = 0U;
    if (!cursor || !*cursor || !line || cap == 0U || !**cursor) {
        return 0;
    }
    p = *cursor;
    while (*p && *p != '\n') {
        if (n + 1U < cap) {
            line[n++] = *p;
        }
        ++p;
    }
    if (*p == '\n') {
        ++p;
    }
    line[n] = '\0';
    *cursor = p;
    return 1;
}

static char *resident_parameter_table_text(V5SettingsParameterDiskTable table)
{
    switch (table) {
    case V5_SETTINGS_PARAMETER_DISK_SELF:
        return g_v5_axis_table_self_parameter_text;
    case V5_SETTINGS_PARAMETER_DISK_DRIVE:
        return g_v5_axis_table_drive_parameter_text;
    default:
        return 0;
    }
}

static uint32_t settings_fnv1a_update(uint32_t hash, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!p) {
        return hash;
    }
    while (*p) {
        hash ^= (uint32_t)*p++;
        hash *= 16777619U;
    }
    return hash;
}

static unsigned int settings_nonzero_generation(uint32_t hash)
{
    hash ^= hash >> 16;
    hash *= 0x7feb352dU;
    hash ^= hash >> 15;
    return hash ? (unsigned int)hash : 1U;
}

unsigned int v5_settings_axis_settings_axis_owner_generation(const char *field_id)
{
    uint32_t hash = 2166136261U;
    hash = settings_fnv1a_update(hash, g_v5_axis_table_project_root);
    hash = settings_fnv1a_update(hash, resident_parameter_table_text(V5_SETTINGS_PARAMETER_DISK_SELF));
    hash = settings_fnv1a_update(hash, resident_parameter_table_text(V5_SETTINGS_PARAMETER_DISK_DRIVE));
    hash = settings_fnv1a_update(hash, field_id);
    return settings_nonzero_generation(hash);
}

unsigned int v5_settings_axis_settings_axis_readback_token(const char *field_id, const char *value)
{
    uint32_t hash = 2166136261U;
    hash = settings_fnv1a_update(hash, field_id);
    hash = settings_fnv1a_update(hash, value);
    return settings_nonzero_generation(hash);
}

void v5_settings_axis_set_resident_parameter_table(V5SettingsParameterDiskTable table, const V5BootClosureResidentText *blob)
{
    char *dst = resident_parameter_table_text(table);
    if (!dst) return;
    dst[0] = '\0';
    if (blob && blob->loaded && blob->text[0]) {
        snprintf(dst, V5_BOOT_CLOSURE_TEXT_CAP, "%s", blob->text);
    }
}

int v5_settings_axis_resident_parameter_table_read_axis(V5SettingsParameterDiskTable table,
                                              const char *axis,
                                              const char *field_key,
                                              char *out,
                                              size_t out_cap)
{
    const char *text_blob = resident_parameter_table_text(table);
    const char *cursor;
    char line[320];
    int found = 0;
    if (!text_blob || !axis || !axis[0] || !field_key || !field_key[0] || !out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    cursor = text_blob;
    while (v5_settings_axis_next_text_line(&cursor, line, sizeof(line))) {
        char *line_axis;
        char *line_field;
        char *line_value;
        v5_settings_axis_trim_in_place(line);
        if (!line[0] || line[0] == '#') continue;
        line_axis = line;
        line_field = strchr(line_axis, '\t');
        if (!line_field) continue;
        *line_field++ = '\0';
        line_value = strchr(line_field, '\t');
        if (!line_value) continue;
        *line_value++ = '\0';
        v5_settings_axis_trim_in_place(line_axis);
        v5_settings_axis_trim_in_place(line_field);
        v5_settings_axis_trim_in_place(line_value);
        if (strcmp(line_axis, axis) == 0 && strcmp(line_field, field_key) == 0 && line_value[0]) {
            snprintf(out, out_cap, "%s", line_value);
            found = 1;
        }
    }
    return found && out[0];
}

int v5_settings_axis_resident_parameter_table_set_axis(
    V5SettingsParameterDiskTable table,
    const char *axis,
    const char *field_key,
    const char *value)
{
    char *text_blob = resident_parameter_table_text(table);
    char *line_start;
    size_t axis_len;
    size_t field_len;
    size_t value_len;
    if (!text_blob || !axis || !axis[0] || !field_key || !field_key[0] || !value || !value[0]) {
        return 0;
    }
    axis_len = strlen(axis);
    field_len = strlen(field_key);
    value_len = strlen(value);
    line_start = text_blob;
    while (*line_start) {
        char *line_end = strchr(line_start, '\n');
        char *first_tab;
        char *second_tab;
        char *value_end;
        size_t old_len;
        size_t total_len;
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }
        first_tab = (char *)memchr(line_start, '\t', (size_t)(line_end - line_start));
        second_tab = first_tab ? (char *)memchr(first_tab + 1, '\t', (size_t)(line_end - first_tab - 1)) : 0;
        if (first_tab && second_tab && (size_t)(first_tab - line_start) == axis_len &&
            (size_t)(second_tab - first_tab - 1) == field_len &&
            memcmp(line_start, axis, axis_len) == 0 &&
            memcmp(first_tab + 1, field_key, field_len) == 0) {
            value_end = line_end;
            while (value_end > second_tab + 1 && value_end[-1] == '\r') {
                --value_end;
            }
            old_len = (size_t)(value_end - (second_tab + 1));
            total_len = strlen(text_blob);
            if (total_len - old_len + value_len >= V5_BOOT_CLOSURE_TEXT_CAP) {
                return 0;
            }
            memmove(second_tab + 1 + value_len, value_end, strlen(value_end) + 1U);
            memcpy(second_tab + 1, value, value_len);
            return 1;
        }
        if (!*line_end) {
            break;
        }
        line_start = line_end + 1;
    }
    return 0;
}

int v5_settings_axis_row_index_for_axis_name(const char *axis)
{
    unsigned int i;
    if (!axis || !axis[0]) return -1;
    for (i = 0U; i < v5_settings_axis_table_row_count(); ++i) {
        if (strcmp(g_v5_axis_table_rows[i].axis, axis) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void trim_ascii_in_place(char *text)
{
    char *start;
    size_t len;
    if (!text || !text[0]) return;
    start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    len = strlen(text);
    while (len > 0U && isspace((unsigned char)text[len - 1U])) {
        text[--len] = '\0';
    }
}

void v5_settings_axis_slave_option_extract_id(const char *text, char *out, size_t cap)
{
    size_t n = 0U;
    const char *p;
    if (!out || cap == 0U) return;
    out[0] = '\0';
    if (!text) return;
    p = text;
    while (*p && isspace((unsigned char)*p)) ++p;
    while (*p && !isspace((unsigned char)*p) && *p != ':' && *p != ',' && *p != ';' && *p != '|') {
        if (n + 1U < cap) out[n++] = *p;
        ++p;
    }
    out[n] = '\0';
}

int v5_settings_axis_slave_id_is_nat(const char *id)
{
    return id && strcmp(id, "NAT") == 0;
}

int v5_settings_axis_slave_option_is_nat(const char *text)
{
    char id[V5_AXIS_VALUE_CAP];
    v5_settings_axis_slave_option_extract_id(text, id, sizeof(id));
    return v5_settings_axis_slave_id_is_nat(id);
}

int v5_settings_axis_slave_option_same_id(const char *a, const char *b)
{
    char id_a[V5_AXIS_VALUE_CAP];
    char id_b[V5_AXIS_VALUE_CAP];
    v5_settings_axis_slave_option_extract_id(a, id_a, sizeof(id_a));
    v5_settings_axis_slave_option_extract_id(b, id_b, sizeof(id_b));
    return id_a[0] && id_b[0] && strcmp(id_a, id_b) == 0;
}

static void format_slave_option_label(const char *value, char *out, size_t cap)
{
    char raw[V5_SLAVE_OPTION_CAP];
    char id[V5_AXIS_VALUE_CAP];
    char name[V5_SLAVE_OPTION_CAP];
    char *sep;
    if (!out || cap == 0U) return;
    out[0] = '\0';
    if (!value || !value[0]) return;
    snprintf(raw, sizeof(raw), "%s", value);
    trim_ascii_in_place(raw);
    if (!raw[0]) return;
    v5_settings_axis_slave_option_extract_id(raw, id, sizeof(id));
    sep = strchr(raw, ':');
    if (sep && id[0]) {
        snprintf(name, sizeof(name), "%s", sep + 1);
        trim_ascii_in_place(name);
        if (name[0]) {
            snprintf(out, cap, "%s %s", id, name);
            return;
        }
    }
    snprintf(out, cap, "%s", raw);
}

static void add_slave_option(const char *value)
{
    unsigned int i;
    char label[V5_SLAVE_OPTION_CAP];
    format_slave_option_label(value, label, sizeof(label));
    if (!label[0]) return;
    for (i = 0U; i < g_v5_axis_table_slave_option_count; ++i) {
        if (strcmp(g_v5_axis_table_slave_options[i], label) == 0 || v5_settings_axis_slave_option_same_id(g_v5_axis_table_slave_options[i], label)) return;
    }
    if (g_v5_axis_table_slave_option_count >= V5_SLAVE_OPTION_MAX) return;
    snprintf(g_v5_axis_table_slave_options[g_v5_axis_table_slave_option_count], V5_SLAVE_OPTION_CAP, "%s", label);
    ++g_v5_axis_table_slave_option_count;
}

static void add_slave_options_from_text(const char *text)
{
    char token[V5_SLAVE_OPTION_CAP];
    size_t n = 0U;
    const char *p;
    if (!text) return;
    for (p = text; ; ++p) {
        int sep = (*p == '\0' || *p == ',' || *p == ';' || *p == '|');
        if (sep) {
            if (n > 0U) {
                token[n] = '\0';
                add_slave_option(token);
                n = 0U;
            }
            if (*p == '\0') break;
        } else if (n + 1U < sizeof(token)) {
            token[n++] = *p;
        }
    }
}

void v5_settings_axis_load_slave_options_from_resident_self_table(void)
{
    char value[256];
    if (v5_settings_axis_resident_parameter_table_read_axis(V5_SETTINGS_PARAMETER_DISK_SELF,
                                           "SETTINGS", "slave_options", value, sizeof(value))) {
        add_slave_options_from_text(value);
    }
}

void v5_settings_axis_reload_slave_options_from_resident_self_table(void)
{
    v5_settings_axis_clear_slave_options();
    v5_settings_axis_load_slave_options_from_resident_self_table();
}
