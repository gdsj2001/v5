#include "v5_app.h"

#include "lvgl.h"
#include "v5_button_visuals.h"
#include "v5_lvgl_headless.h"
#include "v5_lvgl_remote_display.h"
#include "v5_lvgl_remote_input.h"
#include "v5_lvgl_touch_input.h"
#include "v5_boot_closure.h"
#include "v5_main_page.h"
#include "v5_main_page_home_transaction.h"
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

unsigned long long shell_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000000000ULL) + (unsigned long long)ts.tv_nsec;
}

void shell_log_navigation_perf(const char *target, int cache_ok, unsigned long long elapsed_ns)
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

void shell_create_top_status_layer(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    g_v5_shell_top_status_layer = lv_obj_create(screen);
    shell_clear_style(g_v5_shell_top_status_layer);
    lv_obj_set_pos(g_v5_shell_top_status_layer, 270, 10);
    lv_obj_set_size(g_v5_shell_top_status_layer, 500, 34);
    lv_obj_set_style_bg_opa(g_v5_shell_top_status_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_v5_shell_top_status_layer, 0, 0);
    lv_obj_set_style_radius(g_v5_shell_top_status_layer, 0, 0);
    lv_obj_set_style_pad_all(g_v5_shell_top_status_layer, 0, 0);
    lv_obj_clear_flag(g_v5_shell_top_status_layer, LV_OBJ_FLAG_SCROLLABLE);

    g_v5_shell_top_status_label = lv_label_create(g_v5_shell_top_status_layer);
    lv_obj_set_pos(g_v5_shell_top_status_label, 0, 5);
    lv_obj_set_size(g_v5_shell_top_status_label, 500, 24);
    lv_obj_set_style_text_align(g_v5_shell_top_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_v5_shell_top_status_label, lv_color_make(255, 86, 86), 0);
    lv_label_set_long_mode(g_v5_shell_top_status_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(g_v5_shell_top_status_label, "未回零: 开机后需回零一次");
}

static int shell_set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *safe = text ? text : "";
    const char *current;
    if (!label) {
        return 0;
    }
    current = lv_label_get_text(label);
    if (!current || strcmp(current, safe) != 0) {
        lv_label_set_text(label, safe);
        return 1;
    }
    return 0;
}

static void shell_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color, uint32_t selector)
{
    if (obj && lv_color_to32(lv_obj_get_style_text_color(obj, selector)) != lv_color_to32(color)) {
        lv_obj_set_style_text_color(obj, color, selector);
    }
}

void shell_update_top_status_label(void)
{
    unsigned long long now;
    int changed = 0;
    V5CommandGateHomeStatus home_status;
    char home_text[128];
    if (!g_v5_shell_top_status_label) {
        return;
    }
    shell_set_text_color_if_changed(g_v5_shell_top_status_label, lv_color_make(255, 86, 86), 0);
    /* Fixed priority P0: fresh native E-stop actual always wins. */
    if (v5_native_readback_safety_estop_known(&g_v5_shell_main_page.native_readback) &&
        g_v5_shell_main_page.native_readback.safety_estop_active) {
        changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, "急停中");
        if (changed) shell_mark_all_page_caches_dirty();
        return;
    }
    /* P1: current Home/positioning transaction; P2: fresh operator error. */
    if (v5_main_page_home_transaction_status(&home_status) &&
        v5_main_page_home_transaction_format_status_cn(&home_status, home_text, sizeof(home_text))) {
        changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, home_text);
        if (changed) shell_mark_all_page_caches_dirty();
        return;
    }
    now = shell_monotonic_ns();
    if (g_v5_shell_operator_error_status.display_mode ==
            V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS &&
        g_v5_shell_operator_error_show_until_ns != 0ULL &&
        now < g_v5_shell_operator_error_show_until_ns &&
        g_v5_shell_operator_error_status.reason_cn[0]) {
        changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, g_v5_shell_operator_error_status.reason_cn);
        if (changed) shell_mark_all_page_caches_dirty();
        return;
    }
    if (!v5_native_readback_all_homed_known(&g_v5_shell_main_page.native_readback)) {
        changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, "回零状态未知");
        if (changed) shell_mark_all_page_caches_dirty();
        return;
    }
    if (g_v5_shell_main_page.native_readback.all_homed) {
        changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, "");
        if (changed) shell_mark_all_page_caches_dirty();
        return;
    }
    changed = shell_set_label_text_if_changed(g_v5_shell_top_status_label, "未回零: 开机后需回零一次");
    if (changed) shell_mark_all_page_caches_dirty();
}

