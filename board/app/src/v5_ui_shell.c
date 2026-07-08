#include "v5_app.h"

#include "lvgl.h"
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
#include "v5_command_gate_ipc.h"
#include "v5_settings_page.h"
#include "v5_settings_axis_table.h"
#include "v5_status_shm.h"
#include "v5_ui_model.h"
#include "v5_v3_local_pages.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define V5_PROGRAM_ROWS_MAX 9U
#define V5_PROGRAM_SCAN_MAX 256U
#define V5_PROGRAM_ROW_HIT_H 32
#define V5_PROGRAM_DOUBLE_CLICK_NS 800000000ULL

typedef enum V5ShellPageKind {
    V5_SHELL_PAGE_MAIN = 0,
    V5_SHELL_PAGE_SETTINGS,
    V5_SHELL_PAGE_TOOL,
    V5_SHELL_PAGE_PROBE,
    V5_SHELL_PAGE_OFFSET,
    V5_SHELL_PAGE_IO,
    V5_SHELL_PAGE_NETWORK,
    V5_SHELL_PAGE_PROGRAM,
    V5_SHELL_PAGE_MDI,
    V5_SHELL_PAGE_COUNT
} V5ShellPageKind;

typedef struct V5ProgramRow {
    char name[168];
    char size[20];
    char created[20];
    char modified[20];
    char path[384];
    int exists;
} V5ProgramRow;

static V5MainPage g_main_page;
static V5SettingsPage g_settings_page;
static V5ProgramController g_program_controller;
static V5UiModel g_model;
static lv_obj_t *g_shell_pages[V5_SHELL_PAGE_COUNT];
static lv_obj_t *g_program_count_label;
static lv_obj_t *g_program_empty_label;
static lv_obj_t *g_program_source_label;
static lv_obj_t *g_program_row_layers[V5_PROGRAM_ROWS_MAX];
static lv_obj_t *g_program_row_name_labels[V5_PROGRAM_ROWS_MAX];
static lv_obj_t *g_program_row_size_labels[V5_PROGRAM_ROWS_MAX];
static lv_obj_t *g_program_row_created_labels[V5_PROGRAM_ROWS_MAX];
static lv_obj_t *g_program_row_modified_labels[V5_PROGRAM_ROWS_MAX];
static int g_program_row_indices[V5_PROGRAM_ROWS_MAX];
static lv_obj_t *g_mdi_line_label;
static lv_obj_t *g_mdi_status_label;
static char g_project_root[256];
static V5ProgramRow g_program_rows[V5_PROGRAM_ROWS_MAX];
static V5ProgramRow g_program_scan_rows[V5_PROGRAM_SCAN_MAX];
static unsigned int g_program_row_count;
static int g_program_selected_index = -1;
static int g_program_confirm_selected_index = -1;
static char g_program_confirm_selected_path[384];
static int g_program_last_click_index = -1;
static unsigned long long g_program_last_click_ns;
static char g_program_last_click_path[384];
static char g_mdi_line[128] = "";
static int g_ui_ready;
static int g_main_cache_dirty;
static V5ShellPageKind g_current_page = V5_SHELL_PAGE_MAIN;
static lv_obj_t *g_top_status_layer;
static lv_obj_t *g_top_status_label;

static void shell_navigate(void *user_data, V5MainPageActionKind action);
static void shell_clear_style(lv_obj_t *obj);
static void shell_update_program_row(void);
static void shell_format_program_size(off_t size, char *out, size_t out_cap);
static void shell_format_program_date(time_t when, char *out, size_t out_cap);
static lv_color_t shell_rgb(uint8_t r, uint8_t g, uint8_t b);
static unsigned long long shell_monotonic_ns(void);
static int shell_toolpath_touch_points(const lv_point_t *points, int count, int pressed, int *changed, void *user_data);
static void shell_update_top_status_label(void);

#define V5_UI_DYNAMIC_REFRESH_NS 33333333ULL
#define V5_UI_BUTTON_REFRESH_NS 100000000ULL
#define V5_UI_ESTOP_REFRESH_NS 100000000ULL
#define V5_UI_SLOW_REFRESH_NS 200000000ULL
#define V5_NATIVE_READBACK_MIN_NS 200000000ULL
#define V5_SAFETY_READBACK_MIN_NS 100000000ULL
#define V5_SAFETY_READBACK_TIMEOUT_MS 80U

static unsigned long long g_native_readback_last_probe_ns;
static unsigned long long g_safety_readback_last_probe_ns;
static unsigned long long g_ui_dynamic_last_refresh_ns;
static unsigned long long g_ui_button_last_refresh_ns;
static unsigned long long g_ui_estop_last_refresh_ns;
static unsigned long long g_ui_slow_last_refresh_ns;

static int shell_refresh_native_readback(int force)
{
    V5NativeReadback readback;
    V5NativeReadback rtcp_readback;
    V5NativeReadback wcs_readback;
    V5NativeReadback g53_geometry_readback;
    V5NativeReadback modal_tool_readback;
    unsigned long long now;

    now = shell_monotonic_ns();
    if (!force && g_native_readback_last_probe_ns != 0ULL &&
        now - g_native_readback_last_probe_ns < V5_NATIVE_READBACK_MIN_NS) {
        return 1;
    }
    g_native_readback_last_probe_ns = now;

    readback = g_main_page.native_readback;
    v5_native_readback_init(&rtcp_readback);
    v5_native_readback_init(&wcs_readback);
    v5_native_readback_init(&g53_geometry_readback);
    v5_native_readback_init(&modal_tool_readback);
    if (v5_native_rtcp_status_read(0, V5_NATIVE_RTCP_STATUS_DEFAULT_MAX_AGE_MS, &rtcp_readback) &&
        v5_native_readback_rtcp_known(&rtcp_readback)) {
        v5_native_readback_set_rtcp_actual(&readback, rtcp_readback.rtcp_enabled);
    }
    if (v5_native_wcs_status_read(0, V5_NATIVE_WCS_STATUS_DEFAULT_MAX_AGE_MS, &wcs_readback) &&
        v5_native_readback_wcs_known(&wcs_readback)) {
        if (v5_native_readback_wcs_table_known(&wcs_readback)) {
            v5_native_readback_set_wcs_table(
                &readback,
                wcs_readback.wcs_index,
                &wcs_readback.wcs_offsets[0][0],
                V5_NATIVE_READBACK_WCS_COUNT,
                V5_NATIVE_READBACK_WCS_AXIS_COUNT,
                wcs_readback.wcs_offsets_epoch);
        } else {
            v5_native_readback_set_wcs_actual(&readback, wcs_readback.wcs_index);
        }
    }
    if (v5_native_g53_geometry_status_read(0, V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS, &g53_geometry_readback) &&
        v5_native_readback_g53_geometry_known(&g53_geometry_readback)) {
        v5_native_readback_set_g53_geometry(
            &readback,
            &g53_geometry_readback.g53_centers[0][0],
            V5_NATIVE_READBACK_G53_CENTER_COUNT,
            V5_NATIVE_READBACK_G53_AXIS_COUNT,
            g53_geometry_readback.g53_geometry_epoch);
    }
    if (v5_native_modal_tool_status_read(0, V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS, &modal_tool_readback)) {
        if (v5_native_readback_modal_known(&modal_tool_readback)) {
            v5_native_readback_set_modal_actual(&readback, modal_tool_readback.modal_text);
        }
        if (v5_native_readback_tool_known(&modal_tool_readback)) {
            v5_native_readback_set_tool_actual(
                &readback,
                modal_tool_readback.tool_number,
                v5_native_readback_tool_length_known(&modal_tool_readback),
                modal_tool_readback.tool_length_mm);
        }
        if (v5_native_readback_interpreter_idle_known(&modal_tool_readback)) {
            v5_native_readback_set_interpreter_idle(&readback, modal_tool_readback.interpreter_idle);
        }
        if (v5_native_readback_all_homed_known(&modal_tool_readback)) {
            v5_native_readback_set_all_homed(&readback, modal_tool_readback.all_homed);
        }
    }

    v5_main_page_set_native_readback(&g_main_page, &readback);
    shell_update_top_status_label();
    return 1;
}

