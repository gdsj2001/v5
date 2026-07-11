#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_native_rtcp_status.h"
#include "v5_native_wcs_status.h"
#include "v5_native_g53_geometry_status.h"
#include "v5_native_modal_tool_status.h"
#include "v5_native_operator_error_status.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"
#include "v5_v3_local_pages.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "v5_ui_shell_internal.h"

static void shell_program_dir_path(char *out, size_t out_size)
{
    int rc;
    const char *root = g_v5_shell_project_root[0] ? g_v5_shell_project_root : ".";
    if (!out || out_size == 0U) {
        return;
    }
    rc = snprintf(out, out_size, "%s/%s", root, "gcode/golden");
    if (rc <= 0 || (size_t)rc >= out_size) {
        out[0] = '\0';
    }
}

static int shell_has_program_extension(const char *name)
{
    char ext[16];
    const char *dot = name ? strrchr(name, '.') : 0;
    size_t len;
    size_t i;
    if (!dot) {
        return 0;
    }
    len = strlen(dot);
    if (len == 0U || len >= sizeof(ext)) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[len] = '\0';
    return strcmp(ext, ".ngc") == 0 || strcmp(ext, ".nc") == 0 || strcmp(ext, ".tap") == 0 || strcmp(ext, ".gcode") == 0;
}

static int shell_safe_program_basename(const char *name)
{
    const char *p;
    if (!name || !name[0] || strlen(name) >= sizeof(g_v5_shell_program_rows[0].name)) {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strstr(name, "..")) {
        return 0;
    }
    for (p = name; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '/' || ch == '\\' || ch == ':') {
            return 0;
        }
        if (!(isalnum(ch) || ch == ' ' || ch == '.' || ch == '_' || ch == '-' || ch == '(' || ch == ')' || ch == '+' || ch == '[' || ch == ']')) {
            return 0;
        }
    }
    return shell_has_program_extension(name);
}

static void shell_trim_trailing_line_end(char *text)
{
    size_t len;
    if (!text) {
        return;
    }
    len = strlen(text);
    while (len > 0U && (text[len - 1U] == '\n' || text[len - 1U] == '\r')) {
        text[--len] = '\0';
    }
}

void shell_clear_mdi_edit_metadata(void)
{
    g_v5_shell_mdi_edit_program_name[0] = '\0';
    g_v5_shell_mdi_edit_program_path[0] = '\0';
    g_v5_shell_mdi_edit_prepared = 0;
}

int shell_program_path_allowed(const char *path)
{
    char dir_path[384];
    size_t dir_len;
    const char *base;
    if (!path || !path[0]) {
        return 0;
    }
    shell_program_dir_path(dir_path, sizeof(dir_path));
    if (!dir_path[0]) {
        return 0;
    }
    dir_len = strlen(dir_path);
    if (strncmp(path, dir_path, dir_len) != 0 || path[dir_len] != '/') {
        return 0;
    }
    base = path + dir_len + 1U;
    if (!base[0] || strchr(base, '/') || strchr(base, '\\')) {
        return 0;
    }
    return shell_safe_program_basename(base);
}

static void shell_set_mdi_edit_metadata(const char *program_name, const char *path)
{
    snprintf(g_v5_shell_mdi_edit_program_name, sizeof(g_v5_shell_mdi_edit_program_name), "%s", program_name && program_name[0] ? program_name : "opened program");
    snprintf(g_v5_shell_mdi_edit_program_path, sizeof(g_v5_shell_mdi_edit_program_path), "%s", path && path[0] ? path : "");
}

int shell_load_mdi_edit_text(
    const char *program_name,
    const char *path,
    const char *text,
    size_t size,
    int editable_file,
    const char *event_name)
{
    char detail[512];
    if (!text) {
        snprintf(detail, sizeof(detail), "%s missing text", event_name ? event_name : "mdi_edit");
        shell_log_mdi_event("program_edit_rejected", detail, 0);
        return 0;
    }
    if (size == 0U) {
        size = strlen(text);
    }
    if (size == 0U || size >= sizeof(g_v5_shell_mdi_line)) {
        snprintf(detail, sizeof(detail), "%s bytes=%u", event_name ? event_name : "mdi_edit", (unsigned)size);
        shell_log_mdi_event("program_edit_oversize_or_empty", detail, 0);
        return 0;
    }
    if (editable_file && !shell_program_path_allowed(path)) {
        snprintf(detail, sizeof(detail), "%s path=%s", event_name ? event_name : "mdi_edit", path ? path : "");
        shell_log_mdi_event("program_edit_bad_path", detail, 0);
        return 0;
    }
    memcpy(g_v5_shell_mdi_line, text, size);
    g_v5_shell_mdi_line[size] = '\0';
    shell_trim_trailing_line_end(g_v5_shell_mdi_line);
    if (!g_v5_shell_mdi_line[0]) {
        shell_log_mdi_event("program_edit_empty_after_trim", event_name ? event_name : "mdi_edit", 0);
        return 0;
    }
    if (editable_file) {
        shell_set_mdi_edit_metadata(program_name, path);
    } else {
        shell_clear_mdi_edit_metadata();
    }
    g_v5_shell_mdi_edit_prepared = 1;
    shell_update_mdi_line();
    if (g_v5_shell_mdi_status_label) {
        if (g_v5_shell_mdi_edit_program_path[0]) {
            char status[220];
            snprintf(status, sizeof(status), "修改区: %s", g_v5_shell_mdi_edit_program_name[0] ? g_v5_shell_mdi_edit_program_name : "G-code");
            lv_label_set_text(g_v5_shell_mdi_status_label, status);
        } else {
            lv_label_set_text(g_v5_shell_mdi_status_label, "Native MDI");
        }
    }
    snprintf(detail, sizeof(detail), "%s bytes=%u path=%s", event_name ? event_name : "mdi_edit", (unsigned)strlen(g_v5_shell_mdi_line), g_v5_shell_mdi_edit_program_path);
    shell_log_mdi_event("program_edit_loaded", detail, 1);
    return 1;
}

