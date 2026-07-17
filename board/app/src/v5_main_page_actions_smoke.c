#include "v5_main_page.h"
#include "v5_motion_model_registry.h"
#include "v5_main_page_internal.h"
#include "v5_button_visuals.h"
#include "v5_command_gate_ipc.h"
#include "v5_lvgl_headless.h"
#include "v5_main_page_home_transaction.h"
#include "v5_native_home.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static volatile int g_home_hook_terminal;
static volatile unsigned int g_home_send_hook_calls;
static volatile unsigned int g_home_send_delay_ms = 25U;
static volatile unsigned int g_home_progress_terminal_phase;
static volatile int g_home_send_hook_returned;

static void smoke_sleep_us(unsigned int delay_us)
{
#ifdef _WIN32
    Sleep((delay_us + 999U) / 1000U);
#else
    usleep(delay_us);
#endif
}

static unsigned long long smoke_now_ms(void)
{
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ((unsigned long long)now.tv_sec * 1000ULL) + (unsigned long long)(now.tv_nsec / 1000000ULL);
#endif
}

static int home_send_hook(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms)
{
    int all_home;
    int single_axis;
    (void)timeout_ms;
    ++g_home_send_hook_calls;
    if (!prepared || !request || !result) {
        return 0;
    }
    all_home = request->kind == V5_COMMAND_HOME &&
               strcmp(prepared->owner, "native_home_mode_gate") == 0 &&
               request->index_value == 0 && request->enabled_value == 0 &&
               request->axis_value == 0.0 && request->increment_value == 0.0 &&
               request->axis_mask == 0U && !request->text_value &&
               !request->secondary_text_value && !request->mode_value;
    single_axis = request->kind == V5_COMMAND_AXIS_ZERO_POSITION &&
                  strcmp(prepared->owner, "native_axis_zero_position") == 0 &&
                  request->text_value && request->text_value[0] && !request->text_value[1] &&
                  request->mode_value &&
                  (strcmp(request->mode_value, "mcs") == 0 || strcmp(request->mode_value, "wcs") == 0);
    if (!all_home && !single_axis) {
        return 0;
    }
    g_home_send_hook_returned = 0;
    smoke_sleep_us(g_home_send_delay_ms * 1000U);
    v5_command_gate_result_init(result);
    result->send_status = V5_COMMAND_GATE_SEND_SENT;
    result->executed = 1;
    snprintf(result->readback_code, sizeof(result->readback_code), "%s",
             all_home ? "ALL_HOME_CONFIRMED" : "AXIS_ZERO_POSITION_CONFIRMED");
    g_home_hook_terminal = 1;
    g_home_send_hook_returned = 1;
    return 1;
}

static int home_progress_hook(
    unsigned long long run_id,
    unsigned int generation,
    V5CommandGateHomeStatus *status,
    unsigned int timeout_ms)
{
    (void)timeout_ms;
    memset(status, 0, sizeof(*status));
    status->run_id = run_id;
    status->generation = generation;
    if (g_home_progress_terminal_phase != 0U) {
        status->phase = g_home_progress_terminal_phase;
        status->active = 0;
        status->terminal = 1;
        status->cancelled = g_home_progress_terminal_phase == V5_NATIVE_HOME_PHASE_CANCELLED;
        snprintf(status->mode, sizeof(status->mode), "%s", "all");
        snprintf(status->current_axes, sizeof(status->current_axes), "%s", "C");
        snprintf(status->direct_reason, sizeof(status->direct_reason), "%s",
                 status->cancelled ? "HOME_CANCELLED" : "ALL_HOME_NATIVE_CONFIG_INVALID");
        return 1;
    }
    status->phase = g_home_hook_terminal ? V5_NATIVE_HOME_PHASE_COMPLETE : V5_NATIVE_HOME_PHASE_NATIVE_HOME;
    status->active = g_home_hook_terminal ? 0 : 1;
    status->terminal = g_home_hook_terminal ? 1 : 0;
    snprintf(status->mode, sizeof(status->mode), "%s", "all");
    snprintf(status->current_axes, sizeof(status->current_axes), "%s", "ALL");
    snprintf(status->direct_reason, sizeof(status->direct_reason), "%s",
             g_home_hook_terminal ? "ALL_HOME_CONFIRMED" : "ALL_HOME_ACCEPTED");
    return 1;
}

static int wait_home_transaction(V5MainPage *page)
{
    unsigned int attempt;
    for (attempt = 0U; attempt < 500U; ++attempt) {
        if (v5_main_page_home_transaction_poll(page)) return 1;
#ifdef _WIN32
        Sleep(1U);
#else
        smoke_sleep_us(1000U);
#endif
    }
    return 0;
}

static int same_text(const char *left, const char *right)
{
    return left && right && strcmp(left, right) == 0;
}

static void capture_nav_action(void *user_data, V5MainPageActionKind action)
{
    V5MainPageActionKind *out = (V5MainPageActionKind *)user_data;
    if (out) {
        *out = action;
    }
}

static int expect_local(V5MainPage *page, V5MainPageActionKind action, const char *name)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || !report.local_only) {
        return 0;
    }
    if (report.request.kind != V5_COMMAND_UI_LOCAL || report.command.kind != V5_COMMAND_UI_LOCAL) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, "ui_local") && report.command.accepted;
}

static int expect_command_line(V5MainPage *page, V5MainPageActionKind action, const char *name, const char *owner, const char *line)
{
    V5MainPageActionReport report;
    (void)line;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (report.command_line[0]) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, owner) && report.command.accepted;
}

static int expect_command(V5MainPage *page, V5MainPageActionKind action, const char *name, const char *owner)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (report.command_line[0]) {
        return 0;
    }
    return same_text(report.command.name, name) && same_text(report.command.owner, owner) && report.command.accepted;
}