static int shell_refresh_safety_readback(int force)
{
    V5NativeReadback readback;
    V5CommandGateResult gate_result;
    unsigned long long now;
    int estop_ok;
    int machine_ok;

    now = shell_monotonic_ns();
    if (!force && g_safety_readback_last_probe_ns != 0ULL &&
        now - g_safety_readback_last_probe_ns < V5_SAFETY_READBACK_MIN_NS) {
        return 1;
    }
    g_safety_readback_last_probe_ns = now;

    readback = g_main_page.native_readback;
    v5_command_gate_result_init(&gate_result);
    (void)v5_command_gate_probe_safety(&gate_result, force ? 1000U : V5_SAFETY_READBACK_TIMEOUT_MS);
    estop_ok = gate_result.safety_estop_known;
    machine_ok = gate_result.machine_enable_known;
    if (estop_ok) {
        v5_native_readback_set_safety_estop(&readback, gate_result.safety_estop_active);
    }
    if (machine_ok) {
        v5_native_readback_set_machine_enabled(&readback, gate_result.machine_enabled);
    }
    if (estop_ok || machine_ok) {
        v5_main_page_set_native_readback(&g_main_page, &readback);
        shell_update_top_status_label();
        return 1;
    }
    return 0;
}


static void shell_refresh_native_readback_for_action(void *user_data, V5MainPageActionKind action)
{
    int reset_semantics;
    (void)user_data;
    if (action == V5_MAIN_PAGE_ACTION_ESTOP_FORCE) {
        (void)shell_refresh_safety_readback(1);
        reset_semantics =
            (v5_native_readback_safety_estop_known(&g_main_page.native_readback) &&
             g_main_page.native_readback.safety_estop_active) ||
            (v5_native_readback_machine_enable_known(&g_main_page.native_readback) &&
             !g_main_page.native_readback.machine_enabled);
        if (!reset_semantics) {
            return;
        }
    }
    (void)shell_refresh_native_readback(1);
    (void)shell_refresh_safety_readback(1);
}

static int shell_toolpath_touch_points(const lv_point_t *points, int count, int pressed, int *changed, void *user_data)
{
    int local_changed = 0;
    int consumed;
    (void)user_data;
    if (changed) {
        *changed = 0;
    }
    if (!g_main_page.root) {
        return 0;
    }
    consumed = v5_main_page_handle_touch_points(&g_main_page, points, count, pressed, &local_changed);
    if (local_changed) {
        (void)v5_main_page_apply_status_flags(&g_main_page, &g_model.status_view, V5_MAIN_PAGE_REFRESH_DYNAMIC);
        lv_timer_handler();
        if (changed) {
            *changed = 1;
        }
    }
    return consumed;
}

static unsigned long long shell_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000000000ULL) + (unsigned long long)ts.tv_nsec;
}

static void log_navigation_perf(const char *target, int cache_ok, unsigned long long elapsed_ns)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/navigation_perf.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp,
            "{\"schema\":\"v5.navigation_perf.v1\",\"target\":\"%s\",\"cache_blit\":%s,\"elapsed_us\":%llu}\n",
            target ? target : "",
            cache_ok ? "true" : "false",
            elapsed_ns / 1000ULL);
    fclose(fp);
}

static void shell_create_top_status_layer(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    g_top_status_layer = lv_obj_create(screen);
    shell_clear_style(g_top_status_layer);
    lv_obj_set_pos(g_top_status_layer, 270, 10);
    lv_obj_set_size(g_top_status_layer, 500, 34);
    lv_obj_set_style_bg_opa(g_top_status_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_top_status_layer, 0, 0);
    lv_obj_set_style_radius(g_top_status_layer, 0, 0);
    lv_obj_set_style_pad_all(g_top_status_layer, 0, 0);
    lv_obj_clear_flag(g_top_status_layer, LV_OBJ_FLAG_SCROLLABLE);

    g_top_status_label = lv_label_create(g_top_status_layer);
    lv_obj_set_pos(g_top_status_label, 0, 5);
    lv_obj_set_size(g_top_status_label, 500, 24);
    lv_obj_set_style_text_align(g_top_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_top_status_label, lv_color_make(255, 86, 86), 0);
    lv_label_set_long_mode(g_top_status_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(g_top_status_label, "未回零: 开机后需回零一次");
}

static void shell_update_top_status_label(void)
{
    if (!g_top_status_label) {
        return;
    }
    lv_obj_set_style_text_color(g_top_status_label, lv_color_make(255, 86, 86), 0);
    if (!v5_native_readback_all_homed_known(&g_main_page.native_readback)) {
        lv_label_set_text(g_top_status_label, "回零状态未知");
        return;
    }
    if (g_main_page.native_readback.all_homed) {
        lv_label_set_text(g_top_status_label, "");
        return;
    }
    lv_label_set_text(g_top_status_label, "未回零: 开机后需回零一次");
}

static void shell_return_button_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MAIN);
    }
}

static void shell_json_text(FILE *fp, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', fp);
    while (*p) {
        if (*p == '"' || *p == '\\') {
            fputc('_', fp);
        } else if (*p >= 32U && *p < 127U) {
            fputc((int)*p, fp);
        }
        ++p;
    }
    fputc('"', fp);
}