static int shell_compare_program_rows(const void *left, const void *right)
{
    const V5ProgramRow *a = (const V5ProgramRow *)left;
    const V5ProgramRow *b = (const V5ProgramRow *)right;
    return strcmp(a->name, b->name);
}

int shell_load_program_rows(void)
{
    DIR *dir;
    struct dirent *entry;
    char dir_path[384];
    unsigned int scan_count = 0U;
    unsigned int i;
    shell_program_dir_path(dir_path, sizeof(dir_path));
    g_v5_shell_program_row_count = 0U;
    memset(g_v5_shell_program_rows, 0, sizeof(g_v5_shell_program_rows));
    memset(g_v5_shell_program_scan_rows, 0, sizeof(g_v5_shell_program_scan_rows));
    if (!dir_path[0]) {
        return 0;
    }
    dir = opendir(dir_path);
    if (!dir) {
        g_v5_shell_program_selected_index = -1;
        g_v5_shell_program_confirm_selected_index = -1;
        g_v5_shell_program_confirm_selected_path[0] = '\0';
        return 0;
    }
    while ((entry = readdir(dir)) != 0) {
        V5ProgramRow *row;
        struct stat st;
        int rc;
        if (!shell_safe_program_basename(entry->d_name) || scan_count >= V5_PROGRAM_SCAN_MAX) {
            continue;
        }
        row = &g_v5_shell_program_scan_rows[scan_count];
        {
            size_t name_len = strlen(entry->d_name);
            memcpy(row->name, entry->d_name, name_len + 1U);
        }
        rc = snprintf(row->path, sizeof(row->path), "%s/%s", dir_path, entry->d_name);
        if (rc <= 0 || (size_t)rc >= sizeof(row->path)) {
            continue;
        }
        if (stat(row->path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        row->exists = 1;
        shell_format_program_size(st.st_size, row->size, sizeof(row->size));
        shell_format_program_date(st.st_mtime, row->created, sizeof(row->created));
        shell_format_program_date(st.st_mtime, row->modified, sizeof(row->modified));
        ++scan_count;
    }
    closedir(dir);
    qsort(g_v5_shell_program_scan_rows, scan_count, sizeof(g_v5_shell_program_scan_rows[0]), shell_compare_program_rows);
    g_v5_shell_program_row_count = scan_count > V5_PROGRAM_ROWS_MAX ? V5_PROGRAM_ROWS_MAX : scan_count;
    for (i = 0U; i < g_v5_shell_program_row_count; ++i) {
        g_v5_shell_program_rows[i] = g_v5_shell_program_scan_rows[i];
    }
    if (g_v5_shell_program_row_count == 0U) {
        g_v5_shell_program_selected_index = -1;
        g_v5_shell_program_confirm_selected_index = -1;
        g_v5_shell_program_confirm_selected_path[0] = '\0';
    } else if (g_v5_shell_program_selected_index < 0 || (unsigned int)g_v5_shell_program_selected_index >= g_v5_shell_program_row_count) {
        g_v5_shell_program_selected_index = -1;
        g_v5_shell_program_confirm_selected_index = -1;
        g_v5_shell_program_confirm_selected_path[0] = '\0';
    } else if (g_v5_shell_program_confirm_selected_index != g_v5_shell_program_selected_index ||
               strcmp(g_v5_shell_program_confirm_selected_path, g_v5_shell_program_rows[g_v5_shell_program_selected_index].path) != 0) {
        g_v5_shell_program_confirm_selected_index = -1;
        g_v5_shell_program_confirm_selected_path[0] = '\0';
    }
    return 1;
}
