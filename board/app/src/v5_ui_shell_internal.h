#ifndef V5_UI_SHELL_INTERNAL_H
#define V5_UI_SHELL_INTERNAL_H

#include "v5_app.h"
#include "v5_main_page.h"
#include "v5_native_operator_error_status.h"
#include "v5_settings_page.h"
#include "v5_ui_model.h"

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define V5_PROGRAM_ROWS_MAX 9U
#define V5_PROGRAM_SCAN_MAX 256U
#define V5_PROGRAM_ROW_HIT_H 32
#define V5_PROGRAM_DOUBLE_CLICK_NS 800000000ULL
#define V5_MDI_TEXT_CAP 8192U
#define V5_UI_CACHE_BOOT_WORKER_ID 0U

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

struct V5UiPageCacheQueueEvidence;

typedef struct V5ProgramRow {
    char name[168];
    char size[20];
    char created[20];
    char modified[20];
    char path[384];
    int exists;
} V5ProgramRow;

#define V5_UI_DYNAMIC_REFRESH_NS 33333333ULL
#define V5_UI_BUTTON_REFRESH_NS 100000000ULL
#define V5_UI_ESTOP_REFRESH_NS 100000000ULL
#define V5_UI_SLOW_REFRESH_NS 200000000ULL
#define V5_NATIVE_READBACK_MIN_NS 200000000ULL
#define V5_MODAL_LINE_READBACK_MIN_NS 33333333ULL
#define V5_SAFETY_READBACK_MIN_NS 100000000ULL
#define V5_SAFETY_READBACK_TIMEOUT_MS 80U
#define V5_OPERATOR_ERROR_READ_MIN_NS 100000000ULL
#define V5_OPERATOR_ERROR_SHOW_NS 6000000000ULL