static void shell_log_program_event(const char *event, const char *path, int ok, const V5ProgramOpenResult *result)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/program_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp, "{\"schema\":\"v5.program_page.v1\",\"event\":");
    shell_json_text(fp, event);
    fprintf(fp, ",\"path\":");
    shell_json_text(fp, path);
    fprintf(fp, ",\"ok\":%s", ok ? "true" : "false");
    if (result) {
        fprintf(fp, ",\"generation\":%u,\"loaded_epoch\":%u,\"bytes\":%lu,\"lines\":%u,\"display\":",
                result->generation,
                result->loaded_epoch,
                (unsigned long)result->byte_count,
                result->line_count);
        shell_json_text(fp, result->display_name);
        fprintf(fp, ",\"source_sha256\":");
        shell_json_text(fp, result->source_sha256);
        fprintf(fp,
                ",\"preview_kept\":%u,\"preview_candidates\":%u,\"preview_segments\":%u,\"preview_truncated\":%s",
                result->preview_kept_count,
                result->preview_candidate_count,
                result->preview_segment_count,
                result->preview_truncated ? "true" : "false");
    }
    if (strcmp(event ? event : "", "open_applied") == 0 ||
        strcmp(event ? event : "", "program_file_double_click_applied") == 0) {
        fprintf(fp,
                ",\"toolpath_points\":%u,\"toolpath_line_rewrites\":%u,\"toolpath_cache_hits\":%u,\"toolpath_cache_misses\":%u",
                g_main_page.trajectory_point_count,
                g_main_page.toolpath_line_rewrite_count,
                g_main_page.toolpath_static_cache_hits,
                g_main_page.toolpath_static_cache_misses);
        {
            unsigned int i;
            int min_x = 0;
            int max_x = 0;
            int min_y = 0;
            int max_y = 0;
            if (g_main_page.trajectory_point_count > 0U) {
                min_x = max_x = g_main_page.trajectory_points[0].x;
                min_y = max_y = g_main_page.trajectory_points[0].y;
                for (i = 1U; i < g_main_page.trajectory_point_count; ++i) {
                    if (g_main_page.trajectory_points[i].x < min_x) min_x = g_main_page.trajectory_points[i].x;
                    if (g_main_page.trajectory_points[i].x > max_x) max_x = g_main_page.trajectory_points[i].x;
                    if (g_main_page.trajectory_points[i].y < min_y) min_y = g_main_page.trajectory_points[i].y;
                    if (g_main_page.trajectory_points[i].y > max_y) max_y = g_main_page.trajectory_points[i].y;
                }
            }
            fprintf(fp,
                    ",\"program_wcs_valid\":%s,\"program_wcs_index\":%d,\"program_wcs_offset_x\":%.6f,\"program_wcs_offset_y\":%.6f,\"program_wcs_offset_z\":%.6f,\"toolpath_hidden\":%s,\"toolpath_min_x\":%d,\"toolpath_min_y\":%d,\"toolpath_max_x\":%d,\"toolpath_max_y\":%d",
                    g_main_page.toolpath_program_wcs_valid ? "true" : "false",
                    g_main_page.toolpath_program_wcs_index,
                    g_main_page.toolpath_program_wcs_offset[0],
                    g_main_page.toolpath_program_wcs_offset[1],
                    g_main_page.toolpath_program_wcs_offset[2],
                    lv_obj_has_flag(g_main_page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ? "true" : "false",
                    min_x,
                    min_y,
                    max_x,
                    max_y);
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
}

static void shell_log_mdi_event(const char *event, const char *line, int ok)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/mdi_events.jsonl", "ab");
    if (!fp) {
        return;
    }
    fprintf(fp, "{\"schema\":\"v5.mdi_page.v1\",\"event\":");
    shell_json_text(fp, event);
    fprintf(fp, ",\"line\":");
    shell_json_text(fp, line);
    fprintf(fp, ",\"ok\":%s}\n", ok ? "true" : "false");
    fclose(fp);
}

static void shell_update_mdi_line(void)
{
    if (g_mdi_line_label) {
        lv_label_set_text(g_mdi_line_label, g_mdi_line[0] ? g_mdi_line : "MDI>");
    }
    if (g_mdi_status_label) {
        lv_label_set_text(g_mdi_status_label, "Native MDI");
    }
}

static void shell_mdi_load_cb(lv_event_t *event)
{
    int ok;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    ok = v5_main_page_set_mdi_text(&g_main_page, g_mdi_line);
    shell_log_mdi_event("load", g_mdi_line, ok);
    if (ok) {
        g_main_cache_dirty = 1;
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MAIN);
    } else if (g_mdi_status_label) {
        lv_label_set_text(g_mdi_status_label, "载入失败");
    }
}

