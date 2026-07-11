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

void v5_settings_apply_trim(char *text)
{
    char *start = text;
    char *end;
    if (!text) {
        return;
    }
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    *end = '\0';
}

int v5_settings_apply_numeric_text(const char *value)
{
    char buf[64];
    char *end;
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    v5_settings_apply_trim(buf);
    if (!buf[0]) {
        return 0;
    }
    (void)strtod(buf, &end);
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    return *end == '\0';
}

static int settings_apply_integer_text(const char *value)
{
    char buf[64];
    const char *p;
    if (!value || !value[0] || strlen(value) >= sizeof(buf) ||
        strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    v5_settings_apply_trim(buf);
    p = buf;
    if (*p == '+' || *p == '-') {
        ++p;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    while (*p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int settings_apply_axis_field_requires_integer_value(const char *field_key)
{
    return field_key &&
           (strcmp(field_key, "pitch") == 0 ||
            strcmp(field_key, "motor_rev") == 0 ||
            strcmp(field_key, "load_rev") == 0 ||
            strcmp(field_key, "soft_minus") == 0 ||
            strcmp(field_key, "soft_plus") == 0 ||
            strcmp(field_key, "max_velocity") == 0 ||
            strcmp(field_key, "max_acceleration") == 0 ||
            strcmp(field_key, "backlash") == 0);
}

int v5_settings_apply_safe_ini_value(const char *value)
{
    char buf[64];
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", value);
    v5_settings_apply_trim(buf);
    return buf[0] && strchr(buf, '[') == 0 && strchr(buf, ']') == 0;
}

int v5_settings_apply_values_match(const char *actual, const char *expected)
{
    char *ea;
    char *eb;
    double da;
    double db;
    if (!actual || !expected) {
        return 0;
    }
    if (strcmp(actual, expected) == 0) {
        return 1;
    }
    da = strtod(actual, &ea);
    db = strtod(expected, &eb);
    return ea != actual && eb != expected && *ea == '\0' && *eb == '\0' && da == db;
}

static void settings_apply_format_double(char *out, size_t cap, double value)
{
    if (!out || cap == 0U) {
        return;
    }
    snprintf(out, cap, "%.12g", value);
}

double v5_settings_apply_nearest_integer(double value)
{
    double rounded = (double)((long long)(value >= 0.0 ? value + 0.5 : value - 0.5));
    if (rounded == 0.0) {
        rounded = 0.0;
    }
    return rounded;
}

static void settings_apply_format_integer(char *out, size_t cap, double value)
{
    if (!out || cap == 0U || !isfinite(value)) {
        return;
    }
    snprintf(out, cap, "%.0f", v5_settings_apply_nearest_integer(value));
}

static int settings_apply_mode_to_ini(const char *value, char *out, size_t out_cap)
{
    if (!value || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(value, "直线") == 0 || strcmp(value, "LINEAR") == 0) {
        snprintf(out, out_cap, "LINEAR");
    } else if (strcmp(value, "旋转") == 0 || strcmp(value, "ANGULAR") == 0) {
        snprintf(out, out_cap, "ANGULAR");
    } else if (strcmp(value, "虚拟") == 0 || strcmp(value, "VIRTUAL") == 0) {
        snprintf(out, out_cap, "VIRTUAL");
    } else {
        return 0;
    }
    return out[0] != '\0';
}

static int settings_apply_mode_to_display(const char *value, char *out, size_t out_cap)
{
    if (!value || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(value, "ANGULAR") == 0) {
        snprintf(out, out_cap, "旋转");
    } else if (strcmp(value, "VIRTUAL") == 0) {
        snprintf(out, out_cap, "虚拟");
    } else if (strcmp(value, "LINEAR") == 0) {
        snprintf(out, out_cap, "直线");
    } else {
        snprintf(out, out_cap, "%s", value);
    }
    return out[0] != '\0';
}

int v5_settings_apply_ini_value_for_field(
    const char *field_key,
    const char *value,
    char *ini_value,
    size_t ini_cap,
    char *expected_display,
    size_t display_cap)
{
    char local[64];
    char *end;
    double numeric;
    if (!field_key || !value || !value[0] || !ini_value || ini_cap == 0U ||
        !expected_display || display_cap == 0U) {
        return 0;
    }
    ini_value[0] = '\0';
    expected_display[0] = '\0';
    snprintf(local, sizeof(local), "%s", value);
    v5_settings_apply_trim(local);
    if (!local[0]) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        if (!settings_apply_mode_to_ini(local, ini_value, ini_cap)) {
            return 0;
        }
        return settings_apply_mode_to_display(ini_value, expected_display, display_cap);
    }
    if (strcmp(field_key, "home_direction") == 0) {
        if (strcmp(local, "+") == 0) {
            snprintf(ini_value, ini_cap, "1");
        } else if (strcmp(local, "-") == 0) {
            snprintf(ini_value, ini_cap, "-1");
        } else if (strcmp(local, "0") == 0) {
            snprintf(ini_value, ini_cap, "0");
        } else {
            return 0;
        }
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (strcmp(field_key, "home_order") == 0) {
        if (strcmp(local, "禁用") == 0) {
            snprintf(ini_value, ini_cap, "-1");
        } else if (!v5_settings_apply_numeric_text(local)) {
            return 0;
        } else {
            snprintf(ini_value, ini_cap, "%s", local);
        }
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (strcmp(field_key, "precision") == 0) {
        if (!v5_settings_apply_numeric_text(local)) {
            return 0;
        }
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || numeric <= 0.0) {
            return 0;
        }
        settings_apply_format_double(ini_value, ini_cap, 1.0 / numeric);
        settings_apply_format_double(expected_display, display_cap, numeric);
        return 1;
    }
    if (strcmp(field_key, "direction_mode") == 0) {
        if (strcmp(local, "cw") != 0 && strcmp(local, "ccw") != 0) {
            return 0;
        }
        snprintf(ini_value, ini_cap, "%s", local);
        snprintf(expected_display, display_cap, "%s", local);
        return 1;
    }
    if (!v5_settings_apply_numeric_text(local)) {
        return 0;
    }
    if (settings_apply_axis_field_requires_integer_value(field_key) &&
        !settings_apply_integer_text(local)) {
        return 0;
    }
    if (strcmp(field_key, "max_velocity") == 0) {
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_double(ini_value, ini_cap, numeric / 60.0);
        settings_apply_format_integer(expected_display, display_cap, numeric);
        return 1;
    }
    snprintf(ini_value, ini_cap, "%s", local);
    if (settings_apply_axis_field_requires_integer_value(field_key)) {
        numeric = strtod(local, &end);
        if (end == local || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(expected_display, display_cap, numeric);
    } else {
        snprintf(expected_display, display_cap, "%s", local);
    }
    return 1;
}

int v5_settings_apply_display_from_raw(const char *field_key, const char *raw, char *out, size_t out_cap)
{
    char *end;
    double numeric;
    if (!field_key || !raw || !raw[0] || !out || out_cap == 0U) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0) {
        return settings_apply_mode_to_display(raw, out, out_cap);
    }
    if (strcmp(field_key, "home_direction") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw) {
            return 0;
        }
        if (numeric > 0.0) {
            snprintf(out, out_cap, "+");
        } else if (numeric < 0.0) {
            snprintf(out, out_cap, "-");
        } else {
            snprintf(out, out_cap, "0");
        }
        return 1;
    }
    if (strcmp(field_key, "home_order") == 0 && strcmp(raw, "-1") == 0) {
        snprintf(out, out_cap, "禁用");
        return 1;
    }
    if (strcmp(field_key, "precision") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw || numeric <= 0.0) {
            return 0;
        }
        settings_apply_format_double(out, out_cap, 1.0 / numeric);
        return 1;
    }
    if (strcmp(field_key, "max_velocity") == 0) {
        numeric = strtod(raw, &end);
        if (end == raw || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(out, out_cap, numeric * 60.0);
        return 1;
    }
    if (settings_apply_axis_field_requires_integer_value(field_key)) {
        numeric = strtod(raw, &end);
        if (end == raw || *end != '\0' || !isfinite(numeric)) {
            return 0;
        }
        settings_apply_format_integer(out, out_cap, numeric);
        return 1;
    }
    snprintf(out, out_cap, "%s", raw);
    v5_settings_apply_trim(out);
    return out[0] != '\0';
}

int v5_settings_apply_display_values_match(const char *field_key, const char *actual, const char *expected)
{
    if (!field_key || !actual || !expected) {
        return 0;
    }
    if (strcmp(field_key, "axis_mode") == 0 || strcmp(field_key, "direction_mode") == 0 ||
        strcmp(field_key, "home_direction") == 0 || strcmp(expected, "禁用") == 0) {
        return strcmp(actual, expected) == 0;
    }
    return v5_settings_apply_values_match(actual, expected);
}