static int expect_override(
    V5MainPage *page,
    int spindle,
    int percent,
    const char *name)
{
    V5MainPageActionReport report;
    V5MainPageActionKind expected_action = spindle
        ? V5_MAIN_PAGE_ACTION_SPINDLE_OVERRIDE_SET
        : V5_MAIN_PAGE_ACTION_FEED_OVERRIDE_SET;
    V5CommandKind expected_kind = spindle
        ? V5_COMMAND_SPINDLE_OVERRIDE_SET
        : V5_COMMAND_FEED_OVERRIDE_SET;
    if (!v5_main_page_trigger_override(page, spindle, percent, &report)) {
        return 0;
    }
    return report.action == expected_action &&
           report.prepared && !report.local_only && !report.executed &&
           report.request.kind == expected_kind &&
           report.request.index_value == percent &&
           same_text(report.command.name, name) &&
           same_text(report.command.owner, "native_linuxcncrsh") &&
           report.command.accepted;
}


static int expect_home_native_gate(V5MainPage *page)
{
    V5MainPageActionReport report;
    unsigned long long started = smoke_now_ms();
    g_home_hook_terminal = 0;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        fprintf(stderr, "ALL_HOME trigger rejected\n");
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || !report.prepared || report.local_only || report.executed) {
        fprintf(stderr, "ALL_HOME initial report invalid action=%d prepared=%d local=%d executed=%d\n",
                (int)report.action, report.prepared, report.local_only, report.executed);
        return 0;
    }
    if (smoke_now_ms() > started + 15ULL ||
        !same_text(report.command.name, "home") ||
        !same_text(report.command.owner, "native_home_mode_gate") ||
        report.request.kind != V5_COMMAND_HOME ||
        report.request.index_value != 0 || report.request.enabled_value != 0 ||
        report.request.axis_value != 0.0 || report.request.increment_value != 0.0 ||
        report.request.axis_mask != 0U || report.request.text_value ||
        report.request.secondary_text_value || report.request.mode_value ||
        !report.command.accepted || report.command_line[0] ||
        report.send_status != V5_COMMAND_GATE_SEND_SENT || !report.pending_readback) {
        fprintf(stderr,
                "ALL_HOME request contract invalid kind=%d owner=%s mask=%u text=%p secondary=%p mode=%p accepted=%d send=%d pending=%d code=%s\n",
                (int)report.request.kind,
                report.command.owner ? report.command.owner : "(null)",
                report.request.axis_mask,
                (void *)report.request.text_value,
                (void *)report.request.secondary_text_value,
                (void *)report.request.mode_value,
                report.command.accepted,
                (int)report.send_status,
                report.pending_readback,
                report.readback_code);
        return 0;
    }
    if (!wait_home_transaction(page)) {
        fprintf(stderr, "ALL_HOME transaction did not finish\n");
        return 0;
    }
    if (!page->last_action.executed ||
        !same_text(page->last_action.readback_code, "ALL_HOME_CONFIRMED") ||
        v5_main_page_home_transaction_active()) {
        fprintf(stderr, "ALL_HOME final report invalid executed=%d code=%s active=%d\n",
                page->last_action.executed,
                page->last_action.readback_code,
                v5_main_page_home_transaction_active());
        return 0;
    }
    return 1;
}

static int expect_axis_zero_position(V5MainPage *page, char axis, const char *space)
{
    V5MainPageActionReport report;
    g_home_hook_terminal = 0;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_HOME || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    return same_text(report.command.name, "axis_zero_position") &&
           same_text(report.command.owner, "native_axis_zero_position") &&
           report.command.accepted &&
           report.request.kind == V5_COMMAND_AXIS_ZERO_POSITION &&
           report.request.text_value && report.request.text_value[0] == axis &&
           report.request.text_value[1] == '\0' &&
           same_text(report.request.mode_value, space) &&
           !report.command_line[0] &&
           report.send_status == V5_COMMAND_GATE_SEND_SENT &&
           report.pending_readback &&
           wait_home_transaction(page);
}