static void shell_program_dir_path(char *out, size_t out_size)
{
    int rc;
    const char *root = g_project_root[0] ? g_project_root : ".";
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
    if (!name || !name[0] || strlen(name) >= sizeof(g_program_rows[0].name)) {
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

static int shell_compare_program_rows(const void *left, const void *right)
{
    const V5ProgramRow *a = (const V5ProgramRow *)left;
    const V5ProgramRow *b = (const V5ProgramRow *)right;
    return strcmp(a->name, b->name);
}

static int shell_load_program_rows(void)
{
    DIR *dir;
    struct dirent *entry;
    char dir_path[384];
    unsigned int scan_count = 0U;
    unsigned int i;
    shell_program_dir_path(dir_path, sizeof(dir_path));
    g_program_row_count = 0U;
    memset(g_program_rows, 0, sizeof(g_program_rows));
    memset(g_program_scan_rows, 0, sizeof(g_program_scan_rows));
    if (!dir_path[0]) {
        return 0;
    }
    dir = opendir(dir_path);
    if (!dir) {
        g_program_selected_index = -1;
        g_program_confirm_selected_index = -1;
        g_program_confirm_selected_path[0] = '\0';
        return 0;
    }
    while ((entry = readdir(dir)) != 0) {
        V5ProgramRow *row;
        struct stat st;
        int rc;
        if (!shell_safe_program_basename(entry->d_name) || scan_count >= V5_PROGRAM_SCAN_MAX) {
            continue;
        }
        row = &g_program_scan_rows[scan_count];
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
    qsort(g_program_scan_rows, scan_count, sizeof(g_program_scan_rows[0]), shell_compare_program_rows);
    g_program_row_count = scan_count > V5_PROGRAM_ROWS_MAX ? V5_PROGRAM_ROWS_MAX : scan_count;
    for (i = 0U; i < g_program_row_count; ++i) {
        g_program_rows[i] = g_program_scan_rows[i];
    }
    if (g_program_row_count == 0U) {
        g_program_selected_index = -1;
        g_program_confirm_selected_index = -1;
        g_program_confirm_selected_path[0] = '\0';
    } else if (g_program_selected_index < 0 || (unsigned int)g_program_selected_index >= g_program_row_count) {
        g_program_selected_index = -1;
        g_program_confirm_selected_index = -1;
        g_program_confirm_selected_path[0] = '\0';
    } else if (g_program_confirm_selected_index != g_program_selected_index ||
               strcmp(g_program_confirm_selected_path, g_program_rows[g_program_selected_index].path) != 0) {
        g_program_confirm_selected_index = -1;
        g_program_confirm_selected_path[0] = '\0';
    }
    return 1;
}

static void shell_update_program_row_visual(unsigned int idx)
{
    V5ProgramRow *row;
    int selected;
    lv_obj_t *layer;
    if (idx >= V5_PROGRAM_ROWS_MAX || !g_program_row_layers[idx]) {
        return;
    }
    layer = g_program_row_layers[idx];
    if (idx >= g_program_row_count || !g_program_rows[idx].exists) {
        lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    row = &g_program_rows[idx];
    selected = ((int)idx == g_program_selected_index);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(layer, shell_rgb(selected ? 43 : 17, selected ? 133 : 40, selected ? 83 : 58), 0);
    if (g_program_row_name_labels[idx]) {
        lv_label_set_text(g_program_row_name_labels[idx], row->name);
    }
    if (g_program_row_size_labels[idx]) {
        lv_label_set_text(g_program_row_size_labels[idx], row->size);
    }
    if (g_program_row_created_labels[idx]) {
        lv_label_set_text(g_program_row_created_labels[idx], row->created);
    }
    if (g_program_row_modified_labels[idx]) {
        lv_label_set_text(g_program_row_modified_labels[idx], row->modified);
    }
}

static void shell_update_program_row(void)
{
    unsigned int i;
    char count_text[32];
    int loaded = shell_load_program_rows();
    snprintf(count_text, sizeof(count_text), "共 %u", g_program_row_count);
    if (g_program_count_label) {
        lv_label_set_text(g_program_count_label, count_text);
    }
    if (g_program_empty_label) {
        if (g_program_row_count == 0U) {
            lv_obj_clear_flag(g_program_empty_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_program_empty_label, "未找到可打开的G代码文件");
        } else {
            lv_obj_add_flag(g_program_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (i = 0U; i < V5_PROGRAM_ROWS_MAX; ++i) {
        shell_update_program_row_visual(i);
    }
    if (g_program_source_label) {
        if (!loaded) {
            lv_label_set_text(g_program_source_label, "来源: 本机 目录不可读");
        } else if (g_program_selected_index >= 0 && (unsigned int)g_program_selected_index < g_program_row_count) {
            char text[220];
            snprintf(text, sizeof(text), "已选择: %s  双击打开运行，或点打开修改", g_program_rows[g_program_selected_index].name);
            lv_label_set_text(g_program_source_label, text);
        } else {
            lv_label_set_text(g_program_source_label, "来源: 本机");
        }
    }
}

static void shell_clear_program_selection_confirm(void)
{
    g_program_confirm_selected_index = -1;
    g_program_confirm_selected_path[0] = '\0';
    g_program_last_click_index = -1;
    g_program_last_click_ns = 0ULL;
    g_program_last_click_path[0] = '\0';
}

static int shell_program_row_matches_explicit_selection(int idx)
{
    if (idx < 0 || idx != g_program_selected_index || idx != g_program_confirm_selected_index ||
        (unsigned int)idx >= g_program_row_count || !g_program_rows[idx].exists) {
        return 0;
    }
    return strcmp(g_program_confirm_selected_path, g_program_rows[idx].path) == 0;
}

static void shell_select_program_row(int idx)
{
    g_program_selected_index = idx;
    g_program_confirm_selected_index = idx;
    snprintf(g_program_confirm_selected_path, sizeof(g_program_confirm_selected_path), "%s", g_program_rows[idx].path);
    g_program_last_click_index = idx;
    g_program_last_click_ns = shell_monotonic_ns();
    snprintf(g_program_last_click_path, sizeof(g_program_last_click_path), "%s", g_program_rows[idx].path);
    shell_update_program_row();
    shell_log_program_event("program_file_select", g_program_rows[idx].path, 1, 0);
}

static void shell_open_program_row_for_run(int idx)
{
    V5ProgramOpenResult result;
    memset(&result, 0, sizeof(result));
    shell_clear_program_selection_confirm();
    if (v5_main_page_open_program(&g_main_page, g_program_rows[idx].path, &result)) {
        shell_log_program_event("program_file_double_click", g_program_rows[idx].path, 1, &result);
        (void)v5_main_page_apply_status(&g_main_page, &g_model.status_view);
        shell_log_program_event("program_file_double_click_applied", g_program_rows[idx].path, 1, &result);
        (void)v5_settings_page_apply_status(&g_settings_page, &g_model.status_view);
        g_main_cache_dirty = 1;
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MAIN);
    } else {
        shell_log_program_event("program_file_double_click", g_program_rows[idx].path, 0, &result);
        if (g_program_source_label) {
            lv_label_set_text(g_program_source_label, "打开失败");
        }
    }
}

static int shell_program_row_matches_recent_click(int idx)
{
    unsigned long long now;
    if (idx < 0 || idx != g_program_last_click_index || (unsigned int)idx >= g_program_row_count ||
        !g_program_rows[idx].exists || g_program_last_click_ns == 0ULL ||
        strcmp(g_program_last_click_path, g_program_rows[idx].path) != 0) {
        return 0;
    }
    now = shell_monotonic_ns();
    return now >= g_program_last_click_ns && now - g_program_last_click_ns <= V5_PROGRAM_DOUBLE_CLICK_NS;
}

static void shell_program_row_cb(lv_event_t *event)
{
    const int *index_ptr;
    int idx;
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    index_ptr = (const int *)lv_event_get_user_data(event);
    if (!index_ptr) {
        return;
    }
    idx = *index_ptr;
    if (idx < 0 || (unsigned int)idx >= g_program_row_count || !g_program_rows[idx].exists) {
        return;
    }
    if (shell_program_row_matches_explicit_selection(idx) || shell_program_row_matches_recent_click(idx)) {
        shell_open_program_row_for_run(idx);
        return;
    }
    shell_select_program_row(idx);
}

static void shell_bind_program_row_hit(lv_obj_t *obj, int *index_ptr)
{
    if (!obj || !index_ptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, shell_program_row_cb, LV_EVENT_CLICKED, index_ptr);
}

static void shell_bind_program_row_child_hit(lv_obj_t *obj, int *index_ptr)
{
    if (!obj || !index_ptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, shell_program_row_cb, LV_EVENT_CLICKED, index_ptr);
}

static void shell_clear_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *shell_make_label(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}


static lv_color_t shell_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

static lv_obj_t *shell_make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    shell_clear_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, shell_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, shell_rgb(33, 72, 98), 0);
    return panel;
}

static lv_obj_t *shell_text_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;
    shell_clear_style(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, shell_rgb(r, g, b), 0);
    lv_obj_set_style_bg_color(button, shell_rgb(245, 214, 82), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, shell_rgb(76, 119, 146), 0);
    if (cb) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, 0);
    }
    label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, 0, (h - 24) / 2);
    lv_obj_set_size(label, w, 24);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, (r + g + b > 540) ? shell_rgb(16, 20, 24) : shell_rgb(238, 245, 248), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return button;
}

static lv_obj_t *shell_table_label(lv_obj_t *parent, const char *text, int x, int y, int w, uint8_t r, uint8_t g, uint8_t b)
{
    return shell_make_label(parent, x, y, w, 22, text, shell_rgb(r, g, b), LV_TEXT_ALIGN_LEFT);
}

static void shell_format_program_size(off_t size, char *out, size_t out_cap)
{
    if (!out || out_cap == 0U) {
        return;
    }
    if (size < 1024) {
        snprintf(out, out_cap, "%lldB", (long long)size);
    } else if (size < 1024 * 1024) {
        snprintf(out, out_cap, "%.1fK", (double)size / 1024.0);
    } else {
        snprintf(out, out_cap, "%.1fM", (double)size / (1024.0 * 1024.0));
    }
}