extern V5MainPage g_v5_shell_main_page;
extern V5SettingsPage g_v5_shell_settings_page;
extern V5ProgramController g_v5_shell_program_controller;
extern V5UiModel g_v5_shell_model;
extern lv_obj_t *g_v5_shell_shell_pages[V5_SHELL_PAGE_COUNT];
extern lv_obj_t *g_v5_shell_program_count_label;
extern lv_obj_t *g_v5_shell_program_empty_label;
extern lv_obj_t *g_v5_shell_program_source_label;
extern lv_obj_t *g_v5_shell_program_row_layers[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_program_row_name_labels[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_program_row_size_labels[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_program_row_created_labels[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_program_row_modified_labels[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_program_edit_button;
extern int g_v5_shell_program_row_indices[V5_PROGRAM_ROWS_MAX];
extern lv_obj_t *g_v5_shell_mdi_line_label;
extern lv_obj_t *g_v5_shell_mdi_status_label;
extern char g_v5_shell_project_root[256];
extern V5ProgramRow g_v5_shell_program_rows[V5_PROGRAM_ROWS_MAX];
extern V5ProgramRow g_v5_shell_program_scan_rows[V5_PROGRAM_SCAN_MAX];
extern unsigned int g_v5_shell_program_row_count;
extern int g_v5_shell_program_selected_index;
extern int g_v5_shell_program_confirm_selected_index;
extern char g_v5_shell_program_confirm_selected_path[384];
extern int g_v5_shell_program_last_click_index;
extern unsigned long long g_v5_shell_program_last_click_ns;
extern char g_v5_shell_program_last_click_path[384];
extern char g_v5_shell_mdi_line[V5_MDI_TEXT_CAP];
extern char g_v5_shell_mdi_edit_program_name[168];
extern char g_v5_shell_mdi_edit_program_path[384];
extern int g_v5_shell_mdi_edit_prepared;
extern int g_v5_shell_ui_ready;
extern int g_v5_shell_page_cache_dirty[V5_SHELL_PAGE_COUNT];
extern int g_v5_shell_remote_display_active;
extern V5ShellPageKind g_v5_shell_current_page;
extern lv_obj_t *g_v5_shell_top_status_layer;
extern lv_obj_t *g_v5_shell_top_status_label;
extern V5NativeOperatorErrorStatus g_v5_shell_operator_error_status;
extern uint64_t g_v5_shell_operator_error_generation_seen;
extern unsigned long long g_v5_shell_operator_error_show_until_ns;
extern unsigned long long g_v5_shell_native_readback_last_probe_ns;
extern unsigned long long g_v5_shell_modal_line_readback_last_probe_ns;
extern unsigned long long g_v5_shell_safety_readback_last_probe_ns;
extern unsigned long long g_v5_shell_operator_error_last_probe_ns;
extern unsigned long long g_v5_shell_ui_dynamic_last_refresh_ns;
extern unsigned long long g_v5_shell_ui_button_last_refresh_ns;
extern unsigned long long g_v5_shell_ui_estop_last_refresh_ns;
extern unsigned long long g_v5_shell_ui_slow_last_refresh_ns;

void shell_navigate(void *user_data, V5MainPageActionKind action);
int shell_process_pending_navigation(void);
const char *shell_page_name(V5ShellPageKind page);
unsigned int shell_page_cache_slot(V5ShellPageKind page);
void shell_show_page_objects(V5ShellPageKind page);
int shell_show_page_objects_for_cache_blit(V5ShellPageKind page);
int shell_apply_page_resident_model(V5ShellPageKind page);
int shell_prepare_page_cache(V5ShellPageKind page);
int shell_boot_page_cache_registry_validate(void);
int shell_run_boot_page_cache_queue(
    lv_obj_t *screen,
    int remote_display,
    struct V5UiPageCacheQueueEvidence *evidence,
    unsigned long long *peak_cpu_pct_x100);
void shell_mark_page_cache_dirty(V5ShellPageKind page);
void shell_mark_all_page_caches_dirty(void);
int shell_sync_current_page_cache_if_dirty(void);

void shell_clear_style(lv_obj_t *obj);

void shell_update_program_row(void);

void shell_format_program_size(off_t size, char *out, size_t out_cap);

void shell_format_program_date(time_t when, char *out, size_t out_cap);

lv_color_t shell_rgb(uint8_t r, uint8_t g, uint8_t b);

unsigned long long shell_monotonic_ns(void);

int shell_toolpath_touch_points(const lv_point_t *points, int count, int pressed, int *changed, void *user_data);

void shell_update_top_status_label(void);

void shell_clear_mdi_edit_metadata(void);

int shell_load_current_program_for_mdi_edit(void);

int shell_refresh_operator_error(int force);

int shell_refresh_modal_line_readback(int force);

int shell_refresh_native_readback(int force);

int shell_refresh_safety_readback(int force);

void shell_refresh_native_readback_for_action(void *user_data, V5MainPageActionKind action);

void shell_log_navigation_perf(const char *target, int cache_ok, unsigned long long elapsed_ns);

void shell_create_top_status_layer(lv_obj_t *screen);

void shell_create_operator_error_popup(lv_obj_t *screen);

void shell_show_operator_error_popup(const V5NativeOperatorErrorStatus *status);

void shell_hide_operator_error_popup(void);

int shell_operator_error_popup_visible(void);

void shell_raise_operator_error_popup(void);

const char *shell_operator_error_popup_text(void);

lv_obj_t *shell_operator_error_popup_confirm_button(void);

void shell_return_button_cb(lv_event_t *event);

void shell_log_program_event(const char *event, const char *path, int ok, const V5ProgramOpenResult *result);

void shell_log_mdi_event(const char *event, const char *line, int ok);

void shell_update_mdi_line(void);

void shell_mdi_load_cb(lv_event_t *event);

int shell_program_path_allowed(const char *path);

int shell_load_mdi_edit_text(
    const char *program_name,
    const char *path,
    const char *text,
    size_t size,
    int editable_file,
    const char *event_name);

int shell_load_program_rows(void);

void shell_program_edit_cb(lv_event_t *event);

void shell_bind_program_row_hit(lv_obj_t *obj, int *index_ptr);

void shell_bind_program_row_child_hit(lv_obj_t *obj, int *index_ptr);

lv_obj_t *shell_create_program_page(lv_obj_t *screen);

lv_obj_t *shell_create_mdi_page(lv_obj_t *screen);

lv_obj_t *shell_create_aux_page(lv_obj_t *screen, const char *title);

lv_obj_t *shell_create_network_page(lv_obj_t *screen);

#endif
