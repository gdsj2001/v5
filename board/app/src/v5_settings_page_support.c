#include "v5_settings_page.h"
#include "v5_button_visuals.h"
#include "v5_settings_actions.h"
#include "v5_settings_axis_table.h"
#include "v5_lvgl_remote_display.h"
#include "v5_motion_model_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include "v5_settings_page_internal.h"

lv_color_t v5_settings_page_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return lv_color_make(r, g, b);
}

double v5_settings_page_monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

void v5_settings_page_clear_obj_style(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *v5_settings_page_make_panel(lv_obj_t *parent, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *panel = lv_obj_create(parent);
    v5_settings_page_clear_obj_style(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, v5_settings_page_rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    return panel;
}

lv_obj_t *v5_settings_page_make_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, v5_settings_page_rgb(r, g, b), 0);
    return label;
}



static void json_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p;
    const char *end;
    char pattern[80];
    size_t n;
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key ? key : "");
    p = strstr(json ? json : "", pattern);
    if (!p) {
        return;
    }
    p = strchr(p, ':');
    if (!p) {
        return;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p != '"') {
        return;
    }
    ++p;
    end = p;
    while (*end && *end != '"') {
        ++end;
    }
    n = (size_t)(end - p);
    if (n >= out_size) {
        n = out_size - 1U;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

static int is_six_digit_id(const char *value)
{
    size_t i;
    if (!value || strlen(value) != 6U) {
        return 0;
    }
    for (i = 0; i < 6U; ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static char g_resident_machine_code[16];
static int g_resident_machine_code_loaded;

void v5_settings_page_set_boot_closure(const V5BootClosure *closure)
{
    char value[32];
    g_resident_machine_code[0] = '\0';
    g_resident_machine_code_loaded = 0;
    if (!closure || !closure->device_register_status.loaded) {
        return;
    }
    json_string_field(closure->device_register_status.text, "vpsDistributionId", value, sizeof(value));
    if (is_six_digit_id(value)) {
        snprintf(g_resident_machine_code, sizeof(g_resident_machine_code), "%s", value);
        g_resident_machine_code_loaded = 1;
    }
}

static int resident_machine_code(char *out, size_t out_size)
{
    if (!out || out_size == 0U || !g_resident_machine_code_loaded) {
        return 0;
    }
    snprintf(out, out_size, "%s", g_resident_machine_code);
    return 1;
}

void v5_settings_page_refresh_machine_code_label(V5SettingsPage *page)
{
    char id[16];
    char text[32];
    if (!page || !page->machine_code_label) {
        return;
    }
    if (resident_machine_code(id, sizeof(id))) {
        snprintf(text, sizeof(text), "本机码 %s", id);
        lv_label_set_text(page->machine_code_label, text);
        lv_obj_set_style_text_color(page->machine_code_label, v5_settings_page_rgb(42, 221, 128), 0);
        return;
    }
    lv_label_set_text(page->machine_code_label, "本机码 未登记");
    lv_obj_set_style_text_color(page->machine_code_label, v5_settings_page_rgb(155, 177, 198), 0);
}