static void shell_format_program_date(time_t when, char *out, size_t out_cap)
{
    struct tm local_time;
    if (!out || out_cap == 0U) {
        return;
    }
    if (when <= 0) {
        out[0] = '\0';
        return;
    }
    localtime_r(&when, &local_time);
    strftime(out, out_cap, "%m-%d", &local_time);
}

static void shell_refresh_program_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        shell_update_program_row();
    }
}

static void shell_mdi_clear_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        g_mdi_line[0] = '\0';
        shell_update_mdi_line();
    }
}

static void shell_append_mdi_text(const char *text)
{
    size_t used;
    size_t add;
    if (!text || !text[0]) {
        return;
    }
    used = strlen(g_mdi_line);
    add = strlen(text);
    if (used + add >= sizeof(g_mdi_line)) {
        return;
    }
    memcpy(g_mdi_line + used, text, add + 1U);
    shell_update_mdi_line();
}

static void shell_mdi_key_cb(lv_event_t *event)
{
    const char *key;
    size_t len;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    key = (const char *)lv_event_get_user_data(event);
    if (!key) {
        return;
    }
    len = strlen(g_mdi_line);
    if (strcmp(key, "BKSP") == 0) {
        if (len > 0U) {
            g_mdi_line[len - 1U] = '\0';
        }
    } else if (strcmp(key, "SP") == 0) {
        shell_append_mdi_text(" ");
        return;
    } else if (strcmp(key, "EOB") == 0) {
        shell_append_mdi_text(";");
        return;
    } else if (strcmp(key, "CAN") == 0) {
        g_mdi_line[0] = '\0';
    } else if (strcmp(key, "UP\nPAGE") == 0 || strcmp(key, "PAGE\nDN") == 0 || strcmp(key, "<") == 0 || strcmp(key, "^") == 0 || strcmp(key, "v") == 0 || strcmp(key, ">") == 0) {
        return;
    } else {
        shell_append_mdi_text(key);
        return;
    }
    shell_update_mdi_line();
}

static void shell_keyboard_key_label(lv_obj_t *key, const char *text, int w, int h)
{
    const char *newline = text ? strchr(text, '\n') : 0;
    if (!newline) {
        shell_make_label(key, 0, (h - 22) / 2, w, 24, text ? text : "", shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
    } else {
        char top[24];
        size_t n = (size_t)(newline - text);
        if (n >= sizeof(top)) {
            n = sizeof(top) - 1U;
        }
        memcpy(top, text, n);
        top[n] = '\0';
        shell_make_label(key, 0, (h - 42) / 2, w, 22, top, shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
        shell_make_label(key, 0, (h - 42) / 2 + 22, w, 22, newline + 1, shell_rgb(16, 20, 24), LV_TEXT_ALIGN_CENTER);
    }
}

static lv_obj_t *shell_keyboard_key(lv_obj_t *parent, int x, int y, int w, int h, const char *text, int pressed, int interactive)
{
    lv_obj_t *key = shell_make_panel(parent, x, y, w, h, pressed ? 247 : 244, pressed ? 221 : 242, pressed ? 187 : 237);
    lv_obj_set_style_border_color(key, shell_rgb(158, 165, 170), 0);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    shell_keyboard_key_label(key, text, w, h);
    if (interactive) {
        lv_obj_add_event_cb(key, shell_mdi_key_cb, LV_EVENT_CLICKED, (void *)text);
    }
    return key;
}

static void shell_create_keyboard_matrix(lv_obj_t *parent, int x, int y, int w, int h, const char *labels[], int rows, int cols, const char *special, int interactive)
{
    const int row_gap = 8;
    const int col_gap = 8;
    const int row_h = (h - row_gap * (rows - 1)) / rows;
    const int col_w = (w - col_gap * (cols - 1)) / cols;
    int r;
    int c;
    for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
            const char *label = labels[r * cols + c];
            shell_keyboard_key(parent, x + c * (col_w + col_gap), y + r * (row_h + row_gap), col_w, row_h, label, special && strcmp(label, special) == 0, interactive);
        }
    }
}

static void shell_create_cnc_keyboard_panel(lv_obj_t *parent, int x, int y, int interactive)
{
    static const char *letters[] = {
        "O", "N", "G", "P",
        "X", "Y", "Z", "Q",
        "I", "J", "K", "R",
        "M", "S", "T", "L",
        "F", "D", "H", "B",
    };
    static const char *numbers[] = {
        "7", "8", "9",
        "4", "5", "6",
        "1", "2", "3",
        "-", "0", ".",
        "/", "EOB", "CAN",
    };
    shell_create_keyboard_matrix(parent, x, y, 252, 314, letters, 5, 4, 0, interactive);
    shell_create_keyboard_matrix(parent, x + 274, y, 196, 314, numbers, 5, 3, "CAN", interactive);
    shell_keyboard_key(parent, x, y + 328, 64, 58, "UP\nPAGE", 1, interactive);
    shell_keyboard_key(parent, x, y + 402, 64, 58, "PAGE\nDN", 1, interactive);
    shell_keyboard_key(parent, x + 72, y + 388, 54, 54, "<", 0, interactive);
    shell_keyboard_key(parent, x + 134, y + 328, 54, 54, "^", 0, interactive);
    shell_keyboard_key(parent, x + 134, y + 436, 54, 42, "v", 0, interactive);
    shell_keyboard_key(parent, x + 196, y + 388, 54, 54, ">", 0, interactive);
    shell_keyboard_key(parent, x + 274, y + 328, 86, 54, "SP", 0, interactive);
    shell_keyboard_key(parent, x + 376, y + 328, 86, 54, "BKSP", 0, interactive);
}