static int expect_estop_local_reset_preserves_program_and_homed(V5MainPage *page)
{
    V5MainPageActionReport report;
    V5CommandGateHomeStatus status;
    V5ProgramOpenResult saved_open;
    V5NativeReadback before;
    V5ProgramController *controller;
    unsigned int attempt;
    int progress_seen = 0;
    int terminal_polled = 0;
    char status_text[64];
    if (!page) return 0;
    saved_open = page->last_program_open;
    before = page->native_readback;
    controller = page->program_controller;
    page->last_program_open.ok = 1;
    page->last_program_open.display_name = "cc-ac.ngc";
    page->last_program_open.source_path = "/opt/8ax/programs/cc-ac.ngc";
    page->last_program_open.loaded_epoch = 77U;
    v5_native_readback_set_all_homed(&page->native_readback, 1);
    before = page->native_readback;
    g_home_hook_terminal = 0;
    g_home_progress_terminal_phase = 0U;
    g_home_send_delay_ms = 250U;
    g_home_send_hook_returned = 0;
    v5_main_page_select_all_axes(page);
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report) ||
        !v5_main_page_home_transaction_active() || !page->home_transaction_active) {
        page->last_program_open = saved_open;
        return 0;
    }
    for (attempt = 0U; attempt < 100U; ++attempt) {
        if (v5_main_page_home_transaction_status(&status)) {
            progress_seen = 1;
            break;
        }
        smoke_sleep_us(1000U);
    }
    if (!progress_seen) {
        v5_main_page_home_transaction_reset_after_estop(page);
        page->last_program_open = saved_open;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    v5_main_page_home_transaction_reset_after_estop(page);
    if (!v5_main_page_home_transaction_active() || page->home_transaction_active) {
        page->last_program_open = saved_open;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    g_home_progress_terminal_phase = V5_NATIVE_HOME_PHASE_CANCELLED;
    for (attempt = 0U; attempt < 500U; ++attempt) {
        if (v5_main_page_home_transaction_poll(page)) {
            terminal_polled = 1;
            break;
        }
        smoke_sleep_us(1000U);
    }
    status_text[0] = '\0';
    if (!terminal_polled || v5_main_page_home_transaction_active() || page->home_transaction_active ||
        !v5_main_page_home_transaction_status(&status) || !status.terminal || !status.cancelled ||
        status.phase != V5_NATIVE_HOME_PHASE_CANCELLED ||
        !v5_main_page_home_transaction_format_status_cn(&status, status_text, sizeof(status_text)) ||
        !same_text(status_text, "回零已取消") ||
        page->last_action.executed || page->last_action.pending_readback ||
        !same_text(page->last_action.readback_code, "HOME_CANCELLED") ||
        page->program_controller != controller ||
        !page->last_program_open.ok ||
        page->last_program_open.loaded_epoch != 77U ||
        !same_text(page->last_program_open.display_name, "cc-ac.ngc") ||
        memcmp(&page->native_readback, &before, sizeof(before)) != 0) {
        page->last_program_open = saved_open;
        g_home_progress_terminal_phase = 0U;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    for (attempt = 0U; attempt < 500U && !g_home_send_hook_returned; ++attempt) {
        smoke_sleep_us(1000U);
    }
    page->last_program_open = saved_open;
    g_home_progress_terminal_phase = 0U;
    g_home_send_delay_ms = 25U;
    return g_home_send_hook_returned != 0;
}


static int expect_first_point(V5MainPage *page, const char *path)
{
    V5MainPageActionReport report;
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_FIRST_POINT, &report)) {
        return 0;
    }
    if (report.action != V5_MAIN_PAGE_ACTION_FIRST_POINT || !report.prepared || report.local_only || report.executed) {
        return 0;
    }
    if (!same_text(report.command.name, "first_point") || !same_text(report.command.owner, "native_first_point")) {
        return 0;
    }
    if (!same_text(report.request.text_value, path) || !same_text(report.request.mode_value, "AC_XY_Z")) {
        return 0;
    }
    if (!report.request.secondary_text_value || strlen(report.request.secondary_text_value) != 64U ||
        report.request.index_value <= 0 || report.request.axis_mask !=
            (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK | V5_COMMAND_AXIS_Z_MASK | V5_COMMAND_AXIS_A_MASK | V5_COMMAND_AXIS_C_MASK)) {
        return 0;
    }
    if (report.request.point_axis[0] != 1.0 || report.request.point_axis[1] != 2.0 ||
        report.request.point_axis[2] != 3.0 || report.request.point_axis[3] != 4.0 ||
        report.request.point_axis[4] != 5.0) {
        return 0;
    }
    return !report.command_line[0];
}

static const char *button_text_for_action(V5MainPage *page, V5MainPageActionKind action)
{
    unsigned int i;
    if (!page) {
        return "";
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == action && page->button_labels[i]) {
            return lv_label_get_text(page->button_labels[i]);
        }
    }
    return "";
}

static lv_obj_t *button_for_action(V5MainPage *page, V5MainPageActionKind action)
{
    unsigned int i;
    if (!page) {
        return 0;
    }
    for (i = 0U; i < page->button_count; ++i) {
        if (page->button_actions[i] == action) {
            return page->buttons[i];
        }
    }
    return 0;
}

static int button_bg_matches(V5MainPage *page, V5MainPageActionKind action, int r, int g, int b)
{
    lv_obj_t *button = button_for_action(page, action);
    if (!button) {
        return 0;
    }
    return lv_color_to32(lv_obj_get_style_bg_color(button, LV_PART_MAIN)) ==
           lv_color_to32(lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b));
}