void shell_return_button_cb(lv_event_t *event)
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

void shell_log_program_event(const char *event, const char *path, int ok, const V5ProgramOpenResult *result)
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
                g_v5_shell_main_page.trajectory_point_count,
                g_v5_shell_main_page.toolpath_line_rewrite_count,
                g_v5_shell_main_page.toolpath_static_cache_hits,
                g_v5_shell_main_page.toolpath_static_cache_misses);
        {
            unsigned int i;
            int min_x = 0;
            int max_x = 0;
            int min_y = 0;
            int max_y = 0;
            if (g_v5_shell_main_page.trajectory_point_count > 0U) {
                min_x = max_x = g_v5_shell_main_page.trajectory_points[0].x;
                min_y = max_y = g_v5_shell_main_page.trajectory_points[0].y;
                for (i = 1U; i < g_v5_shell_main_page.trajectory_point_count; ++i) {
                    if (g_v5_shell_main_page.trajectory_points[i].x < min_x) min_x = g_v5_shell_main_page.trajectory_points[i].x;
                    if (g_v5_shell_main_page.trajectory_points[i].x > max_x) max_x = g_v5_shell_main_page.trajectory_points[i].x;
                    if (g_v5_shell_main_page.trajectory_points[i].y < min_y) min_y = g_v5_shell_main_page.trajectory_points[i].y;
                    if (g_v5_shell_main_page.trajectory_points[i].y > max_y) max_y = g_v5_shell_main_page.trajectory_points[i].y;
                }
            }
            fprintf(fp,
                    ",\"program_wcs_valid\":%s,\"program_wcs_index\":%d,\"program_wcs_offset_x\":%.6f,\"program_wcs_offset_y\":%.6f,\"program_wcs_offset_z\":%.6f,\"toolpath_hidden\":%s,\"toolpath_min_x\":%d,\"toolpath_min_y\":%d,\"toolpath_max_x\":%d,\"toolpath_max_y\":%d",
                    g_v5_shell_main_page.toolpath_program_wcs_valid ? "true" : "false",
                    g_v5_shell_main_page.toolpath_program_wcs_index,
                    g_v5_shell_main_page.toolpath_program_wcs_offset[0],
                    g_v5_shell_main_page.toolpath_program_wcs_offset[1],
                    g_v5_shell_main_page.toolpath_program_wcs_offset[2],
                    lv_obj_has_flag(g_v5_shell_main_page.trajectory_line, LV_OBJ_FLAG_HIDDEN) ? "true" : "false",
                    min_x,
                    min_y,
                    max_x,
                    max_y);
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
}

void shell_log_mdi_event(const char *event, const char *line, int ok)
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

void shell_update_mdi_line(void)
{
    if (g_v5_shell_mdi_line_label) {
        lv_label_set_text(g_v5_shell_mdi_line_label, g_v5_shell_mdi_line[0] ? g_v5_shell_mdi_line : "MDI>");
    }
    if (g_v5_shell_mdi_status_label) {
        if (g_v5_shell_mdi_edit_program_path[0]) {
            char status[220];
            snprintf(status, sizeof(status), "修改区: %s", g_v5_shell_mdi_edit_program_name[0] ? g_v5_shell_mdi_edit_program_name : "G-code");
            lv_label_set_text(g_v5_shell_mdi_status_label, status);
        } else {
            lv_label_set_text(g_v5_shell_mdi_status_label, "Native MDI");
        }
    }
}

void shell_mdi_load_cb(lv_event_t *event)
{
    int ok;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    ok = v5_main_page_set_mdi_text(&g_v5_shell_main_page, g_v5_shell_mdi_line);
    shell_log_mdi_event("load", g_v5_shell_mdi_line, ok);
    if (ok) {
        shell_clear_mdi_edit_metadata();
        shell_mark_page_cache_dirty(V5_SHELL_PAGE_MAIN);
        shell_navigate(0, V5_MAIN_PAGE_ACTION_NAV_MAIN);
    } else if (g_v5_shell_mdi_status_label) {
        lv_label_set_text(g_v5_shell_mdi_status_label, "载入失败");
    }
}