static lv_obj_t *shell_create_program_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    shell_make_panel(root, 10, 10, 1004, 580, 9, 22, 34);
    shell_make_label(root, 30, 52, 48, 24, "搜索", shell_rgb(142, 162, 184), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 90, 38, 294, 40, 11, 24, 38);
    shell_make_label(root, 98, 52, 180, 24, "输入程序名", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 22, 102, 160, 24, "本机程序文件", shell_rgb(79, 168, 214), LV_TEXT_ALIGN_LEFT);
    g_program_count_label = shell_make_label(root, 253, 102, 80, 24, "共 0", shell_rgb(156, 178, 202), LV_TEXT_ALIGN_LEFT);

    shell_make_panel(root, 18, 126, 500, 344, 22, 37, 54);
    shell_make_panel(root, 19, 127, 498, 28, 16, 31, 48);
    shell_table_label(root, "名称 ^", 32, 132, 120, 199, 212, 226);
    shell_table_label(root, "大小", 218, 132, 60, 199, 212, 226);
    shell_table_label(root, "创建日期", 290, 132, 88, 199, 212, 226);
    shell_table_label(root, "修改日期", 402, 132, 88, 199, 212, 226);
    shell_make_panel(root, 212, 127, 1, 342, 46, 77, 105);
    shell_make_panel(root, 282, 127, 1, 342, 46, 77, 105);
    shell_make_panel(root, 396, 127, 1, 342, 46, 77, 105);

    g_program_empty_label = shell_make_label(root, 56, 174, 360, 24, "", shell_rgb(156, 178, 202), LV_TEXT_ALIGN_LEFT);
    g_program_source_label = shell_make_label(root, 22, 482, 470, 24, "来源: 本机", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    for (i = 0; i < (int)V5_PROGRAM_ROWS_MAX; ++i) {
        lv_obj_t *row;
        g_program_row_indices[i] = i;
        row = shell_make_panel(root, 20, 155 + i * V5_PROGRAM_ROW_HIT_H, 478, V5_PROGRAM_ROW_HIT_H, 17, 40, 58);
        g_program_row_layers[i] = row;
        shell_bind_program_row_hit(row, &g_program_row_indices[i]);
        shell_bind_program_row_child_hit(shell_make_panel(row, 14, 7, 12, 15, 205, 216, 228), &g_program_row_indices[i]);
        g_program_row_name_labels[i] = shell_make_label(row, 36, 6, 132, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_program_row_name_labels[i], &g_program_row_indices[i]);
        g_program_row_size_labels[i] = shell_make_label(row, 208, 6, 56, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_program_row_size_labels[i], &g_program_row_indices[i]);
        g_program_row_created_labels[i] = shell_make_label(row, 270, 6, 80, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_program_row_created_labels[i], &g_program_row_indices[i]);
        g_program_row_modified_labels[i] = shell_make_label(row, 382, 6, 80, 22, "", shell_rgb(205, 216, 228), LV_TEXT_ALIGN_LEFT);
        shell_bind_program_row_child_hit(g_program_row_modified_labels[i], &g_program_row_indices[i]);
    }
    shell_make_panel(root, 497, 162, 15, 300, 112, 132, 154);

    shell_create_cnc_keyboard_panel(root, 536, 26, 0);
    shell_text_button(root, "刷新", 392, 37, 118, 40, 20, 62, 91, shell_refresh_program_cb);
    shell_text_button(root, "本机", 18, 517, 110, 54, 43, 133, 83, shell_refresh_program_cb);
    shell_text_button(root, "删除", 136, 517, 110, 54, 199, 70, 46, 0);
    shell_text_button(root, "打开修改", 254, 517, 128, 54, 74, 91, 111, 0);
    shell_text_button(root, "返回", 390, 517, 128, 54, 20, 62, 91, shell_return_button_cb);

    shell_update_program_row();
    return root;
}

static lv_obj_t *shell_create_mdi_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(77, 85, 93), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 6, 7, 42, 22, "MDI", shell_rgb(245, 242, 232), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 56, 7, 120, 22, "MDI edit", shell_rgb(214, 209, 194), LV_TEXT_ALIGN_LEFT);

    shell_make_panel(root, 0, 28, 512, 548, 16, 24, 32);
    for (i = 0; i < 18; ++i) {
        char line_no[8];
        snprintf(line_no, sizeof(line_no), "%03d", i + 1);
        shell_make_label(root, 12, 37 + i * 28, 42, 18, line_no, shell_rgb(127, 138, 148), LV_TEXT_ALIGN_LEFT);
    }
    g_mdi_line_label = shell_make_label(root, 62, 38, 450, 492, "MDI>", shell_rgb(247, 250, 252), LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(g_mdi_line_label, LV_LABEL_LONG_WRAP);
    g_mdi_status_label = shell_make_label(root, 62, 536, 390, 24, "Native MDI", shell_rgb(150, 162, 174), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 62, 558, 390, 24, "FANUC-style block input", shell_rgb(150, 162, 174), LV_TEXT_ALIGN_LEFT);

    shell_create_cnc_keyboard_panel(root, 554, 28, 1);
    shell_text_button(root, "清空", 828, 418, 86, 54, 244, 242, 237, shell_mdi_clear_cb);
    shell_text_button(root, "发送", 930, 418, 86, 54, 216, 193, 160, shell_mdi_load_cb);
    shell_text_button(root, "保存", 708, 514, 98, 54, 244, 242, 237, 0);
    shell_text_button(root, "关闭", 920, 514, 98, 54, 244, 242, 237, shell_return_button_cb);
    shell_update_mdi_line();
    return root;
}

static lv_obj_t *shell_create_aux_page(lv_obj_t *screen, const char *title)
{
    lv_obj_t *root = lv_obj_create(screen);
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 30, 17, 160, 24, title, shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 28, 78, 560, 420, 7, 31, 48);
    shell_make_panel(root, 612, 78, 260, 220, 8, 36, 55);
    shell_text_button(root, "主页面", 920, 0, 104, 60, 41, 145, 107, shell_return_button_cb);
    return root;
}