static int expect_home_terminal_clears_visual(V5MainPage *page, unsigned int phase)
{
    V5MainPageActionReport report;
    V5CommandGateHomeStatus status;
    lv_obj_t *button;
    unsigned int attempt;
    unsigned int flush_before_terminal;
    char text[64];
    int polled = 0;
    if (!page || (phase != V5_NATIVE_HOME_PHASE_FAILED &&
                  phase != V5_NATIVE_HOME_PHASE_CANCELLED)) return 0;
    button = button_for_action(page, V5_MAIN_PAGE_ACTION_HOME);
    if (!button) return 0;
    g_home_hook_terminal = 0;
    g_home_progress_terminal_phase = phase;
    g_home_send_delay_ms = 250U;
    g_home_send_hook_returned = 0;
    v5_main_page_select_all_axes(page);
    if (!v5_main_page_trigger_action(page, V5_MAIN_PAGE_ACTION_HOME, &report) ||
        !v5_main_page_home_transaction_active() || !page->home_transaction_active ||
        lv_obj_has_state(button, LV_STATE_USER_1) ||
        !button_bg_matches(page, V5_MAIN_PAGE_ACTION_HOME, 42, 63, 85)) {
        g_home_progress_terminal_phase = 0U;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    flush_before_terminal = v5_lvgl_headless_flush_count();
    for (attempt = 0U; attempt < 100U; ++attempt) {
        if (v5_main_page_home_transaction_poll(page)) {
            polled = 1;
            break;
        }
        smoke_sleep_us(1000U);
    }
    if (!polled || g_home_send_hook_returned ||
        v5_main_page_home_transaction_active() || page->home_transaction_active ||
        page->last_action.pending_readback ||
        lv_obj_has_state(button, LV_STATE_PRESSED) ||
        lv_obj_has_state(button, LV_STATE_USER_1) ||
        v5_lvgl_headless_flush_count() <= flush_before_terminal ||
        !button_bg_matches(page, V5_MAIN_PAGE_ACTION_HOME, 42, 63, 85) ||
        !v5_main_page_home_transaction_status(&status) || !status.terminal ||
        status.phase != phase) {
        g_home_progress_terminal_phase = 0U;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    text[0] = '\0';
    if (phase == V5_NATIVE_HOME_PHASE_FAILED) {
        if (v5_main_page_home_transaction_format_status_cn(&status, text, sizeof(text)) || text[0]) {
            g_home_progress_terminal_phase = 0U;
            g_home_send_delay_ms = 25U;
            return 0;
        }
    } else if (!v5_main_page_home_transaction_format_status_cn(&status, text, sizeof(text)) ||
               strcmp(text, "回零已取消") != 0) {
        g_home_progress_terminal_phase = 0U;
        g_home_send_delay_ms = 25U;
        return 0;
    }
    for (attempt = 0U; attempt < 500U && !g_home_send_hook_returned; ++attempt) {
        smoke_sleep_us(1000U);
    }
    g_home_progress_terminal_phase = 0U;
    g_home_send_delay_ms = 25U;
    return g_home_send_hook_returned != 0;
}

static int button_object_bg_matches(lv_obj_t *button, int r, int g, int b)
{
    lv_color_t actual;
    lv_color_t expected;
    if (!button) {
        return 0;
    }
    actual = lv_obj_get_style_bg_color(button, LV_PART_MAIN);
    expected = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return lv_color_to32(actual) == lv_color_to32(expected);
}

static int program_row_bg_matches(V5MainPage *page, unsigned int row, int r, int g, int b)
{
    lv_color_t actual;
    lv_color_t expected;
    if (!page || row >= 4U || !page->program_line_bg[row]) {
        return 0;
    }
    actual = lv_obj_get_style_bg_color(page->program_line_bg[row], LV_PART_MAIN);
    expected = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
    return lv_color_to32(actual) == lv_color_to32(expected);
}

static int program_row_text_has(V5MainPage *page, unsigned int row, const char *needle)
{
    const char *text;
    if (!page || row >= 4U || !page->program_line_labels[row] || !needle) {
        return 0;
    }
    text = lv_label_get_text(page->program_line_labels[row]);
    return text && strstr(text, needle) != 0;
}

static int button_pressed_state_clears_on_click(lv_obj_t *button, int r, int g, int b)
{
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    if (!lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_CLICKED, 0);
    return !lv_obj_has_state(button, LV_STATE_PRESSED) &&
           button_object_bg_matches(button, r, g, b);
}

static int button_pressed_state_clears_on_release(lv_obj_t *button)
{
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    v5_lvgl_headless_reset_flush_count();
    lv_event_send(button, LV_EVENT_RELEASED, 0);
    return !lv_obj_has_state(button, LV_STATE_PRESSED) &&
           v5_lvgl_headless_flush_count() > 0U;
}

static int button_visual_state_cycle(lv_obj_t *button)
{
    if (!button) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    lv_event_send(button, LV_EVENT_PRESS_LOST, 0);
    if (lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    lv_obj_add_state(button, LV_STATE_PRESSED);
    lv_event_send(button, LV_EVENT_CANCEL, 0);
    if (lv_obj_has_state(button, LV_STATE_PRESSED)) {
        return 0;
    }
    v5_button_visual_set_transaction_active(button, 1);
    if (!lv_obj_has_state(button, LV_STATE_USER_1) ||
        !button_object_bg_matches(button, 29, 151, 104)) {
        return 0;
    }
    v5_button_visual_set_transaction_active(button, 0);
    return !lv_obj_has_state(button, LV_STATE_USER_1);
}

static int exercise_jog_press_timing(V5MainPage *page)
{
    lv_obj_t *button;
    if (!page || !v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, 'X')) {
        return 0;
    }
    button = button_for_action(page, V5_MAIN_PAGE_ACTION_JOG_PLUS);
    if (!button) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(499U);
    (void)lv_timer_handler();
    lv_event_send(button, LV_EVENT_RELEASED, 0);
    if (page->last_action.request.kind != V5_COMMAND_JOG_INCREMENT ||
        page->last_action.request.increment_value != page->jog_step ||
        page->last_action.request.axis_value <= 0.0 || page->jog_pressed_button) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(500U);
    (void)lv_timer_handler();
    if (page->last_action.request.kind != V5_COMMAND_JOG_CONTINUOUS ||
        !page->jog_long_press_elapsed) {
        return 0;
    }
    lv_tick_inc(V5_MAIN_PAGE_JOG_KEEPALIVE_MS);
    lv_event_send(button, LV_EVENT_PRESSING, 0);
    if (page->last_action.request.kind != V5_COMMAND_JOG_CONTINUOUS ||
        page->jog_keepalive_last_tick == 0U) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESS_LOST, 0);
    if (page->jog_pressed_button || page->jog_long_press_elapsed ||
        page->last_action.request.kind != V5_COMMAND_JOG_STOP) {
        return 0;
    }
    lv_event_send(button, LV_EVENT_PRESSED, 0);
    lv_tick_inc(V5_MAIN_PAGE_JOG_HOLD_MS);
    (void)lv_timer_handler();
    lv_event_send(button, LV_EVENT_CANCEL, 0);
    return !page->jog_pressed_button && !page->jog_long_press_elapsed &&
           page->last_action.request.kind == V5_COMMAND_JOG_STOP;
}

static int exercise_axis_selection_timeout(V5MainPage *page)
{
    if (!page || !v5_main_page_select_axis(page, V5_MAIN_PAGE_SELECT_MCS, 'Y')) {
        return 0;
    }
    lv_tick_inc(2500U);
    (void)lv_timer_handler();
    if (page->selection.all_axes || !expect_local(page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "jog_step")) {
        return 0;
    }
    lv_tick_inc(1000U);
    (void)lv_timer_handler();
    if (page->selection.all_axes) {
        return 0;
    }
    lv_tick_inc(2100U);
    (void)lv_timer_handler();
    return page->selection.all_axes && page->selection.space == V5_MAIN_PAGE_SELECT_MCS;
}

static void refresh_estop_active(void *user_data, V5MainPageActionKind action)
{
    V5MainPage *page = (V5MainPage *)user_data;
    V5NativeReadback readback;
    if (!page || action != V5_MAIN_PAGE_ACTION_ESTOP_FORCE) {
        return;
    }
    v5_native_readback_init(&readback);
    v5_native_readback_set_safety_estop(&readback, 1);
    v5_native_readback_set_machine_enabled(&readback, 0);
    v5_main_page_set_native_readback(page, &readback);
}

static int write_first_point_program(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G21\nG90\nG1 X1 Y2 Z3 A4 C5\n", fp);
    return fclose(fp) == 0;
}

static int write_preview_scroll_program(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G21\nG90\nG1 X1\nG1 X2\nG1 X3\nG1 X4\nG1 X5\nG1 X6\n", fp);
    return fclose(fp) == 0;
}

static int expect_missing_gate(V5MainPage *page, V5MainPageActionKind action)
{
    V5MainPageActionReport report;
    if (v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    return page->last_action.action == action && !page->last_action.prepared;
}

static int expect_power_on_home_block(
    V5MainPage *page,
    V5MainPageActionKind action,
    const char *readback_code)
{
    V5MainPageActionReport report;
    const char *expected_title;
    const char *message;
    if (!page || !readback_code || !v5_main_page_trigger_action(page, action, &report)) {
        return 0;
    }
    if (report.action != action || report.prepared || !report.local_only || report.executed ||
        report.send_status != V5_COMMAND_GATE_SEND_INVALID ||
        report.request.kind != V5_COMMAND_UI_LOCAL ||
        !same_text(report.command.name, "power_on_home_precondition") ||
        !same_text(report.command.owner, "native_home_precondition") ||
        report.command.accepted || !same_text(report.readback_code, readback_code)) {
        return 0;
    }
    if (!page->power_on_home_popup || !page->power_on_home_popup_message ||
        !page->power_on_home_popup_confirm ||
        !lv_obj_has_state(page->power_on_home_popup_confirm, LV_STATE_DISABLED) ||
        !page->power_on_home_popup_close ||
        lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    expected_title = same_text(readback_code, "POWER_ON_HOME_REQUIRED") ?
        "需要回零" : "回零状态不可用";
    message = lv_label_get_text(page->power_on_home_popup_message);
    if (!message || strstr(message, readback_code) || !strstr(message, "提示:") ||
        !strstr(message, expected_title) || !strstr(message, "原因:") ||
        !strstr(message, "下一步:") || strstr(message, "LinuxCNC") ||
        strstr(message, "linuxcnc")) {
        return 0;
    }
    lv_event_send(page->power_on_home_popup_close, LV_EVENT_RELEASED, 0);
    return lv_obj_has_flag(page->power_on_home_popup, LV_OBJ_FLAG_HIDDEN);
}

int main(void)
{
    V5MainPage page;
    V5MainPageActionReport visual_report_before;
    V5ProgramController controller;
    lv_obj_t *screen;
    lv_obj_t *visual_button;

    lv_init();
    if (!v5_lvgl_headless_display_setup()) {
        return 1;
    }
    screen = lv_scr_act();
    if (!v5_main_page_create(&page, screen)) {
        return 2;
    }
    v5_main_page_home_transaction_set_send_hook(home_send_hook);
    v5_main_page_home_transaction_set_progress_hook(home_progress_hook);
    v5_main_page_set_command_execution_enabled(&page, 0);
    g_home_send_hook_calls = 0U;
    if (page.button_count != V5_MAIN_PAGE_BUTTON_COUNT) {
        return 3;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
        return 26;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL), "机械全轴")) {
        return 37;
    }
    if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
        return 33;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_START, 16, 48, 77) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_HOME, 42, 63, 85) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, 42, 63, 85) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS, 32, 52, 73)) {
        return 43;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G54, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 29, 151, 104) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_1, 29, 151, 104) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, 32, 52, 73)) {
        return 44;
    }
    visual_button = lv_btn_create(screen);
    if (!visual_button) {
        return 75;
    }
    lv_obj_set_size(visual_button, 40, 24);
    lv_obj_set_style_bg_color(visual_button, lv_color_make(42, 63, 85), 0);
    v5_button_visual_bind(visual_button);
    memcpy(&visual_report_before, &page.last_action, sizeof(visual_report_before));
    if (!button_pressed_state_clears_on_click(visual_button, 42, 63, 85)) {
        return 50;
    }
    if (!button_pressed_state_clears_on_release(visual_button)) {
        return 65;
    }
    if (!button_visual_state_cycle(visual_button)) {
        return 67;
    }
    if (page.command_execution_enabled || g_home_send_hook_calls != 0U ||
        v5_main_page_home_transaction_active() ||
        memcmp(&visual_report_before, &page.last_action, sizeof(visual_report_before)) != 0) {
        return 74;
    }
    lv_obj_del(visual_button);
    v5_main_page_set_command_execution_enabled(&page, 1);
    if (!expect_home_terminal_clears_visual(&page, V5_NATIVE_HOME_PHASE_FAILED) ||
        !expect_home_terminal_clears_visual(&page, V5_NATIVE_HOME_PHASE_CANCELLED)) {
        return 121;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X') ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_HOME,
                "POWER_ON_HOME_REQUIRED") ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_START,
                "POWER_ON_HOME_REQUIRED")) {
            return 68;
        }
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X')) {
            return 71;
        }
        {
            V5MainPageActionReport modal_report;
            if (!v5_main_page_trigger_action(
                    &page,
                    V5_MAIN_PAGE_ACTION_HOME,
                    &modal_report) ||
                !same_text(modal_report.readback_code, "POWER_ON_HOME_REQUIRED")) {
                return 71;
            }
        }
        lv_tick_inc(3100U);
        (void)lv_timer_handler();
        if (lv_obj_has_flag(page.power_on_home_popup, LV_OBJ_FLAG_HIDDEN) ||
            page.selection.all_axes || page.selection.axis != 'X') {
            return 71;
        }
        lv_event_send(page.power_on_home_popup_close, LV_EVENT_RELEASED, 0);
        lv_tick_inc(3100U);
        (void)lv_timer_handler();
        if (!page.selection.all_axes || page.selection.space != V5_MAIN_PAGE_SELECT_MCS) {
            return 72;
        }
        v5_main_page_select_all_axes(&page);
        if (!expect_home_native_gate(&page)) {
            return 69;
        }
        v5_native_readback_set_unavailable(&readback, "home_status_unknown");
        v5_main_page_set_native_readback(&page, &readback);
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'X') ||
            !expect_power_on_home_block(
                &page,
                V5_MAIN_PAGE_ACTION_HOME,
                "POWER_ON_HOME_STATUS_UNAVAILABLE")) {
            return 70;
        }
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_select_all_axes(&page);
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_native_readback_set_safety_estop(&readback, 1);
        v5_native_readback_set_machine_enabled(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_select_all_axes(&page);
        if (!expect_home_native_gate(&page)) {
            return 73;
        }
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
    }
    v5_program_controller_init(&controller);
    v5_main_page_bind_program_controller(&page, &controller);

    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MAIN, "nav_main") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_SETTINGS, "nav_settings") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_PROGRAM, "nav_program") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MDI, "nav_mdi") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT, "nav_mdi_edit") ||
        !expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all")) {
        return 4;
    }
    {
        V5MainPageActionKind nav_action = 0;
        if (!page.program_edit_hit_area) {
            return 51;
        }
        v5_main_page_set_navigation_callback(&page, capture_nav_action, &nav_action);
        lv_tick_inc(1);
        lv_event_send(page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
        if (nav_action != 0) {
            return 52;
        }
        lv_tick_inc(100);
        lv_event_send(page.program_edit_hit_area, LV_EVENT_CLICKED, 0);
        if (nav_action != V5_MAIN_PAGE_ACTION_NAV_MDI_EDIT) {
            return 53;
        }
        v5_main_page_set_navigation_callback(&page, 0, 0);
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, "jog_step") || page.jog_step != 0.01) {
        return 5;
    }
    if (!exercise_jog_press_timing(&page) || !exercise_axis_selection_timeout(&page)) {
        return 66;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_1, 32, 52, 73) ||
        !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_JOG_STEP_10, 29, 151, 104)) {
        return 45;
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_VIEW_XZ, "view_xz") || page.view_plane != V5_TOOLPATH_DISPLAY_XZ) {
        return 6;
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_VIEW_3D, "view_3d") || page.view_plane != V5_TOOLPATH_DISPLAY_3D) {
        return 7;
    }

    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_PAUSE) ||
        !expect_home_native_gate(&page) ||
        !expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_force", "native_safety") ||
        !expect_command(&page, V5_MAIN_PAGE_ACTION_WCS_G55, "wcs_select", "native_linuxcncrsh")) {
        return 8;
    }
    if (!expect_estop_local_reset_preserves_program_and_homed(&page)) {
        return 120;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_interpreter_paused(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_PAUSE, "pause", "native_linuxcncrsh", "Set Pause")) {
            return 22;
        }
        v5_native_readback_set_interpreter_paused(&readback, 1);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_START, "resume", "native_linuxcncrsh", "Set Resume")) {
            return 121;
        }
        if (!expect_command_line(&page, V5_MAIN_PAGE_ACTION_PAUSE, "resume", "native_linuxcncrsh", "Set Resume")) {
            return 23;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
    }

    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_safety_estop(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "取消急停")) {
            return 27;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 24;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        v5_native_readback_init(&readback);
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "取消急停")) {
            return 29;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 30;
        }
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
            return 31;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_force", "native_safety")) {
            return 32;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        v5_main_page_set_native_readback_refresh_callback(&page, refresh_estop_active, &page);
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE, "estop_reset", "native_safety")) {
            return 25;
        }
        v5_main_page_set_native_readback_refresh_callback(&page, 0, 0);
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_ESTOP_FORCE), "急停")) {
            return 28;
        }
    }

    if (!expect_override(&page, 0, 120, "feed_override_set") ||
        !expect_override(&page, 1, 80, "spindle_override_set")) {
        return 21;
    }
    if (v5_main_page_trigger_override(&page, 0, -1, 0) ||
        v5_main_page_trigger_override(&page, 1, 201, 0)) {
        return 65;
    }
    {
        V5NativeReadback readback;
        v5_native_readback_init(&readback);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
    }
    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS) ||
        !expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X)) {
        return 16;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'Y') ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_PLUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y 1.000 0.010") ||
        !expect_command_line(&page, V5_MAIN_PAGE_ACTION_JOG_MINUS, "jog_increment", "native_linuxcncrsh", "Set Jog_Incr Y -1.000 0.010") ||
        !expect_axis_zero_position(&page, 'Y', "mcs")) {
        return 17;
    }
    {
        V5NativeReadback model_readback;
        v5_native_readback_init(&model_readback);
        v5_native_readback_set_motion_model(&model_readback, "XYZAC_TRT");
        v5_native_readback_set_all_homed(&model_readback, 1);
        v5_main_page_set_native_readback(&page, &model_readback);
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'A') ||
        !expect_axis_zero_position(&page, 'A', "mcs")) {
        return 18;
    }
    if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'C') ||
        !expect_axis_zero_position(&page, 'C', "mcs")) {
        return 36;
    }
    {
        V5NativeReadback model_readback;
        v5_native_readback_init(&model_readback);
        v5_native_readback_set_motion_model(&model_readback, "XYZBC_TRT");
        v5_native_readback_set_all_homed(&model_readback, 1);
        v5_main_page_set_native_readback(&page, &model_readback);
        if (page.mcs_targets[3].axis != 'B' || page.wcs_targets[3].axis != 'B' ||
            page.mcs_targets[4].axis != 'C' ||
            !v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_MCS, 'B') ||
            !expect_axis_zero_position(&page, 'B', "mcs")) {
            return 67;
        }
    }
    {
        V5NativeReadback readback;
        double table[V5_NATIVE_READBACK_WCS_COUNT][V5_NATIVE_READBACK_WCS_AXIS_COUNT];
        memset(table, 0, sizeof(table));
        table[2][0] = 10.0;
        table[2][1] = 20.0;
        table[2][2] = -30.0;
        v5_native_readback_init(&readback);
        v5_native_readback_set_wcs_table(
            &readback,
            2,
            &table[0][0],
            V5_NATIVE_READBACK_WCS_COUNT,
            V5_NATIVE_READBACK_WCS_AXIS_COUNT,
            9U);
        v5_native_readback_set_rtcp_actual(&readback, 0);
        v5_native_readback_set_interpreter_idle(&readback, 1);
        v5_native_readback_set_all_homed(&readback, 1);
        v5_native_readback_set_safety_estop(&readback, 0);
        v5_native_readback_set_machine_enabled(&readback, 1);
        v5_native_readback_set_modal_actual(&readback, "G0 G17 G21 G40 G49 G56 G64 G80 G90 G94 G97");
        v5_native_readback_set_tool_actual(&readback, 0, 1, 15.0);
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G54, 32, 52, 73) ||
            !button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G56, 35, 198, 120)) {
            return 46;
        }
        if (!v5_main_page_select_axis(&page, V5_MAIN_PAGE_SELECT_WCS, 'Z') ||
            !expect_command_line(&page, V5_MAIN_PAGE_ACTION_WORK_ZERO_X, "work_zero", "native_work_zero", "Set MDI G10 L20 P3 Z0") ||
            !expect_axis_zero_position(&page, 'Z', "wcs")) {
            return 19;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 42, 63, 85)) {
            return 47;
        }
        v5_native_readback_set_unavailable(&readback, "smoke_reset");
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_WCS_G56, 32, 52, 73)) {
            return 48;
        }
    }
    if (!expect_local(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, "axis_all") || !page.selection.all_axes || page.selection.space != V5_MAIN_PAGE_SELECT_MCS) {
        return 20;
    }
    if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_AXIS_ALL, 29, 151, 104)) {
        return 49;
    }

    v5_native_readback_set_all_homed(&page.native_readback, 1);
    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_START)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (!v5_main_page_set_mdi_text(&page, "G4 P0")) {
        v5_program_controller_destroy(&controller);
        return 10;
    }
    if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
        v5_program_controller_destroy(&controller);
        return 50;
    }
    {
        V5NativeReadback line_readback;
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_interpreter_paused(&line_readback, 0);
        v5_native_readback_set_all_homed(&line_readback, 1);
        v5_native_readback_set_current_line(&line_readback, 1);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            v5_program_controller_destroy(&controller);
            return 51;
        }
        v5_native_readback_set_interpreter_idle(&line_readback, 1);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            v5_program_controller_destroy(&controller);
            return 52;
        }
    }
    if (!expect_command(&page, V5_MAIN_PAGE_ACTION_START, "mdi_run", "native_linuxcncrsh")) {
        v5_program_controller_destroy(&controller);
        return 11;
    }

    if (!expect_missing_gate(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE)) {
        v5_program_controller_destroy(&controller);
        return 12;
    }

    {
        V5NativeReadback readback;
        const char *modal_text;
        v5_native_readback_init(&readback);
        v5_native_readback_set_rtcp_actual(&readback, 0);
        v5_main_page_set_native_readback(&page, &readback);
        modal_text = lv_label_get_text(page.modal_label);
        if (!modal_text || !strstr(modal_text, "L--\nRTCP OFF\n")) {
            v5_program_controller_destroy(&controller);
            return 38;
        }
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
            v5_program_controller_destroy(&controller);
            return 34;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 42, 63, 85)) {
            v5_program_controller_destroy(&controller);
            return 40;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_rtcp_control")) {
            v5_program_controller_destroy(&controller);
            return 13;
        }
        v5_native_readback_set_rtcp_actual(&readback, 1);
        v5_main_page_set_native_readback(&page, &readback);
        modal_text = lv_label_get_text(page.modal_label);
        if (!modal_text || !strstr(modal_text, "L--\nRTCP ON\n")) {
            v5_program_controller_destroy(&controller);
            return 39;
        }
        if (!same_text(button_text_for_action(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE), "RTCP")) {
            v5_program_controller_destroy(&controller);
            return 35;
        }
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 29, 151, 104)) {
            v5_program_controller_destroy(&controller);
            return 41;
        }
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, "rtcp_set", "native_rtcp_control")) {
            v5_program_controller_destroy(&controller);
            return 14;
        }
        v5_native_readback_set_unavailable(&readback, "rtcp_unknown");
        v5_main_page_set_native_readback(&page, &readback);
        if (!button_bg_matches(&page, V5_MAIN_PAGE_ACTION_RTCP_TOGGLE, 42, 63, 85)) {
            v5_program_controller_destroy(&controller);
            return 42;
        }
    }

    v5_native_readback_set_all_homed(&page.native_readback, 1);
    {
        const char *first_point_path = "v5_first_point_smoke.ngc";
        V5ProgramOpenResult open_result;
        memset(&open_result, 0, sizeof(open_result));
        if (!write_first_point_program(first_point_path) ||
            !v5_main_page_open_program(&page, first_point_path, &open_result) ||
            !expect_first_point(&page, first_point_path)) {
            unlink(first_point_path);
            v5_program_controller_destroy(&controller);
            return 15;
        }
        unlink(first_point_path);
    }

    {
        const char *preview_scroll_path = "v5_preview_scroll_smoke.ngc";
        V5ProgramOpenResult open_result;
        V5NativeReadback idle_readback;
        V5NativeReadback line_readback;
        V5NativeReadback stopped_readback;
        lv_point_t preview_touch_start = {120, 535};
        lv_point_t preview_touch_move = {120, 505};
        int preview_changed = 0;
        memset(&open_result, 0, sizeof(open_result));
        if (!write_preview_scroll_program(preview_scroll_path) ||
            !v5_main_page_open_program(&page, preview_scroll_path, &open_result)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 56;
        }
        v5_native_readback_init(&idle_readback);
        v5_native_readback_set_interpreter_idle(&idle_readback, 1);
        v5_main_page_set_native_readback(&page, &idle_readback);
        if (!program_row_text_has(&page, 0U, "001 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 61;
        }
        v5_native_readback_set_current_line(&idle_readback, 8);
        v5_main_page_set_native_readback(&page, &idle_readback);
        if (!program_row_text_has(&page, 0U, "001 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 63;
        }
        if (!v5_main_page_handle_touch_points(&page, &preview_touch_start, 1, 1, &preview_changed) ||
            !v5_main_page_handle_touch_points(&page, &preview_touch_move, 1, 1, &preview_changed) ||
            !preview_changed ||
            !program_row_text_has(&page, 0U, "002 ") ||
            !program_row_text_has(&page, 3U, "005 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 57;
        }
        if (!v5_main_page_handle_touch_points(&page, 0, 0, 0, &preview_changed)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 58;
        }
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_current_line(&line_readback, 8);
        v5_native_readback_set_motion_line(&line_readback, 3);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "003 ") ||
            !program_row_text_has(&page, 3U, "006 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 64;
        }
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_current_line(&line_readback, 5);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 59;
        }
        v5_native_readback_set_current_line(&line_readback, 8);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 3U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 62;
        }
        v5_native_readback_init(&stopped_readback);
        v5_native_readback_set_safety_estop(&stopped_readback, 1);
        v5_native_readback_set_machine_enabled(&stopped_readback, 0);
        v5_native_readback_set_interpreter_idle(&stopped_readback, 1);
        v5_native_readback_set_interpreter_paused(&stopped_readback, 0);
        v5_native_readback_set_current_line(&stopped_readback, 0);
        v5_native_readback_set_motion_line(&stopped_readback, 0);
        v5_main_page_set_native_readback(&page, &stopped_readback);
        if (!program_row_text_has(&page, 0U, "005 ") ||
            !program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 3U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 60;
        }
        v5_native_readback_set_safety_estop(&stopped_readback, 0);
        v5_native_readback_set_machine_enabled(&stopped_readback, 1);
        v5_native_readback_set_all_homed(&stopped_readback, 1);
        v5_native_readback_set_current_line(&stopped_readback, 8);
        v5_native_readback_set_motion_line(&stopped_readback, 8);
        v5_main_page_set_native_readback(&page, &stopped_readback);
        if (!expect_command(&page, V5_MAIN_PAGE_ACTION_START, "start", "native_linuxcncrsh")) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 122;
        }
        v5_main_page_internal_sync_program_preview_after_execution(&page, V5_COMMAND_START);
        if (page.program_preview_scroll_start_line != 1U ||
            !program_row_text_has(&page, 0U, "001 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 123;
        }
        v5_native_readback_init(&line_readback);
        v5_native_readback_set_safety_estop(&line_readback, 0);
        v5_native_readback_set_machine_enabled(&line_readback, 1);
        v5_native_readback_set_interpreter_idle(&line_readback, 0);
        v5_native_readback_set_interpreter_paused(&line_readback, 0);
        v5_native_readback_set_current_line(&line_readback, 3);
        v5_main_page_set_native_readback(&page, &line_readback);
        if (!program_row_text_has(&page, 0U, "003 ") ||
            !program_row_bg_matches(&page, 0U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 124;
        }
        v5_native_readback_set_interpreter_paused(&line_readback, 1);
        v5_native_readback_set_current_line(&line_readback, 8);
        v5_main_page_set_native_readback(&page, &line_readback);
        v5_main_page_internal_sync_program_preview_after_execution(&page, V5_COMMAND_RESUME);
        if (!program_row_text_has(&page, 3U, "008 ") ||
            !program_row_bg_matches(&page, 3U, 43, 133, 83)) {
            unlink(preview_scroll_path);
            v5_program_controller_destroy(&controller);
            return 125;
        }
        unlink(preview_scroll_path);
    }

    printf("v5 main page actions: buttons=%u local=view_3d mdi=start jog=native rtcp=toggle axis_select=prepared axis_zero_position=prepared first_point=prepared native_lines=prepared missing_gates=0\n",
           page.button_count);
    v5_main_page_set_command_execution_enabled(&page, 0);
    v5_program_controller_destroy(&controller);
    return 0;
}