static lv_obj_t *shell_create_network_page(lv_obj_t *screen)
{
    lv_obj_t *root = lv_obj_create(screen);
    int i;
    static const char *axes[] = {"X", "Y", "Z", "A", "C", "S", "备用7", "备用8"};
    shell_clear_style(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 1024, 600);
    lv_obj_set_style_bg_color(root, shell_rgb(4, 20, 31), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    shell_make_label(root, 30, 17, 220, 24, "网络 / 总线只读详情", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 270, 17, 520, 24, "P8 只读：不改网口、不扫写总线、不复位驱动", shell_rgb(155, 177, 198), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 28, 72, 452, 270, 7, 31, 48);
    shell_make_label(root, 52, 96, 360, 24, "PS eth0: SSH/调试/普通网络", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 132, 390, 24, "PS eth0 IP: --", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 168, 390, 24, "调试软件: http://--:8091", shell_rgb(245, 214, 82), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 204, 390, 24, "PL eth1: EtherCAT 专用，不 DHCP，不维护 IP", shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 240, 300, 24, "EtherCAT master: OP", shell_rgb(90, 230, 170), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 276, 260, 24, "Domain0: 10/10", shell_rgb(90, 230, 170), LV_TEXT_ALIGN_LEFT);
    shell_make_label(root, 52, 312, 390, 24, "5 drives: OP  0x603F=0  0x6041=0x1650/0x1637", shell_rgb(90, 230, 170), LV_TEXT_ALIGN_LEFT);
    shell_make_panel(root, 506, 72, 440, 360, 7, 31, 48);
    for (i = 0; i < 8; ++i) {
        int y = 98 + i * 38;
        shell_make_panel(root, 532, y, 364, 30, (i < 5) ? 8 : 38, (i < 5) ? 36 : 54, (i < 5) ? 55 : 65);
        if (i < 5) {
            char row[128];
            snprintf(row, sizeof(row), "%s轴  OP  fault=0  statusword=ready", axes[i]);
            shell_make_label(root, 548, y + 5, 320, 20, row, shell_rgb(226, 238, 246), LV_TEXT_ALIGN_LEFT);
        } else {
            shell_make_label(root, 548, y + 5, 320, 20, axes[i], shell_rgb(150, 170, 190), LV_TEXT_ALIGN_LEFT);
        }
    }
    shell_text_button(root, "主页面", 920, 0, 104, 60, 41, 145, 107, shell_return_button_cb);
    return root;
}

static V5ShellPageKind shell_page_for_action(V5MainPageActionKind action)
{
    switch (action) {
    case V5_MAIN_PAGE_ACTION_NAV_SETTINGS:
        return V5_SHELL_PAGE_SETTINGS;
    case V5_MAIN_PAGE_ACTION_NAV_TOOL:
        return V5_SHELL_PAGE_TOOL;
    case V5_MAIN_PAGE_ACTION_NAV_PROBE:
        return V5_SHELL_PAGE_PROBE;
    case V5_MAIN_PAGE_ACTION_NAV_OFFSET:
        return V5_SHELL_PAGE_OFFSET;
    case V5_MAIN_PAGE_ACTION_NAV_IO:
        return V5_SHELL_PAGE_IO;
    case V5_MAIN_PAGE_ACTION_NAV_NETWORK:
        return V5_SHELL_PAGE_NETWORK;
    case V5_MAIN_PAGE_ACTION_NAV_PROGRAM:
        return V5_SHELL_PAGE_PROGRAM;
    case V5_MAIN_PAGE_ACTION_NAV_MDI:
        return V5_SHELL_PAGE_MDI;
    case V5_MAIN_PAGE_ACTION_NAV_MAIN:
    default:
        return V5_SHELL_PAGE_MAIN;
    }
}

static const char *shell_page_name(V5ShellPageKind page)
{
    switch (page) {
    case V5_SHELL_PAGE_SETTINGS:
        return "settings";
    case V5_SHELL_PAGE_TOOL:
        return "tool";
    case V5_SHELL_PAGE_PROBE:
        return "probe";
    case V5_SHELL_PAGE_OFFSET:
        return "offset";
    case V5_SHELL_PAGE_IO:
        return "io";
    case V5_SHELL_PAGE_NETWORK:
        return "network";
    case V5_SHELL_PAGE_PROGRAM:
        return "program";
    case V5_SHELL_PAGE_MDI:
        return "mdi";
    case V5_SHELL_PAGE_MAIN:
    default:
        return "main";
    }
}

static void shell_hide_all_pages(void)
{
    unsigned int i;
    for (i = 0; i < (unsigned int)V5_SHELL_PAGE_COUNT; ++i) {
        if (g_shell_pages[i]) {
            lv_obj_add_flag(g_shell_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * REQ-UI-FIRST-FRAME-CACHE: this is the canonical path for all current
 * and future page switches. Navigation must show a cached/restored frame
 * within 0.2 s on the board. Any later page, popup, keyboard, network, tool,
 * probe, offset, IO, or similar full-screen surface must be pre-rendered into
 * a resident display cache at boot/canonical reload, or preserve the opening
 * frame before covering the page. cache_blit/frame restore is always the first
 * visible action; normal LVGL refresh may follow only for the dirty target
 * page, overlay, or changed cell.
 */
static void shell_navigate(void *user_data, V5MainPageActionKind action)
{
    unsigned long long t0;
    unsigned long long elapsed;
    int cache_ok = 0;
    V5ShellPageKind page;
    (void)user_data;
    if (!g_main_page.root || !g_settings_page.root) {
        return;
    }
    t0 = shell_monotonic_ns();
    page = shell_page_for_action(action);
    if (page == V5_SHELL_PAGE_MDI) {
        g_mdi_line[0] = '\0';
        shell_update_mdi_line();
    } else if (page == V5_SHELL_PAGE_PROGRAM) {
        shell_update_program_row();
    } else if (page == V5_SHELL_PAGE_SETTINGS) {
        v5_settings_page_reset_return_state(&g_settings_page);
    }
    shell_hide_all_pages();
    if (g_shell_pages[page]) {
        lv_obj_clear_flag(g_shell_pages[page], LV_OBJ_FLAG_HIDDEN);
    }
    if (g_top_status_layer) {
        if (page == V5_SHELL_PAGE_MAIN) {
            lv_obj_clear_flag(g_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (page == V5_SHELL_PAGE_MAIN) {
        if (g_main_cache_dirty) {
            lv_timer_handler();
            v5_lvgl_remote_display_render_now();
            cache_ok = v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_MAIN);
            g_main_cache_dirty = 0;
        } else {
            cache_ok = v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_MAIN);
        }
    } else if (page == V5_SHELL_PAGE_SETTINGS) {
        cache_ok = v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_SETTINGS);
    }
    if (!cache_ok) {
        v5_lvgl_remote_display_render_now();
    }
    g_current_page = page;
    elapsed = shell_monotonic_ns() - t0;
    log_navigation_perf(shell_page_name(page), cache_ok, elapsed);
}

static void fill_report(V5ShellBootReport *report, const V5BootClosure *closure, int status_refresh_ok, int main_page_created, int main_page_applied)
{
    if (!report || !closure) {
        return;
    }
    report->boot_closure_abi = g_model.boot_closure_abi;
    report->command_count = g_model.command_count;
    report->drive_profile_count = g_model.drive_profile_count;
    report->drive_profile_map_count = g_model.drive_profile_map_count;
    report->parameter_owner_count = g_model.parameter_owner_count;
    report->microkernel_manifest_count = (unsigned int)closure->microkernel_manifest_count;
    report->native_gate_count = (unsigned int)closure->native_gate_count;
    report->native_readback_count = (unsigned int)closure->native_readback_count;
    report->resource_count = g_model.resource_count;
    report->status_refresh_ok = (unsigned int)status_refresh_ok;
    report->status_valid_mask = g_model.status_view.valid_mask;
    report->status_frame_flags = g_model.status_view.frame_flags;
    report->main_page_created = (unsigned int)main_page_created;
    report->main_page_applied = (unsigned int)main_page_applied;
}

static int v5_ui_shell_bootstrap_common(V5ShellBootReport *report, const char *project_root, int remote_display)
{
    V5BootClosure closure;
    lv_obj_t *screen;
    int status_refresh_ok;
    int main_page_created;
    int settings_page_created;
    int main_page_applied;

    v5_ui_model_init(&g_model);
    snprintf(g_project_root, sizeof(g_project_root), "%s", (project_root && project_root[0]) ? project_root : ".");
    v5_boot_closure_load(&closure, project_root);

    lv_init();
    if (remote_display) {
        if (!v5_lvgl_remote_display_setup(1024U, 600U)) {
            return 1;
        }
        (void)v5_lvgl_remote_input_setup();
        (void)v5_lvgl_touch_input_setup();
    } else if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }

    screen = lv_scr_act();
    g_model.lvgl_initialized = 1;
    g_model.boot_closure_abi = closure.abi_version;
    g_model.command_count = (unsigned int)closure.command_count;
    g_model.drive_profile_count = (unsigned int)closure.drive_profile_count;
    g_model.drive_profile_map_count = (unsigned int)closure.drive_profile_map_count;
    g_model.parameter_owner_count = (unsigned int)closure.parameter_owner_count;
    g_model.resource_count = (unsigned int)closure.resource_count;
    status_refresh_ok = v5_ui_model_refresh_status_from_shm(&g_model, V5_STATUS_SHM_PATH);
    v5_program_controller_init(&g_program_controller);
    v5_settings_page_set_boot_closure(&closure);
    v5_settings_axis_table_load_boot_closure(&closure);
    main_page_created = v5_main_page_create(&g_main_page, screen);
    settings_page_created = v5_settings_page_create(&g_settings_page, screen);
    if (main_page_created) {
        g_shell_pages[V5_SHELL_PAGE_MAIN] = g_main_page.root;
        v5_lvgl_touch_input_set_points_callback(shell_toolpath_touch_points, 0);
    }
    if (settings_page_created) {
        g_shell_pages[V5_SHELL_PAGE_SETTINGS] = g_settings_page.root;
    }
    g_shell_pages[V5_SHELL_PAGE_TOOL] = v5_v3_local_page_create_tool(screen, shell_return_button_cb);
    g_shell_pages[V5_SHELL_PAGE_PROBE] = shell_create_aux_page(screen, "探测");
    g_shell_pages[V5_SHELL_PAGE_OFFSET] = v5_v3_local_page_create_offset(screen, shell_return_button_cb);
    g_shell_pages[V5_SHELL_PAGE_IO] = shell_create_aux_page(screen, "输入输出设置");
    g_shell_pages[V5_SHELL_PAGE_NETWORK] = shell_create_network_page(screen);
    g_shell_pages[V5_SHELL_PAGE_PROGRAM] = shell_create_program_page(screen);
    g_shell_pages[V5_SHELL_PAGE_MDI] = shell_create_mdi_page(screen);
    shell_create_top_status_layer(screen);
    if (main_page_created) {
        v5_main_page_bind_program_controller(&g_main_page, &g_program_controller);
        v5_main_page_set_command_execution_enabled(&g_main_page, 1);
        v5_main_page_set_navigation_callback(&g_main_page, shell_navigate, 0);
        v5_main_page_set_native_readback_refresh_callback(&g_main_page, shell_refresh_native_readback_for_action, 0);
        (void)shell_refresh_native_readback(1);
        (void)shell_refresh_safety_readback(1);
    }
    if (settings_page_created) {
        v5_settings_page_set_navigation_callback(&g_settings_page, shell_navigate, 0);
    }
    shell_hide_all_pages();
    if (g_shell_pages[V5_SHELL_PAGE_MAIN]) {
        lv_obj_clear_flag(g_shell_pages[V5_SHELL_PAGE_MAIN], LV_OBJ_FLAG_HIDDEN);
    }
    g_current_page = V5_SHELL_PAGE_MAIN;
    if (g_top_status_layer) {
        lv_obj_clear_flag(g_top_status_layer, LV_OBJ_FLAG_HIDDEN);
    }
    main_page_applied = main_page_created ? v5_main_page_apply_status(&g_main_page, &g_model.status_view) : 0;
    if (settings_page_created) {
        (void)v5_settings_page_apply_status(&g_settings_page, &g_model.status_view);
    }
    lv_timer_handler();
    (void)v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_MAIN);
    if (settings_page_created && main_page_created) {
        shell_hide_all_pages();
        lv_obj_clear_flag(g_shell_pages[V5_SHELL_PAGE_SETTINGS], LV_OBJ_FLAG_HIDDEN);
        if (g_top_status_layer) {
            lv_obj_add_flag(g_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_timer_handler();
        (void)v5_lvgl_remote_display_cache_capture(V5_REMOTE_DISPLAY_CACHE_SETTINGS);
        shell_hide_all_pages();
        lv_obj_clear_flag(g_shell_pages[V5_SHELL_PAGE_MAIN], LV_OBJ_FLAG_HIDDEN);
        if (g_top_status_layer) {
            lv_obj_clear_flag(g_top_status_layer, LV_OBJ_FLAG_HIDDEN);
        }
        (void)v5_lvgl_remote_display_cache_blit(V5_REMOTE_DISPLAY_CACHE_MAIN);
    }
    fill_report(report, &closure, status_refresh_ok, main_page_created, main_page_applied);
    g_ui_ready = g_model.lvgl_initialized;
    return g_ui_ready ? 0 : 1;
}

int v5_ui_shell_bootstrap(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 0);
}

int v5_ui_shell_bootstrap_remote(V5ShellBootReport *report, const char *project_root)
{
    return v5_ui_shell_bootstrap_common(report, project_root, 1);
}

static int shell_refresh_due(unsigned long long now, unsigned long long *last, unsigned long long period_ns)
{
    if (!last || period_ns == 0ULL) {
        return 0;
    }
    if (*last == 0ULL || now < *last || now - *last >= period_ns) {
        *last = now;
        return 1;
    }
    return 0;
}

int v5_ui_shell_refresh_once(void)
{
    unsigned long long now;
    unsigned int flags = 0U;

    if (!g_ui_ready || !g_main_page.root) {
        return 0;
    }
    now = shell_monotonic_ns();
    if (shell_refresh_due(now, &g_ui_dynamic_last_refresh_ns, V5_UI_DYNAMIC_REFRESH_NS)) {
        (void)v5_ui_model_refresh_status_from_shm(&g_model, V5_STATUS_SHM_PATH);
        flags |= V5_MAIN_PAGE_REFRESH_DYNAMIC;
    }
    if (shell_refresh_due(now, &g_ui_estop_last_refresh_ns, V5_UI_ESTOP_REFRESH_NS)) {
        (void)shell_refresh_safety_readback(0);
        flags |= V5_MAIN_PAGE_REFRESH_ESTOP;
    }
    if (shell_refresh_due(now, &g_ui_button_last_refresh_ns, V5_UI_BUTTON_REFRESH_NS)) {
        flags |= V5_MAIN_PAGE_REFRESH_BUTTONS;
    }
    if (shell_refresh_due(now, &g_ui_slow_last_refresh_ns, V5_UI_SLOW_REFRESH_NS)) {
        (void)shell_refresh_native_readback(0);
        flags |= V5_MAIN_PAGE_REFRESH_SLOW;
    }
    if (flags != 0U) {
        (void)v5_main_page_apply_status_flags(&g_main_page, &g_model.status_view, flags);
        if ((flags & V5_MAIN_PAGE_REFRESH_DYNAMIC) != 0U && g_current_page == V5_SHELL_PAGE_SETTINGS) {
            (void)v5_settings_page_apply_status(&g_settings_page, &g_model.status_view);
        }
        lv_timer_handler();
    }
    return 1;
}

#ifdef V5_UI_SHELL_TEST_HOOKS
int v5_ui_shell_test_program_selected_index(void)
{
    return g_program_selected_index;
}

int v5_ui_shell_test_program_loaded(void)
{
    const V5ProgramRuntime *runtime = v5_program_controller_runtime(&g_program_controller);
    return v5_program_runtime_has_open_program(runtime);
}

const char *v5_ui_shell_test_program_loaded_path(void)
{
    const V5ProgramRuntime *runtime = v5_program_controller_runtime(&g_program_controller);
    return v5_program_runtime_source_path(runtime);
}

int v5_ui_shell_test_click_program_name(unsigned int idx)
{
    if (idx >= V5_PROGRAM_ROWS_MAX || !g_program_row_name_labels[idx]) {
        return 0;
    }
    lv_event_send(g_program_row_name_labels[idx], LV_EVENT_CLICKED, 0);
    lv_timer_handler();
    return 1;
}
#endif
