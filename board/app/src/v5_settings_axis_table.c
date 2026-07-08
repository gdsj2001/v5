#include "v5_settings_axis_table.h"
#include "v5_boot_closure.h"
#include "v5_settings_parameter_store.h"
#include "v5_ui_first_frame_guard.h"
#include "v5_lvgl_remote_display.h"
#include "v5_settings_apply.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const V5SettingsAxisColumnSpec kAxisColumns[];
enum {
    V5_AXIS_FIELD_EDIT = 0,
    V5_AXIS_FIELD_SELECT = 1,
    V5_AXIS_FIELD_ACTION = 2,
    V5_AXIS_FIELD_READONLY = 3
};

static const V5SettingsAxisRowSpec kAxisRows[] = {
    {"X", "X 直线", "直线", 1},
    {"Y", "Y 直线", "直线", 1},
    {"Z", "Z 直线", "直线", 1},
    {"A", "A 旋转", "旋转", 1},
    {"B", "B 旋转", "旋转", 1},
    {"C", "C 旋转", "旋转", 1},
    {"GANTRY", "龙门", "从轴", 1},
    {"TOOLMAG", "刀库", "刀库", 1},
};


#define V5_AXIS_TABLE_MAX_ROWS 8U
#define V5_AXIS_TABLE_MAX_COLS 19U
#define V5_AXIS_VALUE_CAP 32U
#define V5_G53_ROW_COUNT 5U
#define V5_SLAVE_OPTION_MAX 32U
#define V5_SLAVE_OPTION_CAP 128U
#define V5_DROPDOWN_OPTIONS_CAP 1024U
#define V5_RESIDENT_PARAMETER_MAX_ROWS 192U
#define V5_RESIDENT_PARAMETER_KEY_CAP 96U
#define V5_RESIDENT_PARAMETER_VALUE_CAP 512U

typedef struct V5AxisIniRow {
    int axis_seen;
    int joint_seen;
    char type[16];
    char direction_mode[24];
    char min_limit[24];
    char max_limit[24];
    char max_velocity[24];
    char max_acceleration[24];
    char pitch[24];
    char motor_rev[24];
    char load_rev[24];
    char scale[24];
    char home[24];
    char home_sequence[24];
    char home_search_vel[24];
    char backlash[24];
} V5AxisIniRow;

typedef struct V5ResidentParameterRow {
    char axis[V5_RESIDENT_PARAMETER_KEY_CAP];
    char field[V5_RESIDENT_PARAMETER_KEY_CAP];
    char value[V5_RESIDENT_PARAMETER_VALUE_CAP];
} V5ResidentParameterRow;

static char g_values[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS][V5_AXIS_VALUE_CAP];
static unsigned char g_value_real[V5_AXIS_TABLE_MAX_ROWS][V5_AXIS_TABLE_MAX_COLS];
static int g_loaded;
static char g_g53_values[V5_G53_ROW_COUNT][3U][V5_AXIS_VALUE_CAP];
static unsigned char g_g53_value_real[V5_G53_ROW_COUNT][3U];
static char g_motion_model_value[V5_AXIS_VALUE_CAP];
static unsigned char g_motion_model_real;
static char g_bus_pulse_value[V5_AXIS_VALUE_CAP];
static unsigned char g_bus_pulse_real;
static char g_slave_options[V5_SLAVE_OPTION_MAX][V5_SLAVE_OPTION_CAP];
static unsigned int g_slave_option_count;
static char g_project_root[256] = ".";
static char g_self_parameter_table_text[V5_BOOT_CLOSURE_TEXT_CAP];
static char g_drive_parameter_table_text[V5_BOOT_CLOSURE_TEXT_CAP];

typedef struct V5AxisCellRef {
    unsigned int kind;
    unsigned int row;
    unsigned int col;
    lv_obj_t *obj;
    lv_obj_t *label;
    char field_id[96];
} V5AxisCellRef;

enum {
    V5_CELL_KIND_AXIS = 0U,
    V5_CELL_KIND_G53 = 1U
};

#define V5_AXIS_CELL_REF_MAX 192U
static V5AxisCellRef g_cell_refs[V5_AXIS_CELL_REF_MAX];
static unsigned int g_cell_ref_count;
static lv_obj_t *g_keyboard_overlay;
static lv_obj_t *g_keyboard_value_label;
static V5AxisCellRef *g_keyboard_ref;
static char g_keyboard_value[64];
static V5UiFirstFrameGuard g_keyboard_frame_guard;
static lv_obj_t *g_axis_scroll;


static void trim_in_place(char *text)
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

static int same_key(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static unsigned int axis_letter_index(char axis)
{
    switch (axis) {
    case 'X': return 0U;
    case 'Y': return 1U;
    case 'Z': return 2U;
    case 'A': return 3U;
    case 'B': return 4U;
    case 'C': return 5U;
    default: return V5_AXIS_TABLE_MAX_ROWS;
    }
}

static int column_index(const char *field_key)
{
    unsigned int i;
    for (i = 0U; i < v5_settings_axis_table_column_count(); ++i) {
        if (same_key(kAxisColumns[i].field_key, field_key)) {
            return (int)i;
        }
    }
    return -1;
}

static void set_value(unsigned int row, const char *field_key, const char *value, int real)
{
    int c = column_index(field_key);
    if (row >= V5_AXIS_TABLE_MAX_ROWS || c < 0 || !value || !value[0]) {
        return;
    }
    snprintf(g_values[row][(unsigned int)c], V5_AXIS_VALUE_CAP, "%s", value);
    g_value_real[row][(unsigned int)c] = real ? 1U : 0U;
}

static void set_value_from_double(unsigned int row, const char *field_key, double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", value);
    for (char *p = buf + strlen(buf); p > buf && p[-1] == '0'; --p) {
        p[-1] = '\0';
    }
    if (buf[strlen(buf) - 1U] == '.') {
        buf[strlen(buf) - 1U] = '\0';
    }
    set_value(row, field_key, buf, 1);
}

static void set_max_velocity_from_runtime_ini(unsigned int row, const char *value)
{
    char *end;
    double units_per_s;
    double display_value;
    double rounded;
    double delta;
    if (!value || !value[0]) {
        return;
    }
    units_per_s = strtod(value, &end);
    if (end == value || *end != '\0' || !isfinite(units_per_s)) {
        return;
    }
    display_value = units_per_s * 60.0;
    rounded = (double)((long long)(display_value >= 0.0 ? display_value + 0.5 : display_value - 0.5));
    delta = display_value - rounded;
    if (delta < 0.0) {
        delta = -delta;
    }
    if (delta < 0.0001) {
        display_value = rounded;
    }
    set_value_from_double(row, "max_velocity", display_value);
}

static void clear_slave_options(void)
{
    unsigned int r;
    g_slave_option_count = 0U;
    for (r = 0U; r < V5_SLAVE_OPTION_MAX; ++r) {
        g_slave_options[r][0] = '\0';
    }
}


static V5SettingsAxisCommitCallback g_commit_callback;
static void *g_commit_callback_user_data;
static V5SettingsAxisZeroCallback g_axis_zero_callback;
static void *g_axis_zero_callback_user_data;

void v5_settings_axis_table_set_commit_callback(V5SettingsAxisCommitCallback cb, void *user_data)
{
    g_commit_callback = cb;
    g_commit_callback_user_data = user_data;
}

void v5_settings_axis_table_set_axis_zero_callback(V5SettingsAxisZeroCallback cb, void *user_data)
{
    g_axis_zero_callback = cb;
    g_axis_zero_callback_user_data = user_data;
}

static void notify_settings_axis_commit_success(void)
{
    if (g_commit_callback) {
        g_commit_callback(g_commit_callback_user_data);
    }
}

static void clear_values(void)
{
    unsigned int r;
    unsigned int c;
    snprintf(g_motion_model_value, V5_AXIS_VALUE_CAP, "--");
    g_motion_model_real = 0U;
    snprintf(g_bus_pulse_value, V5_AXIS_VALUE_CAP, "--");
    g_bus_pulse_real = 0U;
    clear_slave_options();
    for (r = 0U; r < V5_G53_ROW_COUNT; ++r) {
        for (c = 0U; c < 3U; ++c) {
            snprintf(g_g53_values[r][c], V5_AXIS_VALUE_CAP, "--");
            g_g53_value_real[r][c] = 0U;
        }
    }
    for (r = 0U; r < V5_AXIS_TABLE_MAX_ROWS; ++r) {
        for (c = 0U; c < V5_AXIS_TABLE_MAX_COLS; ++c) {
            snprintf(g_values[r][c], V5_AXIS_VALUE_CAP, "--");
            g_value_real[r][c] = 0U;
        }
    }
    g_loaded = 0;
}

static void copy_axis_value(char *dst, size_t cap, const char *value)
{
    if (!dst || cap == 0U || !value || !value[0]) {
        return;
    }
    snprintf(dst, cap, "%s", value);
}

static int next_text_line(const char **cursor, char *line, size_t cap)
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
        return g_self_parameter_table_text;
    case V5_SETTINGS_PARAMETER_DISK_DRIVE:
        return g_drive_parameter_table_text;
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

static unsigned int settings_axis_owner_generation(const char *field_id)
{
    uint32_t hash = 2166136261U;
    hash = settings_fnv1a_update(hash, g_project_root);
    hash = settings_fnv1a_update(hash, resident_parameter_table_text(V5_SETTINGS_PARAMETER_DISK_SELF));
    hash = settings_fnv1a_update(hash, resident_parameter_table_text(V5_SETTINGS_PARAMETER_DISK_DRIVE));
    hash = settings_fnv1a_update(hash, field_id);
    return settings_nonzero_generation(hash);
}

static unsigned int settings_axis_readback_token(const char *field_id, const char *value)
{
    uint32_t hash = 2166136261U;
    hash = settings_fnv1a_update(hash, field_id);
    hash = settings_fnv1a_update(hash, value);
    return settings_nonzero_generation(hash);
}

static void set_resident_parameter_table(V5SettingsParameterDiskTable table, const V5BootClosureResidentText *blob)
{
    char *dst = resident_parameter_table_text(table);
    if (!dst) return;
    dst[0] = '\0';
    if (blob && blob->loaded && blob->text[0]) {
        snprintf(dst, V5_BOOT_CLOSURE_TEXT_CAP, "%s", blob->text);
    }
}

static int resident_parameter_table_read_axis(V5SettingsParameterDiskTable table,
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
    while (next_text_line(&cursor, line, sizeof(line))) {
        char *line_axis;
        char *line_field;
        char *line_value;
        trim_in_place(line);
        if (!line[0] || line[0] == '#') continue;
        line_axis = line;
        line_field = strchr(line_axis, '\t');
        if (!line_field) continue;
        *line_field++ = '\0';
        line_value = strchr(line_field, '\t');
        if (!line_value) continue;
        *line_value++ = '\0';
        trim_in_place(line_axis);
        trim_in_place(line_field);
        trim_in_place(line_value);
        if (strcmp(line_axis, axis) == 0 && strcmp(line_field, field_key) == 0 && line_value[0]) {
            snprintf(out, out_cap, "%s", line_value);
            found = 1;
        }
    }
    return found && out[0];
}

static int resident_parameter_table_append_rewrite_line(char *out,
                                                        size_t out_cap,
                                                        size_t *used,
                                                        const char *axis,
                                                        const char *field_key,
                                                        const char *value)
{
    int n;
    if (!out || out_cap == 0U || !used || !axis || !field_key || !value || *used >= out_cap) {
        return 0;
    }
    n = snprintf(out + *used, out_cap - *used, "%s\t%s\t%s\n", axis, field_key, value);
    if (n <= 0 || (size_t)n >= out_cap - *used) return 0;
    *used += (size_t)n;
    return 1;
}

static int resident_parameter_table_upsert_write(V5SettingsParameterDiskTable table,
                                                 const char *axis,
                                                 const char *field_key,
                                                 const char *value)
{
    char *text_blob = resident_parameter_table_text(table);
    V5ResidentParameterRow *rows;
    char *rewritten;
    const char *cursor;
    char line[1024];
    size_t count = 0U;
    size_t i;
    size_t used = 0U;
    int found = 0;
    int ok = 0;
    int n;
    if (!text_blob || !text_blob[0] || !axis || !axis[0] || !field_key || !field_key[0] || !value || !value[0]) {
        return 0;
    }
    rows = (V5ResidentParameterRow *)calloc(V5_RESIDENT_PARAMETER_MAX_ROWS, sizeof(*rows));
    rewritten = (char *)malloc(V5_BOOT_CLOSURE_TEXT_CAP);
    if (!rows || !rewritten) goto done;

    cursor = text_blob;
    while (next_text_line(&cursor, line, sizeof(line))) {
        char *line_axis;
        char *line_field;
        char *line_value;
        int is_target;
        int replaced = 0;
        trim_in_place(line);
        if (!line[0] || line[0] == '#') continue;
        line_axis = line;
        line_field = strchr(line_axis, '\t');
        if (!line_field) continue;
        *line_field++ = '\0';
        line_value = strchr(line_field, '\t');
        if (!line_value) continue;
        *line_value++ = '\0';
        if (strchr(line_value, '\t')) continue;
        trim_in_place(line_axis);
        trim_in_place(line_field);
        trim_in_place(line_value);
        if (!line_axis[0] || !line_field[0] || !line_value[0]) continue;
        is_target = (strcmp(line_axis, axis) == 0 && strcmp(line_field, field_key) == 0);
        for (i = 0U; i < count; ++i) {
            if (strcmp(rows[i].axis, line_axis) == 0 && strcmp(rows[i].field, line_field) == 0) {
                snprintf(rows[i].value, sizeof(rows[i].value), "%s", is_target ? value : line_value);
                replaced = 1;
                break;
            }
        }
        if (is_target) found = 1;
        if (replaced) continue;
        if (count >= V5_RESIDENT_PARAMETER_MAX_ROWS) continue;
        snprintf(rows[count].axis, sizeof(rows[count].axis), "%s", line_axis);
        snprintf(rows[count].field, sizeof(rows[count].field), "%s", line_field);
        snprintf(rows[count].value, sizeof(rows[count].value), "%s", is_target ? value : line_value);
        ++count;
    }
    if (!found) goto done;

    n = snprintf(rewritten, V5_BOOT_CLOSURE_TEXT_CAP, "# schema=v5.settings.parameter_table.tsv.v1\n");
    if (n <= 0 || (size_t)n >= V5_BOOT_CLOSURE_TEXT_CAP) goto done;
    used = (size_t)n;
    for (i = 0U; i < count; ++i) {
        if (!resident_parameter_table_append_rewrite_line(rewritten,
                                                          V5_BOOT_CLOSURE_TEXT_CAP,
                                                          &used,
                                                          rows[i].axis,
                                                          rows[i].field,
                                                          rows[i].value)) {
            goto done;
        }
    }
    memcpy(text_blob, rewritten, used + 1U);
    ok = 1;

done:
    free(rewritten);
    free(rows);
    return ok;
}

static int row_index_for_axis_name(const char *axis)
{
    unsigned int i;
    if (!axis || !axis[0]) return -1;
    for (i = 0U; i < v5_settings_axis_table_row_count(); ++i) {
        if (strcmp(kAxisRows[i].axis, axis) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void trim_ascii_in_place(char *text)
{
    char *start;
    char *end;
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

static void slave_option_extract_id(const char *text, char *out, size_t cap)
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

static int slave_id_is_nat(const char *id)
{
    return id && strcmp(id, "NAT") == 0;
}

static int slave_option_is_nat(const char *text)
{
    char id[V5_AXIS_VALUE_CAP];
    slave_option_extract_id(text, id, sizeof(id));
    return slave_id_is_nat(id);
}

static int slave_option_same_id(const char *a, const char *b)
{
    char id_a[V5_AXIS_VALUE_CAP];
    char id_b[V5_AXIS_VALUE_CAP];
    slave_option_extract_id(a, id_a, sizeof(id_a));
    slave_option_extract_id(b, id_b, sizeof(id_b));
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
    slave_option_extract_id(raw, id, sizeof(id));
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
    for (i = 0U; i < g_slave_option_count; ++i) {
        if (strcmp(g_slave_options[i], label) == 0 || slave_option_same_id(g_slave_options[i], label)) return;
    }
    if (g_slave_option_count >= V5_SLAVE_OPTION_MAX) return;
    snprintf(g_slave_options[g_slave_option_count], V5_SLAVE_OPTION_CAP, "%s", label);
    ++g_slave_option_count;
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

static void load_slave_options_from_resident_self_table(void)
{
    char value[256];
    if (resident_parameter_table_read_axis(V5_SETTINGS_PARAMETER_DISK_SELF,
                                           "SETTINGS", "slave_options", value, sizeof(value))) {
        add_slave_options_from_text(value);
    }
}

static void reload_slave_options_from_resident_self_table(void)
{
    clear_slave_options();
    load_slave_options_from_resident_self_table();
}

static void set_g53_value(unsigned int row, unsigned int col, const char *value, int real)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U || !value || !value[0]) {
        return;
    }
    snprintf(g_g53_values[row][col], V5_AXIS_VALUE_CAP, "%s", value);
    g_g53_value_real[row][col] = real ? 1U : 0U;
}

static int g53_value_is_modal_owner(unsigned int row, unsigned int col)
{
    return (row < 3U && row == col) ? 1 : 0;
}

static int g53_value_is_native_geometry_owner(unsigned int row, unsigned int col)
{
    if (row >= 3U || col >= 3U) return 0;
    return !g53_value_is_modal_owner(row, col);
}

int v5_settings_axis_table_g53_value_is_editable(unsigned int row, unsigned int col)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U) return 0;
    return row >= 3U || g53_value_is_native_geometry_owner(row, col);
}

static const char *g53_field_key(unsigned int row, unsigned int col)
{
    static const char *keys[V5_G53_ROW_COUNT][3U] = {
        {"modal_wcs_x_offset", "g53_a_y", "g53_a_z"},
        {"g53_b_x", "modal_wcs_y_offset", "g53_b_z"},
        {"g53_c_x", "g53_c_y", "modal_wcs_z_offset"},
        {"tool_setter_x", "tool_setter_y", "tool_setter_z"},
        {"five_direction_detector_x", "five_direction_detector_y", "five_direction_detector_z"},
    };
    if (row >= V5_G53_ROW_COUNT || col >= 3U) return 0;
    return keys[row][col];
}

static V5SettingsParameterDiskTable g53_disk_table(unsigned int row, unsigned int col)
{
    (void)col;
    if (row >= 3U) return V5_SETTINGS_PARAMETER_DISK_SELF;
    return V5_SETTINGS_PARAMETER_DISK_NONE;
}

static int strings_equal_numeric_ok(const char *a, const char *b);

static const char *g53_runtime_ini_key(unsigned int row, unsigned int col)
{
    if (!g53_value_is_native_geometry_owner(row, col)) return 0;
    if (row == 0U && col == 1U) return "G53_A_Y";
    if (row == 0U && col == 2U) return "G53_A_Z";
    if (row == 1U && col == 0U) return "G53_B_X";
    if (row == 1U && col == 2U) return "G53_B_Z";
    if (row == 2U && col == 0U) return "G53_C_X";
    if (row == 2U && col == 1U) return "G53_C_Y";
    return 0;
}

static int build_project_path(char *out, size_t out_cap, const char *rel)
{
    int n;
    if (!out || out_cap == 0U || !rel || !rel[0]) return 0;
    n = snprintf(out, out_cap, "%s/%s", g_project_root[0] ? g_project_root : ".", rel);
    return n > 0 && (size_t)n < out_cap;
}

static int value_text_is_numeric_ini_value(const char *value)
{
    char buf[64];
    char *end;
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) return 0;
    snprintf(buf, sizeof(buf), "%s", value);
    trim_in_place(buf);
    if (!buf[0]) return 0;
    (void)strtod(buf, &end);
    while (*end && isspace((unsigned char)*end)) {
        ++end;
    }
    return *end == '\0';
}

static int ini_probe_section(const char *raw, char *section, size_t section_cap)
{
    char probe[256];
    if (!raw || !section || section_cap == 0U) return 0;
    snprintf(probe, sizeof(probe), "%s", raw);
    trim_in_place(probe);
    if (probe[0] != '[') return 0;
    section[0] = '\0';
    return sscanf(probe, "[%31[^]]]", section) == 1;
}

static int ini_probe_key_matches(const char *raw, const char *key)
{
    char probe[256];
    char *eq;
    if (!raw || !key || !key[0]) return 0;
    snprintf(probe, sizeof(probe), "%s", raw);
    trim_in_place(probe);
    if (!probe[0] || probe[0] == '#' || probe[0] == ';') return 0;
    eq = strchr(probe, '=');
    if (!eq) return 0;
    *eq = '\0';
    trim_in_place(probe);
    return same_key(probe, key);
}

static void runtime_ini_write_key(FILE *out, const char *ini_key, const char *value, int *last_had_newline)
{
    if (!out || !ini_key || !value) return;
    if (last_had_newline && !*last_had_newline) {
        fputc('\n', out);
    }
    fprintf(out, "%s = %s\n", ini_key, value);
    if (last_had_newline) {
        *last_had_newline = 1;
    }
}

static int value_text_is_safe_ini_value(const char *value)
{
    char buf[64];
    if (!value || !value[0] || strchr(value, '\n') || strchr(value, '\r')) return 0;
    snprintf(buf, sizeof(buf), "%s", value);
    trim_in_place(buf);
    return buf[0] && strchr(buf, '[') == 0 && strchr(buf, ']') == 0;
}

static int runtime_ini_read_section_value(const char *section_name, const char *ini_key, char *out, size_t out_cap)
{
    char path[512];
    char raw[512];
    int in_section = 0;
    FILE *fp;
    if (!section_name || !section_name[0] || !ini_key || !out || out_cap == 0U || !build_project_path(path, sizeof(path), "linuxcnc/ini/v5_bus.ini")) return 0;
    out[0] = '\0';
    fp = fopen(path, "rb");
    if (!fp) return 0;
    while (fgets(raw, sizeof(raw), fp)) {
        char section[32];
        if (ini_probe_section(raw, section, sizeof(section))) {
            in_section = same_key(section, section_name);
            continue;
        }
        if (in_section && ini_probe_key_matches(raw, ini_key)) {
            char *eq = strchr(raw, '=');
            if (!eq) {
                fclose(fp);
                return 0;
            }
            snprintf(out, out_cap, "%s", eq + 1);
            trim_in_place(out);
            fclose(fp);
            return out[0] != '\0';
        }
    }
    fclose(fp);
    return 0;
}

static int runtime_ini_write_section_value(const char *section_name, const char *ini_key, const char *value, int numeric_required, char *readback, size_t readback_cap)
{
    char path[512];
    char tmp_path[544];
    char raw[512];
    FILE *in;
    FILE *out;
    int in_section = 0;
    int saw_section = 0;
    int wrote_key = 0;
    int last_had_newline = 1;
    if (!section_name || !section_name[0] || !ini_key || !ini_key[0] ||
        (numeric_required ? !value_text_is_numeric_ini_value(value) : !value_text_is_safe_ini_value(value)) ||
        !build_project_path(path, sizeof(path), "linuxcnc/ini/v5_bus.ini")) {
        return 0;
    }
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) <= 0 || strlen(tmp_path) >= sizeof(tmp_path)) {
        return 0;
    }
    in = fopen(path, "rb");
    if (!in) return 0;
    out = fopen(tmp_path, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while (fgets(raw, sizeof(raw), in)) {
        char section[32];
        size_t len = strlen(raw);
        if (ini_probe_section(raw, section, sizeof(section))) {
            if (in_section && !wrote_key) {
                runtime_ini_write_key(out, ini_key, value, &last_had_newline);
                wrote_key = 1;
            }
            in_section = same_key(section, section_name);
            if (in_section) saw_section = 1;
        }
        if (in_section && ini_probe_key_matches(raw, ini_key)) {
            runtime_ini_write_key(out, ini_key, value, &last_had_newline);
            wrote_key = 1;
            continue;
        }
        fputs(raw, out);
        last_had_newline = (len == 0U || raw[len - 1U] == '\n') ? 1 : 0;
    }
    if (saw_section && in_section && !wrote_key) {
        runtime_ini_write_key(out, ini_key, value, &last_had_newline);
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
    if (!runtime_ini_read_section_value(section_name, ini_key, readback, readback_cap)) return 0;
    return numeric_required ? strings_equal_numeric_ok(readback, value) : strcmp(readback, value) == 0;
}

static int runtime_ini_read_rtcp_value(const char *ini_key, char *out, size_t out_cap)
{
    return runtime_ini_read_section_value("RTCP", ini_key, out, out_cap);
}

static int runtime_ini_write_rtcp_value(const char *ini_key, const char *value, char *readback, size_t readback_cap)
{
    return runtime_ini_write_section_value("RTCP", ini_key, value, 1, readback, readback_cap);
}

static void format_double_compact(char *out, size_t cap, double value)
{
    if (!out || cap == 0U) return;
    snprintf(out, cap, "%.12g", value);
}

static int g53_field_id(unsigned int row, unsigned int col, char *out, size_t out_cap)
{
    const char *key = g53_field_key(row, col);
    if (!key || !out || out_cap == 0U) return 0;
    snprintf(out, out_cap, "g53_%s", key);
    return 1;
}

static void load_g53_disk_parameter_tables(void)
{
    unsigned int r;
    unsigned int c;
    char value[V5_AXIS_VALUE_CAP];
    for (r = 0U; r < V5_G53_ROW_COUNT; ++r) {
        for (c = 0U; c < 3U; ++c) {
            const char *key;
            if (!v5_settings_axis_table_g53_value_is_editable(r, c)) continue;
            if (g53_value_is_native_geometry_owner(r, c)) continue;
            key = g53_field_key(r, c);
            if (key && resident_parameter_table_read_axis(g53_disk_table(r, c), "G53", key, value, sizeof(value))) {
                set_g53_value(r, c, value, 1);
            }
        }
    }
}

static void set_motion_model_value(const char *value, int real)
{
    if (!value || !value[0]) {
        return;
    }
    snprintf(g_motion_model_value, V5_AXIS_VALUE_CAP, "%s", value);
    g_motion_model_real = real ? 1U : 0U;
}

static void set_bus_pulse_value(const char *value, int real)
{
    if (!value || !value[0]) {
        return;
    }
    snprintf(g_bus_pulse_value, V5_AXIS_VALUE_CAP, "%s", value);
    g_bus_pulse_real = real ? 1U : 0U;
}

static void load_self_setting_parameter_table(void)
{
    char value[V5_AXIS_VALUE_CAP];
    if (resident_parameter_table_read_axis(V5_SETTINGS_PARAMETER_DISK_SELF,
                                           "SETTINGS", "bus_pulse_setting", value, sizeof(value))) {
        set_bus_pulse_value(value, 1);
    }
    load_slave_options_from_resident_self_table();
}

static void parse_runtime_ini_text(const char *text)
{
    const char *cursor;
    char line[256];
    int current_axis = -1;
    int current_joint = -1;
    int current_rtcp = 0;
    int current_traj = 0;
    int joint_axis_map[V5_AXIS_TABLE_MAX_ROWS];
    V5AxisIniRow rows[V5_AXIS_TABLE_MAX_ROWS];
    unsigned int i;

    if (!text || !text[0]) return;
    memset(rows, 0, sizeof(rows));
    for (i = 0U; i < V5_AXIS_TABLE_MAX_ROWS; ++i) {
        joint_axis_map[i] = (int)i;
    }
    cursor = text;
    while (next_text_line(&cursor, line, sizeof(line))) {
        char *eq;
        trim_in_place(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            char section[32];
            char axis = '\0';
            int joint = -1;
            current_axis = -1;
            current_joint = -1;
            current_rtcp = 0;
            current_traj = 0;
            section[0] = '\0';
            if (sscanf(line, "[%31[^]]]", section) == 1) {
                if (strcmp(section, "RTCP") == 0) {
                    current_rtcp = 1;
                } else if (strcmp(section, "TRAJ") == 0) {
                    current_traj = 1;
                } else if (sscanf(section, "AXIS_%c", &axis) == 1 && strlen(section) == 6U) {
                    unsigned int idx = axis_letter_index(axis);
                    if (idx < 6U) {
                        current_axis = (int)idx;
                        rows[idx].axis_seen = 1;
                    }
                } else if (strcmp(section, "AXIS_GANTRY") == 0) {
                    current_axis = 6;
                    rows[6].axis_seen = 1;
                } else if (strcmp(section, "AXIS_TOOLMAG") == 0) {
                    current_axis = 7;
                    rows[7].axis_seen = 1;
                } else if (sscanf(section, "JOINT_%d", &joint) == 1 && joint >= 0 && joint < (int)V5_AXIS_TABLE_MAX_ROWS) {
                    current_joint = joint_axis_map[joint];
                    if (current_joint >= 0 && current_joint < (int)V5_AXIS_TABLE_MAX_ROWS) {
                        rows[current_joint].joint_seen = 1;
                    } else {
                        current_joint = -1;
                    }
                }
            }
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        trim_in_place(line);
        trim_in_place(eq + 1);
        if (current_traj) {
            if (same_key(line, "COORDINATES")) {
                unsigned int pos = 0U;
                const char *p = eq + 1;
                while (*p && pos < V5_AXIS_TABLE_MAX_ROWS) {
                    if (isalpha((unsigned char)*p)) {
                        unsigned int idx = axis_letter_index((char)toupper((unsigned char)*p));
                        if (idx < V5_AXIS_TABLE_MAX_ROWS) {
                            joint_axis_map[pos++] = (int)idx;
                        }
                    }
                    ++p;
                }
            }
        } else if (current_axis >= 0) {
            V5AxisIniRow *row = &rows[current_axis];
            if (same_key(line, "TYPE")) copy_axis_value(row->type, sizeof(row->type), eq + 1);
            else if (same_key(line, "DIRECTION_MODE")) copy_axis_value(row->direction_mode, sizeof(row->direction_mode), eq + 1);
            else if (same_key(line, "MIN_LIMIT")) copy_axis_value(row->min_limit, sizeof(row->min_limit), eq + 1);
            else if (same_key(line, "MAX_LIMIT")) copy_axis_value(row->max_limit, sizeof(row->max_limit), eq + 1);
            else if (same_key(line, "MAX_VELOCITY")) copy_axis_value(row->max_velocity, sizeof(row->max_velocity), eq + 1);
            else if (same_key(line, "MAX_ACCELERATION")) copy_axis_value(row->max_acceleration, sizeof(row->max_acceleration), eq + 1);
            else if (same_key(line, "PITCH")) copy_axis_value(row->pitch, sizeof(row->pitch), eq + 1);
            else if (same_key(line, "MOTOR_REV")) copy_axis_value(row->motor_rev, sizeof(row->motor_rev), eq + 1);
            else if (same_key(line, "LOAD_REV")) copy_axis_value(row->load_rev, sizeof(row->load_rev), eq + 1);
            else if (same_key(line, "SCALE")) copy_axis_value(row->scale, sizeof(row->scale), eq + 1);
            else if (same_key(line, "HOME")) copy_axis_value(row->home, sizeof(row->home), eq + 1);
            else if (same_key(line, "HOME_SEQUENCE")) copy_axis_value(row->home_sequence, sizeof(row->home_sequence), eq + 1);
            else if (same_key(line, "HOME_SEARCH_VEL")) copy_axis_value(row->home_search_vel, sizeof(row->home_search_vel), eq + 1);
            else if (same_key(line, "BACKLASH")) copy_axis_value(row->backlash, sizeof(row->backlash), eq + 1);
        } else if (current_joint >= 0) {
            V5AxisIniRow *row = &rows[current_joint];
            if (same_key(line, "TYPE") && !row->type[0]) copy_axis_value(row->type, sizeof(row->type), eq + 1);
            else if (same_key(line, "DIRECTION_MODE") && !row->direction_mode[0]) copy_axis_value(row->direction_mode, sizeof(row->direction_mode), eq + 1);
            else if (same_key(line, "MIN_LIMIT") && !row->min_limit[0]) copy_axis_value(row->min_limit, sizeof(row->min_limit), eq + 1);
            else if (same_key(line, "MAX_LIMIT") && !row->max_limit[0]) copy_axis_value(row->max_limit, sizeof(row->max_limit), eq + 1);
            else if (same_key(line, "MAX_VELOCITY") && !row->max_velocity[0]) copy_axis_value(row->max_velocity, sizeof(row->max_velocity), eq + 1);
            else if (same_key(line, "MAX_ACCELERATION") && !row->max_acceleration[0]) copy_axis_value(row->max_acceleration, sizeof(row->max_acceleration), eq + 1);
            else if (same_key(line, "PITCH")) copy_axis_value(row->pitch, sizeof(row->pitch), eq + 1);
            else if (same_key(line, "MOTOR_REV")) copy_axis_value(row->motor_rev, sizeof(row->motor_rev), eq + 1);
            else if (same_key(line, "LOAD_REV")) copy_axis_value(row->load_rev, sizeof(row->load_rev), eq + 1);
            else if (same_key(line, "SCALE")) copy_axis_value(row->scale, sizeof(row->scale), eq + 1);
            else if (same_key(line, "HOME")) copy_axis_value(row->home, sizeof(row->home), eq + 1);
            else if (same_key(line, "HOME_SEQUENCE")) copy_axis_value(row->home_sequence, sizeof(row->home_sequence), eq + 1);
            else if (same_key(line, "HOME_SEARCH_VEL")) copy_axis_value(row->home_search_vel, sizeof(row->home_search_vel), eq + 1);
            else if (same_key(line, "BACKLASH")) copy_axis_value(row->backlash, sizeof(row->backlash), eq + 1);
        } else if (current_rtcp) {
            if (same_key(line, "MOTION_MODEL") || same_key(line, "MODEL")) set_motion_model_value(eq + 1, 1);
            else if (same_key(line, "G53_A_Y")) set_g53_value(0U, 1U, eq + 1, 1);
            else if (same_key(line, "G53_A_Z")) set_g53_value(0U, 2U, eq + 1, 1);
            else if (same_key(line, "G53_B_X")) set_g53_value(1U, 0U, eq + 1, 1);
            else if (same_key(line, "G53_B_Z")) set_g53_value(1U, 2U, eq + 1, 1);
            else if (same_key(line, "G53_C_X")) set_g53_value(2U, 0U, eq + 1, 1);
            else if (same_key(line, "G53_C_Y")) set_g53_value(2U, 1U, eq + 1, 1);
        }
    }

    for (i = 0U; i < V5_AXIS_TABLE_MAX_ROWS; ++i) {
        V5AxisIniRow *row = &rows[i];
        if (row->type[0]) {
            set_value(i, "axis_mode", strcmp(row->type, "ANGULAR") == 0 ? "旋转" : (strcmp(row->type, "VIRTUAL") == 0 ? "虚拟" : "直线"), 1);
        }
        if (row->direction_mode[0]) set_value(i, "direction_mode", row->direction_mode, 1);
        if (row->scale[0]) {
            double scale = strtod(row->scale, 0);
            if (scale > 0.0) {
                set_value_from_double(i, "precision", 1.0 / scale);
            }
        }
        if (row->pitch[0]) set_value(i, "pitch", row->pitch, 1);
        if (row->motor_rev[0]) set_value(i, "motor_rev", row->motor_rev, 1);
        if (row->load_rev[0]) set_value(i, "load_rev", row->load_rev, 1);
        if (row->home_sequence[0]) set_value(i, "home_order", strcmp(row->home_sequence, "-1") == 0 ? "禁用" : row->home_sequence, 1);
        if (row->home_search_vel[0]) {
            double vel = strtod(row->home_search_vel, 0);
            if (vel > 0.0) set_value(i, "home_direction", "+", 1);
            else if (vel < 0.0) set_value(i, "home_direction", "-", 1);
            else set_value(i, "home_direction", "0", 1);
        }
        if (row->min_limit[0]) set_value(i, "soft_minus", row->min_limit, 1);
        if (row->home[0]) set_value(i, "zero", row->home, 1);
        if (row->max_limit[0]) set_value(i, "soft_plus", row->max_limit, 1);
        if (row->max_velocity[0]) set_max_velocity_from_runtime_ini(i, row->max_velocity);
        if (row->max_acceleration[0]) set_value(i, "max_acceleration", row->max_acceleration, 1);
        if (row->backlash[0]) set_value(i, "backlash", row->backlash, 1);
    }
}

static void mark_missing_encoder_bits_unavailable(void)
{
    int col = column_index("encoder_bits");
    unsigned int r;
    if (col < 0) return;
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        if (!g_value_real[r][(unsigned int)col]) {
            set_value(r, "encoder_bits", "--", 0);
        }
    }
}


static const char *row_value(unsigned int row, const char *field_key)
{
    int c = column_index(field_key);
    if (row >= V5_AXIS_TABLE_MAX_ROWS || c < 0) {
        return 0;
    }
    return g_value_real[row][(unsigned int)c] ? g_values[row][(unsigned int)c] : 0;
}

static int row_slave_matches_option(unsigned int row, const char *option)
{
    char option_id[V5_AXIS_VALUE_CAP];
    char value_id[V5_AXIS_VALUE_CAP];
    if (row >= v5_settings_axis_table_row_count()) return 0;
    slave_option_extract_id(option, option_id, sizeof(option_id));
    if (!option_id[0] || slave_id_is_nat(option_id)) return 0;
    slave_option_extract_id(row_value(row, "slave"), value_id, sizeof(value_id));
    return value_id[0] && !slave_id_is_nat(value_id) && strcmp(option_id, value_id) == 0;
}

static int slave_option_used_by_other_row(unsigned int current_row, const char *option)
{
    unsigned int row;
    for (row = 0U; row < v5_settings_axis_table_row_count(); ++row) {
        if (row == current_row) continue;
        if (row_slave_matches_option(row, option)) {
            return 1;
        }
    }
    return 0;
}

static V5SettingsParameterDiskTable disk_table_for_column(const V5SettingsAxisColumnSpec *col)
{
    if (!col) return V5_SETTINGS_PARAMETER_DISK_NONE;
    if (col->drive_only_allowed || col->owner == V5_PARAMETER_OWNER_DRIVE_ONLY) {
        return V5_SETTINGS_PARAMETER_DISK_DRIVE;
    }
    if (col->owner == V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE) {
        return V5_SETTINGS_PARAMETER_DISK_SELF;
    }
    return V5_SETTINGS_PARAMETER_DISK_NONE;
}

static int should_read_disk_table_for_column(V5SettingsParameterDiskTable table, const V5SettingsAxisColumnSpec *col)
{
    if (!col || !col->field_key) return 0;
    if (strcmp(col->field_key, "slave") == 0) return table == V5_SETTINGS_PARAMETER_DISK_SELF;
    if (strcmp(col->field_key, "encoder_bits") == 0) return table == V5_SETTINGS_PARAMETER_DISK_DRIVE;
    return disk_table_for_column(col) == table;
}

static void load_disk_parameter_table(V5SettingsParameterDiskTable table)
{
    unsigned int r;
    unsigned int c;
    char value[V5_AXIS_VALUE_CAP];
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        for (c = 0U; c < v5_settings_axis_table_column_count(); ++c) {
            const V5SettingsAxisColumnSpec *col = &kAxisColumns[c];
            if (!should_read_disk_table_for_column(table, col)) {
                continue;
            }
            if (resident_parameter_table_read_axis(table, kAxisRows[r].axis, col->field_key, value, sizeof(value))) {
                set_value(r, col->field_key, value, 1);
            }
        }
    }
}

void v5_settings_axis_table_load_boot_closure(const V5BootClosure *closure)
{
    if (closure && closure->project_root[0]) {
        snprintf(g_project_root, sizeof(g_project_root), "%s", closure->project_root);
    } else {
        snprintf(g_project_root, sizeof(g_project_root), ".");
    }
    clear_values();
    set_resident_parameter_table(V5_SETTINGS_PARAMETER_DISK_SELF, closure ? &closure->self_parameter_table : 0);
    set_resident_parameter_table(V5_SETTINGS_PARAMETER_DISK_DRIVE, closure ? &closure->drive_parameter_table : 0);
    if (closure && closure->runtime_ini.loaded) {
        parse_runtime_ini_text(closure->runtime_ini.text);
    }
    load_self_setting_parameter_table();
    load_disk_parameter_table(V5_SETTINGS_PARAMETER_DISK_SELF);
    load_disk_parameter_table(V5_SETTINGS_PARAMETER_DISK_DRIVE);
    load_g53_disk_parameter_tables();
    mark_missing_encoder_bits_unavailable();
    g_loaded = 1;
}

void v5_settings_axis_table_load_readback(const char *project_root)
{
    static V5BootClosure closure;
    v5_boot_closure_load(&closure, project_root);
    v5_settings_axis_table_load_boot_closure(&closure);
}

const char *v5_settings_axis_table_value(unsigned int row, unsigned int col)
{
    if (!g_loaded) {
        clear_values();
        g_loaded = 1;
    }
    if (row >= V5_AXIS_TABLE_MAX_ROWS || col >= V5_AXIS_TABLE_MAX_COLS) {
        return "--";
    }
    return g_values[row][col][0] ? g_values[row][col] : "--";
}

int v5_settings_axis_table_value_is_real(unsigned int row, unsigned int col)
{
    if (row >= V5_AXIS_TABLE_MAX_ROWS || col >= V5_AXIS_TABLE_MAX_COLS) {
        return 0;
    }
    return g_value_real[row][col] ? 1 : 0;
}


unsigned int v5_settings_axis_table_slave_option_count(void)
{
    return g_slave_option_count;
}

const char *v5_settings_axis_table_slave_option(unsigned int index)
{
    if (index >= g_slave_option_count) {
        return "";
    }
    return g_slave_options[index];
}

static const V5SettingsAxisColumnSpec kAxisColumns[] = {
    {"axis_mode", "轴模式", 0, 94, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"direction_mode", "方向", 100, 62, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"slave", "从站", 168, 98, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE, V5_PARAMETER_READBACK_SELF_PARAMETER_TABLE, 0},
    {"precision", "目标精度\n单位/count", 272, 112, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"pitch", "螺距", 390, 76, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"motor_rev", "电机", 472, 74, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"load_rev", "负载", 552, 74, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"home_order", "回零次序", 632, 90, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"home_direction", "回零方向", 728, 90, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"soft_minus", "-软限位", 824, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"zero", "零位", 930, 80, V5_AXIS_FIELD_ACTION, V5_PARAMETER_OWNER_NATIVE_RUNTIME, V5_PARAMETER_READBACK_NATIVE_API, 0},
    {"soft_plus", "+软限位", 1016, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"max_velocity", "速度", 1122, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"max_acceleration", "加速", 1228, 100, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"backlash", "反向间隙", 1334, 96, V5_AXIS_FIELD_EDIT, V5_PARAMETER_OWNER_RUNTIME_INI, V5_PARAMETER_READBACK_RUNTIME_INI, 0},
    {"encoder_bits", "bit", 1440, 70, V5_AXIS_FIELD_SELECT, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"egear_numerator", "分子", 1516, 92, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"egear_denominator", "分母", 1614, 92, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
    {"write_status", "写入状态", 1712, 132, V5_AXIS_FIELD_READONLY, V5_PARAMETER_OWNER_DRIVE_ONLY, V5_PARAMETER_READBACK_DRIVE_PROVIDER, 1},
};

static lv_color_t rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return lv_color_make(r, g, b);
}

static void plain_obj(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 2, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    lv_obj_t *obj = lv_obj_create(parent);
    plain_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, rgb(r, g, b), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text ? text : "");
    lv_obj_set_style_text_color(obj, rgb(r, g, b), 0);
    return obj;
}

static lv_color_t axis_color(const V5SettingsAxisRowSpec *row)
{
    if (!row || !row->axis) {
        return rgb(226, 238, 246);
    }
    if (strcmp(row->axis, "X") == 0 || strcmp(row->axis, "A") == 0) {
        return rgb(255, 100, 106);
    }
    if (strcmp(row->axis, "Y") == 0) {
        return rgb(0, 232, 150);
    }
    if (strcmp(row->axis, "Z") == 0) {
        return rgb(82, 178, 255);
    }
    if (strcmp(row->axis, "C") == 0) {
        return rgb(0, 225, 220);
    }
    return rgb(226, 238, 246);
}

static lv_color_t field_color(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    if (!row || !row->enabled || !col) {
        return rgb(150, 170, 190);
    }
    if (col->kind == V5_AXIS_FIELD_ACTION) {
        return rgb(0, 230, 200);
    }
    if (col->kind == V5_AXIS_FIELD_SELECT) {
        return rgb(255, 100, 106);
    }
    if (col->kind == V5_AXIS_FIELD_READONLY) {
        return rgb(155, 177, 198);
    }
    return rgb(226, 238, 246);
}

static const char *initial_value(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    unsigned int r;
    unsigned int c;
    if (!row || !col) {
        return "--";
    }
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        if (&kAxisRows[r] == row) {
            for (c = 0U; c < v5_settings_axis_table_column_count(); ++c) {
                if (&kAxisColumns[c] == col) {
                    return v5_settings_axis_table_value(r, c);
                }
            }
        }
    }
    return "--";
}

int v5_settings_axis_table_field_id(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col, char *out, size_t out_cap)
{
    if (!out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!row || !row->axis || !col || !col->field_key) {
        return 0;
    }
    snprintf(out, out_cap, "axis_%s_%s", row->axis, col->field_key);
    return out[0] != '\0';
}

int v5_settings_axis_table_field_matches_owner(const V5SettingsAxisRowSpec *row, const V5SettingsAxisColumnSpec *col)
{
    char id[96];
    const V5ParameterField *field;

    if (!v5_settings_axis_table_field_id(row, col, id, sizeof(id))) {
        return 0;
    }
    field = v5_parameter_table_find(id);
    if (!field) {
        return 0;
    }
    return field->owner == col->owner &&
           field->readback == col->readback &&
           field->drive_only_allowed == col->drive_only_allowed;
}

static lv_obj_t *value_cell(lv_obj_t *parent, int x, int y, int w, int h, const char *text, lv_color_t text_color, int disabled, const char *debug_id)
{
    lv_obj_t *cell = panel(parent, x, y, w, h, disabled ? 34 : 5, disabled ? 47 : 27, disabled ? 58 : 43);
    lv_obj_t *text_label = label(cell, text, 0, 5, w, h - 6, 226, 238, 246);
    (void)debug_id;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, rgb(disabled ? 54 : 45, disabled ? 66 : 86, disabled ? 76 : 112), 0);
    return cell;
}

static V5AxisCellRef *store_cell_ref(unsigned int kind, unsigned int row, unsigned int col, lv_obj_t *obj, lv_obj_t *label_obj, const char *field_id)
{
    V5AxisCellRef *ref;
    if (g_cell_ref_count >= V5_AXIS_CELL_REF_MAX) {
        return 0;
    }
    ref = &g_cell_refs[g_cell_ref_count++];
    ref->kind = kind;
    ref->row = row;
    ref->col = col;
    ref->obj = obj;
    ref->label = label_obj;
    snprintf(ref->field_id, sizeof(ref->field_id), "%s", field_id ? field_id : "axis_param");
    return ref;
}

static void log_axis_param_event(const char *field_id, const char *value, int ok)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/ui_events.jsonl", "ab");
    if (!fp) return;
    fprintf(fp, "{\"schema\":\"v5.ui_event.v1\",\"event\":\"settings_axis_param_commit\",\"field\":\"%s\",\"value\":\"%s\",\"ok\":%s}\n",
            field_id ? field_id : "", value ? value : "", ok ? "true" : "false");
    fclose(fp);
}

static int current_driver_mode_is_pulse(void)
{
    const char *mode = v5_settings_axis_table_bus_pulse_value();
    if (!mode) return 0;
    return strstr(mode, "pulse") || strstr(mode, "Pulse") || strstr(mode, "PULSE") || strstr(mode, "脉冲");
}

static int axis_zero_col_index(void)
{
    return column_index("zero");
}

static void log_axis_zero_request_event(const V5SettingsAxisRowSpec *row, const char *driver_mode, const char *target_scope, const char *apply_mode, const char *slave_index, const char *home_offset, int ok)
{
    FILE *fp;
    mkdir("/run/8ax_v5_product_ui", 0755);
    fp = fopen("/run/8ax_v5_product_ui/ui_events.jsonl", "ab");
    if (!fp) return;
    fprintf(fp,
            "{\"schema\":\"v5.ui_event.v1\",\"event\":\"settings_axis_zero_request\",\"axis\":\"%s\",\"driver_mode\":\"%s\",\"target_scope\":\"%s\",\"apply_mode\":\"%s\",\"slave_index\":\"%s\",\"home_offset\":\"%s\",\"ok\":%s}\n",
            row && row->axis ? row->axis : "",
            driver_mode ? driver_mode : "",
            target_scope ? target_scope : "",
            apply_mode ? apply_mode : "",
            slave_index ? slave_index : "",
            home_offset ? home_offset : "",
            ok ? "true" : "false");
    fclose(fp);
}

int v5_settings_axis_table_start_axis_zero(unsigned int row, const char *home_offset)
{
    const V5SettingsAxisRowSpec *row_spec;
    const char *driver_mode;
    const char *target_scope;
    const char *apply_mode;
    const char *slave_value;
    char slave_index[V5_AXIS_VALUE_CAP];
    int pulse;

    if (row >= v5_settings_axis_table_row_count()) {
        return 0;
    }
    row_spec = &kAxisRows[row];
    if (!row_spec->enabled) {
        return 0;
    }
    pulse = current_driver_mode_is_pulse();
    driver_mode = pulse ? "pulse" : "bus";
    target_scope = pulse ? "pulse_mechanical_home_offset" : "bus_count_domain_zero";
    apply_mode = pulse ? "persist_home_offset" : "count_domain_zero";
    slave_index[0] = '\0';
    slave_value = row_value(row, "slave");
    slave_option_extract_id(slave_value, slave_index, sizeof(slave_index));
    if (slave_id_is_nat(slave_index)) {
        slave_index[0] = '\0';
    }
    if (!pulse && !slave_index[0]) {
        log_axis_zero_request_event(row_spec, driver_mode, target_scope, apply_mode, "", "", 0);
        return 0;
    }
    if (pulse && (!home_offset || !home_offset[0])) {
        log_axis_zero_request_event(row_spec, driver_mode, target_scope, apply_mode, "", "", 0);
        return 0;
    }
    log_axis_zero_request_event(row_spec, driver_mode, target_scope, apply_mode, slave_index, home_offset ? home_offset : "", 1);
    if (g_axis_zero_callback) {
        g_axis_zero_callback(row_spec->axis, driver_mode, target_scope, apply_mode, slave_index, home_offset ? home_offset : "", g_axis_zero_callback_user_data);
    }
    return 1;
}


static int strings_equal_numeric_ok(const char *a, const char *b)
{
    char *ea;
    char *eb;
    double da;
    double db;
    if (!a || !b) return 0;
    if (strcmp(a, b) == 0) return 1;
    da = strtod(a, &ea);
    db = strtod(b, &eb);
    return ea != a && eb != b && *ea == '\0' && *eb == '\0' && da == db;
}

static void set_axis_value_from_double(unsigned int row, const char *field_key, double value)
{
    char text[V5_AXIS_VALUE_CAP];
    if (!field_key || row >= v5_settings_axis_table_row_count() || !isfinite(value)) {
        return;
    }
    format_double_compact(text, sizeof(text), value);
    set_value(row, field_key, text, 1);
}

int v5_settings_axis_table_commit_g53_value(unsigned int row, unsigned int col, const char *value)
{
    const char *key;
    V5SettingsParameterDiskTable table;
    char readback[V5_AXIS_VALUE_CAP];
    char id[96];
    int ok;
    if (!v5_settings_axis_table_g53_value_is_editable(row, col) || !value || !value[0]) return 0;
    key = g53_field_key(row, col);
    if (!key) return 0;
    if (g53_value_is_native_geometry_owner(row, col)) {
        const char *ini_key = g53_runtime_ini_key(row, col);
        ok = runtime_ini_write_rtcp_value(ini_key, value, readback, sizeof(readback));
    } else {
        table = g53_disk_table(row, col);
        if (table == V5_SETTINGS_PARAMETER_DISK_NONE) return 0;
        ok = v5_settings_parameter_store_write_axis(g_project_root, table, "G53", key, value);
        ok = ok && resident_parameter_table_upsert_write(table, "G53", key, value);
        ok = ok && resident_parameter_table_read_axis(table, "G53", key, readback, sizeof(readback));
        ok = ok && strings_equal_numeric_ok(readback, value);
    }
    if (ok) {
        set_g53_value(row, col, readback, 1);
    }
    if (!g53_field_id(row, col, id, sizeof(id))) {
        snprintf(id, sizeof(id), "g53_%u_%u", row, col);
    }
    log_axis_param_event(id, value, ok);
    if (ok) {
        notify_settings_axis_commit_success();
    }
    return ok;
}

int v5_settings_axis_table_commit_value(unsigned int row, unsigned int col, const char *value)
{
    const V5SettingsAxisRowSpec *row_spec;
    const V5SettingsAxisColumnSpec *col_spec;
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult commit_result;
    char id[96];
    int ok;
    if (row >= v5_settings_axis_table_row_count() || col >= v5_settings_axis_table_column_count() || !value || !value[0]) {
        return 0;
    }
    row_spec = &kAxisRows[row];
    col_spec = &kAxisColumns[col];
    if (!row_spec->enabled || col_spec->kind == V5_AXIS_FIELD_READONLY) {
        return 0;
    }
    if (strcmp(col_spec->field_key, "zero") == 0) {
        return 0;
    }
    if (strcmp(col_spec->field_key, "encoder_bits") == 0 &&
        (strcmp(value, "NAT") == 0 || strcmp(value, "--") == 0)) {
        return 0;
    }
    if (!v5_settings_axis_table_field_matches_owner(row_spec, col_spec)) {
        return 0;
    }
    if (!v5_settings_axis_table_field_id(row_spec, col_spec, id, sizeof(id))) {
        return 0;
    }
    memset(&request, 0, sizeof(request));
    memset(&commit_result, 0, sizeof(commit_result));
    request.project_root = g_project_root;
    request.axis = row_spec->axis;
    request.axis_index = row;
    request.field_key = col_spec->field_key;
    request.field_name = id;
    request.value_text = value;
    request.owner_generation = settings_axis_owner_generation(id);
    request.readback_token = settings_axis_readback_token(id, value);
    ok = v5_settings_apply_commit_axis_value(&request, &commit_result);
    if (ok && commit_result.scale_chain.raw_limits_recomputed) {
        set_axis_value_from_double(row, "soft_minus", commit_result.scale_chain.raw_min_limit);
        set_axis_value_from_double(row, "soft_plus", commit_result.scale_chain.raw_max_limit);
        if (commit_result.scale_chain.scale_recomputed && commit_result.scale_chain.effective_scale > 0.0) {
            set_axis_value_from_double(row, "precision", 1.0 / commit_result.scale_chain.effective_scale);
        }
    }
    if (ok) {
        set_value(row, col_spec->field_key, commit_result.readback_value, 1);
    }
    {
        log_axis_param_event(id, value, ok);
    }
    if (ok) {
        notify_settings_axis_commit_success();
    }
    return ok;
}

static int option_line_matches_value(const char *start, size_t len, const char *value)
{
    char option[64];
    char *endptr_a;
    char *endptr_b;
    long a;
    long b;
    if (!start || !value || !value[0]) return 0;
    if (len >= sizeof(option)) len = sizeof(option) - 1U;
    memcpy(option, start, len);
    option[len] = '\0';
    trim_in_place(option);
    if (strcmp(option, value) == 0) return 1;
    a = strtol(option, &endptr_a, 10);
    b = strtol(value, &endptr_b, 10);
    return endptr_a != option && endptr_b != value && a == b;
}

static unsigned int dropdown_selected_for_value(const char *options, const char *value)
{
    const char *start;
    unsigned int index = 0U;
    if (!options || !options[0] || !value || !value[0]) return 0U;
    start = options;
    while (*start) {
        const char *end = strchr(start, '\n');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (option_line_matches_value(start, len, value)) {
            return index;
        }
        if (!end) break;
        start = end + 1;
        ++index;
    }
    return 0U;
}

static int append_dropdown_option(char *out, size_t cap, const char *value)
{
    size_t len;
    if (!out || cap == 0U || !value || !value[0]) return 0;
    len = strlen(out);
    if (len > 0U) {
        if (len + 1U >= cap) return 0;
        out[len++] = '\n';
        out[len] = '\0';
    }
    if (len + strlen(value) + 1U > cap) return 0;
    strcat(out, value);
    return 1;
}

static int append_slave_dropdown_options_for_row(unsigned int row, char *out, size_t cap)
{
    unsigned int i;
    int ok = append_dropdown_option(out, cap, "NAT");
    for (i = 0U; i < g_slave_option_count; ++i) {
        if (slave_option_is_nat(g_slave_options[i])) continue;
        if (!row_slave_matches_option(row, g_slave_options[i]) &&
            slave_option_used_by_other_row(row, g_slave_options[i])) {
            continue;
        }
        ok = append_dropdown_option(out, cap, g_slave_options[i]) && ok;
    }
    return ok;
}

static int dropdown_options_for_cell(const V5SettingsAxisColumnSpec *col, unsigned int row, const char *value, char *out, size_t cap)
{
    if (!col || !out || cap == 0U) return 0;
    out[0] = '\0';
    if (strcmp(col->field_key, "axis_mode") == 0) {
        snprintf(out, cap, "直线\n旋转\n虚拟");
    } else if (strcmp(col->field_key, "direction_mode") == 0) {
        snprintf(out, cap, "cw\nccw");
    } else if (strcmp(col->field_key, "slave") == 0) {
        return append_slave_dropdown_options_for_row(row, out, cap);
    } else if (strcmp(col->field_key, "home_order") == 0) {
        snprintf(out, cap, "禁用\n0\n1\n2\n3\n4\n5\n6\n7");
    } else if (strcmp(col->field_key, "home_direction") == 0) {
        snprintf(out, cap, "0\n+\n-");
    } else if (strcmp(col->field_key, "encoder_bits") == 0) {
        snprintf(out, cap, "24\n23\n22\n21\n20\n19\n18\n17\n16");
    }
    (void)value;
    return out[0] != '\0';
}

int v5_settings_axis_table_dropdown_options(unsigned int row, unsigned int col, char *out, size_t cap)
{
    if (!out || cap == 0U) return 0;
    out[0] = '\0';
    if (row >= v5_settings_axis_table_row_count() || col >= v5_settings_axis_table_column_count()) {
        return 0;
    }
    return dropdown_options_for_cell(&kAxisColumns[col], row, v5_settings_axis_table_value(row, col), out, cap);
}

static void refresh_axis_cell_ref(V5AxisCellRef *ref)
{
    const V5SettingsAxisColumnSpec *col;
    const char *value;
    if (!ref) return;
    if (ref->kind == V5_CELL_KIND_G53) {
        value = v5_settings_axis_table_g53_value(ref->row, ref->col);
        if (ref->label) {
            lv_label_set_text(ref->label, value);
        }
        if (ref->obj) {
            lv_obj_invalidate(ref->obj);
        }
        return;
    }
    if (ref->row >= v5_settings_axis_table_row_count() ||
        ref->col >= v5_settings_axis_table_column_count()) {
        return;
    }
    col = &kAxisColumns[ref->col];
    value = v5_settings_axis_table_value(ref->row, ref->col);
    if (col->kind == V5_AXIS_FIELD_SELECT && ref->obj) {
        char options[V5_DROPDOWN_OPTIONS_CAP];
        if (dropdown_options_for_cell(col, ref->row, value, options, sizeof(options))) {
            lv_dropdown_set_options(ref->obj, options);
            lv_dropdown_set_selected(ref->obj, (uint16_t)dropdown_selected_for_value(options, value));
        }
    } else if (col->kind == V5_AXIS_FIELD_ACTION && ref->label) {
        lv_label_set_text(ref->label, current_driver_mode_is_pulse() ? "设零" : "设0");
    } else if (ref->label) {
        lv_label_set_text(ref->label, value);
    }
    if (ref->obj) {
        lv_obj_invalidate(ref->obj);
    }
}

void v5_settings_axis_table_reload_current_readback(void)
{
    char project_root[sizeof(g_project_root)];
    lv_coord_t scroll_x = 0;
    unsigned int i;
    snprintf(project_root, sizeof(project_root), "%s", g_project_root[0] ? g_project_root : ".");
    if (g_axis_scroll) {
        scroll_x = lv_obj_get_scroll_x(g_axis_scroll);
    }
    v5_settings_axis_table_load_readback(project_root);
    for (i = 0U; i < g_cell_ref_count; ++i) {
        refresh_axis_cell_ref(&g_cell_refs[i]);
    }
    if (g_axis_scroll) {
        lv_obj_scroll_to_x(g_axis_scroll, scroll_x, LV_ANIM_OFF);
        lv_obj_invalidate(g_axis_scroll);
    }
}

static void dropdown_changed_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    char selected[64];
    char committed[V5_AXIS_VALUE_CAP];
    const char *commit_value = selected;
    if (!ref || !ref->obj || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    selected[0] = '\0';
    lv_dropdown_get_selected_str(ref->obj, selected, sizeof(selected));
    if (ref->col < V5_AXIS_TABLE_MAX_COLS && strcmp(kAxisColumns[ref->col].field_key, "slave") == 0) {
        slave_option_extract_id(selected, committed, sizeof(committed));
        if (committed[0]) {
            commit_value = committed;
        }
    }
    (void)v5_settings_axis_table_commit_value(ref->row, ref->col, commit_value);
}

static void dropdown_pressed_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    const V5SettingsAxisColumnSpec *col;
    char options[V5_DROPDOWN_OPTIONS_CAP];
    const char *value;
    if (!ref || !ref->obj || lv_event_get_code(event) != LV_EVENT_PRESSED) {
        return;
    }
    if (ref->col >= v5_settings_axis_table_column_count()) return;
    col = &kAxisColumns[ref->col];
    if (strcmp(col->field_key, "slave") != 0) return;
    reload_slave_options_from_resident_self_table();
    value = v5_settings_axis_table_value(ref->row, ref->col);
    if (dropdown_options_for_cell(col, ref->row, value, options, sizeof(options))) {
        lv_dropdown_set_options(ref->obj, options);
        lv_dropdown_set_selected(ref->obj, (uint16_t)dropdown_selected_for_value(options, value));
    }
}

static void dropdown_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, const V5SettingsAxisColumnSpec *col, lv_color_t text_color, const char *debug_id)
{
    char options[V5_DROPDOWN_OPTIONS_CAP];
    lv_obj_t *dd;
    (void)debug_id;
    if (!dropdown_options_for_cell(col, row, text, options, sizeof(options))) {
        value_cell(parent, x, y, w, h, text, text_color, 0, debug_id);
        return;
    }
    dd = lv_dropdown_create(parent);
    plain_obj(dd);
    lv_obj_set_pos(dd, x, y);
    lv_obj_set_size(dd, w, h);
    lv_obj_set_style_bg_color(dd, rgb(5, 27, 43), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, rgb(33, 72, 98), 0);
    lv_obj_set_style_text_color(dd, text_color, 0);
    lv_obj_set_style_pad_all(dd, 0, 0);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    {
        V5AxisCellRef *ref;
        lv_dropdown_set_options(dd, options);
        lv_dropdown_set_selected(dd, (uint16_t)dropdown_selected_for_value(options, text));
        ref = store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, dd, 0, debug_id);
        lv_obj_add_event_cb(dd, dropdown_pressed_cb, LV_EVENT_PRESSED, ref);
        lv_obj_add_event_cb(dd, dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, ref);
    }
}

static void close_keyboard_overlay_with_refresh(V5AxisCellRef *refresh_ref)
{
    lv_obj_t *refresh_obj = refresh_ref ? refresh_ref->obj : 0;

    if (g_keyboard_overlay) {
        lv_obj_del(g_keyboard_overlay);
    }
    g_keyboard_overlay = 0;
    g_keyboard_value_label = 0;
    g_keyboard_ref = 0;
    g_keyboard_value[0] = '\0';
    v5_ui_first_frame_guard_restore_dirty(&g_keyboard_frame_guard, refresh_obj);
}

static void close_keyboard_overlay(void)
{
    close_keyboard_overlay_with_refresh(0);
}

static void keyboard_overlay_clicked_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        close_keyboard_overlay();
    }
}

static void refresh_keyboard_value_label(void)
{
    if (g_keyboard_value_label) {
        lv_label_set_text(g_keyboard_value_label, g_keyboard_value[0] ? g_keyboard_value : " ");
    }
}

static void keyboard_append_text(const char *text)
{
    size_t used;
    size_t add;
    if (!text || !text[0]) return;
    used = strlen(g_keyboard_value);
    add = strlen(text);
    if (used + add >= sizeof(g_keyboard_value)) {
        return;
    }
    memcpy(g_keyboard_value + used, text, add + 1U);
    refresh_keyboard_value_label();
}

static void keyboard_backspace(void)
{
    size_t len = strlen(g_keyboard_value);
    if (len > 0U) {
        g_keyboard_value[len - 1U] = '\0';
        refresh_keyboard_value_label();
    }
}

static void keyboard_toggle_sign(void)
{
    size_t len = strlen(g_keyboard_value);
    if (g_keyboard_value[0] == '-') {
        memmove(g_keyboard_value, g_keyboard_value + 1, len);
    } else if (len + 1U < sizeof(g_keyboard_value)) {
        memmove(g_keyboard_value + 1, g_keyboard_value, len + 1U);
        g_keyboard_value[0] = '-';
    }
    refresh_keyboard_value_label();
}

static void keyboard_commit_value(void)
{
    int ok;
    const char *readback;
    if (!g_keyboard_ref || !g_keyboard_value[0]) {
        return;
    }
    if (g_keyboard_ref->kind == V5_CELL_KIND_G53) {
        ok = v5_settings_axis_table_commit_g53_value(g_keyboard_ref->row, g_keyboard_ref->col, g_keyboard_value);
        readback = v5_settings_axis_table_g53_value(g_keyboard_ref->row, g_keyboard_ref->col);
    } else if ((int)g_keyboard_ref->col == axis_zero_col_index()) {
        ok = v5_settings_axis_table_start_axis_zero(g_keyboard_ref->row, g_keyboard_value);
        readback = ok ? g_keyboard_value : v5_settings_axis_table_value(g_keyboard_ref->row, g_keyboard_ref->col);
    } else {
        ok = v5_settings_axis_table_commit_value(g_keyboard_ref->row, g_keyboard_ref->col, g_keyboard_value);
        readback = v5_settings_axis_table_value(g_keyboard_ref->row, g_keyboard_ref->col);
    }
    if (g_keyboard_ref->label) {
        lv_label_set_text(g_keyboard_ref->label, readback);
    }
    if (ok) {
        close_keyboard_overlay_with_refresh(g_keyboard_ref);
    }
}

static void keyboard_key_event_cb(lv_event_t *event)
{
    const char *key = (const char *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !key) {
        return;
    }
    if (strcmp(key, "OK") == 0) {
        keyboard_commit_value();
    } else if (strcmp(key, "ESC") == 0) {
        close_keyboard_overlay();
    } else if (strcmp(key, "BKSP") == 0) {
        keyboard_backspace();
    } else if (strcmp(key, "C") == 0) {
        g_keyboard_value[0] = '\0';
        refresh_keyboard_value_label();
    } else if (strcmp(key, "+/-") == 0) {
        keyboard_toggle_sign();
    } else {
        keyboard_append_text(key);
    }
}

static void make_keyboard_key(lv_obj_t *parent, const char *text, int x, int y, int w, int h)
{
    unsigned char fill_r = 5U, fill_g = 27U, fill_b = 43U;
    unsigned char border_r = 33U, border_g = 72U, border_b = 98U;
    unsigned char text_r = 226U, text_g = 238U, text_b = 246U;
    lv_obj_t *key;
    lv_obj_t *text_label;

    if (strcmp(text, "C") == 0) {
        fill_r = 185U; fill_g = 75U; fill_b = 69U;
        border_r = 212U; border_g = 106U; border_b = 99U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    } else if (strcmp(text, "OK") == 0) {
        fill_r = 125U; fill_g = 169U; fill_b = 93U;
        border_r = 158U; border_g = 196U; border_b = 125U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    } else if (strcmp(text, "/") == 0 || strcmp(text, "x") == 0 ||
               strcmp(text, "-") == 0 || strcmp(text, "+") == 0 ||
               strcmp(text, "BKSP") == 0) {
        fill_r = 75U; fill_g = 86U; fill_b = 97U;
        border_r = 107U; border_g = 120U; border_b = 134U;
        text_r = 255U; text_g = 255U; text_b = 255U;
    }

    key = panel(parent, x, y, w, h, fill_r, fill_g, fill_b);
    lv_obj_set_style_border_width(key, 1, 0);
    lv_obj_set_style_border_color(key, rgb(border_r, border_g, border_b), 0);
    lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
    text_label = label(key, text, 0, (h - 22) / 2, w, 24, text_r, text_g, text_b);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(key, keyboard_key_event_cb, LV_EVENT_CLICKED, (void *)text);
}

static void open_keyboard_for_ref(V5AxisCellRef *ref)
{
    static const char *keys[] = {
        "/", "x", "-", "+",
        "7", "8", "9", "BKSP",
        "4", "5", "6", "C",
        "1", "2", "3", "OK",
        "0", ".", "+/-", "ESC",
    };
    enum {
        hit_x = 352,
        hit_y = 76,
        hit_w = 320,
        hit_h = 419,
        title_x = 10,
        title_y = 6,
        panel_x = 0,
        panel_y = 29,
        value_x = 6,
        value_y = 35,
        value_w = 308,
        value_h = 54,
        matrix_x = 6,
        matrix_y = 95,
        key_w = 75,
        key_h = 61,
        gap = 2
    };
    lv_obj_t *keyboard_hit_area;
    lv_obj_t *keyboard_panel;
    lv_obj_t *value_box;
    lv_obj_t *title;
    unsigned int i;

    if (!ref) return;
    close_keyboard_overlay();
    g_keyboard_ref = ref;
    snprintf(g_keyboard_value, sizeof(g_keyboard_value), "%s", ref->kind == V5_CELL_KIND_G53 ? v5_settings_axis_table_g53_value(ref->row, ref->col) : v5_settings_axis_table_value(ref->row, ref->col));
    v5_ui_first_frame_guard_begin(&g_keyboard_frame_guard, V5_REMOTE_DISPLAY_CACHE_KEYBOARD);

    g_keyboard_overlay = panel(lv_scr_act(), 0, 0, 1024, 600, 3, 12, 20);
    lv_obj_set_style_bg_opa(g_keyboard_overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_keyboard_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_keyboard_overlay, keyboard_overlay_clicked_cb, LV_EVENT_CLICKED, 0);
    lv_obj_move_foreground(g_keyboard_overlay);

    keyboard_hit_area = panel(g_keyboard_overlay, hit_x, hit_y, hit_w, hit_h, 3, 12, 20);
    lv_obj_set_style_bg_opa(keyboard_hit_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboard_hit_area, 0, 0);
    lv_obj_add_flag(keyboard_hit_area, LV_OBJ_FLAG_CLICKABLE);

    title = label(keyboard_hit_area, "设置参数", title_x, title_y, 300, 24, 226, 238, 246);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    keyboard_panel = panel(keyboard_hit_area, panel_x, panel_y, 320, 390, 12, 39, 58);
    lv_obj_set_style_border_width(keyboard_panel, 1, 0);
    lv_obj_set_style_border_color(keyboard_panel, rgb(33, 72, 98), 0);
    lv_obj_add_flag(keyboard_panel, LV_OBJ_FLAG_CLICKABLE);

    value_box = panel(keyboard_hit_area, value_x, value_y, value_w, value_h, 5, 27, 43);
    lv_obj_set_style_border_width(value_box, 1, 0);
    lv_obj_set_style_border_color(value_box, rgb(54, 86, 113), 0);
    lv_obj_add_flag(value_box, LV_OBJ_FLAG_CLICKABLE);
    g_keyboard_value_label = label(value_box, g_keyboard_value[0] ? g_keyboard_value : " ", 10, 12, value_w - 20, 30, 226, 238, 246);
    lv_obj_set_style_text_align(g_keyboard_value_label, LV_TEXT_ALIGN_CENTER, 0);

    for (i = 0U; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int col = (int)(i % 4U);
        int row = (int)(i / 4U);
        make_keyboard_key(keyboard_hit_area,
                          keys[i],
                          matrix_x + col * (key_w + gap),
                          matrix_y + row * (key_h + gap),
                          key_w,
                          key_h);
    }
}

static void edit_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    open_keyboard_for_ref(ref);
}

static void axis_zero_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (!ref || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (current_driver_mode_is_pulse()) {
        open_keyboard_for_ref(ref);
        return;
    }
    (void)v5_settings_axis_table_start_axis_zero(ref->row, 0);
}

static void action_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id)
{
    lv_obj_t *cell = panel(parent, x, y, w, h, 5, 27, 43);
    lv_obj_t *text_label = label(cell, text && text[0] ? text : "设0", 0, 5, w, h - 6, 0, 230, 200);
    V5AxisCellRef *ref;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, rgb(45, 86, 112), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    ref = store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, cell, text_label, debug_id);
    lv_obj_add_event_cb(cell, axis_zero_cell_clicked_cb, LV_EVENT_CLICKED, ref);
}

static void edit_cell(lv_obj_t *parent, unsigned int row, unsigned int col_index, int x, int y, int w, int h, const char *text, lv_color_t text_color, const char *debug_id)
{
    lv_obj_t *cell = panel(parent, x, y, w, h, 5, 27, 43);
    lv_obj_t *text_label = label(cell, text, 0, 5, w, h - 6, 226, 238, 246);
    V5AxisCellRef *ref;
    lv_obj_set_style_text_color(text_label, text_color, 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, rgb(45, 86, 112), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    ref = store_cell_ref(V5_CELL_KIND_AXIS, row, col_index, cell, text_label, debug_id);
    lv_obj_add_event_cb(cell, edit_cell_clicked_cb, LV_EVENT_CLICKED, ref);
}

void v5_settings_axis_table_begin_page(void)
{
    close_keyboard_overlay();
    g_cell_ref_count = 0U;
    memset(g_cell_refs, 0, sizeof(g_cell_refs));
}

static void g53_edit_cell_clicked_cb(lv_event_t *event)
{
    V5AxisCellRef *ref = (V5AxisCellRef *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    open_keyboard_for_ref(ref);
}

void v5_settings_axis_table_create_g53_cell(lv_obj_t *root, unsigned int row, unsigned int col, int x, int y, int w, int h)
{
    const char *value;
    char id[96];
    lv_obj_t *cell;
    lv_obj_t *text_label;
    V5AxisCellRef *ref;
    int editable;
    if (!root || row >= V5_G53_ROW_COUNT || col >= 3U) return;
    value = v5_settings_axis_table_g53_value(row, col);
    editable = v5_settings_axis_table_g53_value_is_editable(row, col);
    if (!editable) {
        value_cell(root, x, y, w, h, value, rgb(226, 238, 246), !v5_settings_axis_table_g53_value_is_real(row, col), "g53_readonly");
        return;
    }
    if (!g53_field_id(row, col, id, sizeof(id))) {
        snprintf(id, sizeof(id), "g53_%u_%u", row, col);
    }
    cell = panel(root, x, y, w, h, 5, 27, 43);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, rgb(15, 43, 61), 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    text_label = label(cell, value, 0, 4, w, h - 5, 226, 238, 246);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    ref = store_cell_ref(V5_CELL_KIND_G53, row, col, cell, text_label, id);
    lv_obj_add_event_cb(cell, g53_edit_cell_clicked_cb, LV_EVENT_CLICKED, ref);
}

static lv_coord_t axis_table_last_content_x(void)
{
    unsigned int i;
    lv_coord_t last = 0;
    for (i = 0U; i < v5_settings_axis_table_column_count(); ++i) {
        lv_coord_t right = (lv_coord_t)(kAxisColumns[i].x + kAxisColumns[i].width);
        if (right > last) {
            last = right;
        }
    }
    return last;
}

static void axis_scroll_page(int direction)
{
    lv_coord_t current;
    lv_coord_t page;
    lv_coord_t max_x;
    lv_coord_t target;
    if (!g_axis_scroll || direction == 0) {
        return;
    }
    current = lv_obj_get_scroll_x(g_axis_scroll);
    page = lv_obj_get_width(g_axis_scroll) - 96;
    if (page < 160) {
        page = 160;
    }
    max_x = axis_table_last_content_x() - lv_obj_get_width(g_axis_scroll);
    if (max_x < 0) {
        max_x = 0;
    }
    target = current + (direction > 0 ? page : -page);
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    lv_obj_scroll_to_x(g_axis_scroll, target, LV_ANIM_ON);
}

static void axis_page_button_cb(lv_event_t *event)
{
    intptr_t direction;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    direction = (intptr_t)lv_event_get_user_data(event);
    axis_scroll_page(direction > 0 ? 1 : -1);
}

static void make_axis_page_button(lv_obj_t *parent, int x, int y, int w, int h, int direction)
{
    static const lv_point_t left_points[] = {{25, 9}, {13, 20}, {25, 31}};
    static const lv_point_t right_points[] = {{13, 9}, {25, 20}, {13, 31}};
    static const lv_point_t left_points_narrow[] = {{17, 13}, {8, 27}, {17, 41}};
    static const lv_point_t right_points_narrow[] = {{8, 13}, {17, 27}, {8, 41}};
    lv_obj_t *button;
    lv_obj_t *line;
    int narrow = w < 32;
    button = panel(parent, x, y, w, h, 8, 34, 52);
    lv_obj_set_style_bg_color(button, rgb(20, 62, 91), 0);
    lv_obj_set_style_bg_color(button, rgb(39, 113, 164), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, rgb(76, 119, 146), 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, axis_page_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)direction);

    line = lv_line_create(button);
    if (narrow) {
        lv_line_set_points(line, direction < 0 ? left_points_narrow : right_points_narrow, 3);
        lv_obj_set_pos(line, 0, 0);
    } else {
        lv_line_set_points(line, direction < 0 ? left_points : right_points, 3);
        lv_obj_set_pos(line, (w - 38) / 2, (h - 40) / 2);
    }
    lv_obj_set_style_line_color(line, rgb(226, 238, 246), 0);
    lv_obj_set_style_line_width(line, narrow ? 3 : 4, 0);
    lv_obj_set_style_line_rounded(line, 1, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(button);
}

static lv_obj_t *axis_scroll(lv_obj_t *parent)
{
    lv_obj_t *scroll = lv_obj_create(parent);
    plain_obj(scroll);
    lv_obj_set_pos(scroll, 95, 274);
    lv_obj_set_size(scroll, 908, 300);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_ON);
    g_axis_scroll = scroll;
    return scroll;
}

void v5_settings_axis_table_create(lv_obj_t *root)
{
    enum { row_h = 28, row_step = 31 };
    lv_obj_t *scroll;
    if (!root) {
        return;
    }

    /* Values come from owner readback: native/runtime INI, self parameter table, then drive/profile providers. */
    panel(root, 21, 225, 982, 343, 7, 31, 48);
    label(root, "轴参数(总线)", 30, 230, 120, 24, 226, 238, 246);
    label(root, "单位:mm/deg", 198, 230, 160, 24, 155, 177, 198);
    label(root, "轴", 54, 285, 32, 20, 150, 170, 190);

    for (unsigned int r = 0; r < v5_settings_axis_table_row_count(); ++r) {
        const V5SettingsAxisRowSpec *row = &kAxisRows[r];
        lv_color_t color = axis_color(row);
        value_cell(root, 30, 318 + (int)r * row_step, 63, row_h, row->label, color, !row->enabled, "axis_label");
    }

    scroll = axis_scroll(root);
    make_axis_page_button(root, 24, 244, 44, 46, -1);
    make_axis_page_button(root, 998, 236, 24, 54, 1);
    for (unsigned int c = 0; c < v5_settings_axis_table_column_count(); ++c) {
        const V5SettingsAxisColumnSpec *col = &kAxisColumns[c];
        const char *unit = strchr(col->label, '\n');
        if (unit) {
            char title[64];
            size_t len = (size_t)(unit - col->label);
            if (len >= sizeof(title)) {
                len = sizeof(title) - 1U;
            }
            memcpy(title, col->label, len);
            title[len] = '\0';
            label(scroll, title, col->x, 8, col->width, 20, 150, 170, 190);
            label(scroll, unit + 1, col->x, 27, col->width, 20, 150, 170, 190);
        } else {
            label(scroll, col->label, col->x, 11, col->width, 20, 150, 170, 190);
        }
    }

    for (unsigned int r = 0; r < v5_settings_axis_table_row_count(); ++r) {
        const V5SettingsAxisRowSpec *row = &kAxisRows[r];
        int y = 44 + (int)r * row_step;
        for (unsigned int c = 0; c < v5_settings_axis_table_column_count(); ++c) {
            const V5SettingsAxisColumnSpec *col = &kAxisColumns[c];
            char id[96];
            int disabled = !row->enabled || col->kind == V5_AXIS_FIELD_READONLY;
            v5_settings_axis_table_field_id(row, col, id, sizeof(id));
            if (!disabled && col->kind == V5_AXIS_FIELD_SELECT) {
                dropdown_cell(scroll, r, c, col->x, y, col->width, row_h, initial_value(row, col), col, field_color(row, col), id);
            } else if (!disabled && col->kind == V5_AXIS_FIELD_ACTION) {
                action_cell(scroll, r, c, col->x, y, col->width, row_h, current_driver_mode_is_pulse() ? "设零" : "设0", field_color(row, col), id);
            } else if (!disabled && col->kind != V5_AXIS_FIELD_READONLY) {
                edit_cell(scroll, r, c, col->x, y, col->width, row_h, initial_value(row, col), field_color(row, col), id);
            } else {
                lv_obj_t *cell = value_cell(scroll, col->x, y, col->width, row_h, initial_value(row, col), field_color(row, col), disabled, id);
                lv_obj_t *text_label = lv_obj_get_child(cell, 0);
                (void)store_cell_ref(V5_CELL_KIND_AXIS, r, c, cell, text_label, id);
            }
        }
    }
    lv_obj_scroll_to_x(scroll, 0, LV_ANIM_OFF);
}

const char *v5_settings_axis_table_g53_value(unsigned int row, unsigned int col)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U) {
        return "--";
    }
    return g_g53_values[row][col][0] ? g_g53_values[row][col] : "--";
}

int v5_settings_axis_table_g53_value_is_real(unsigned int row, unsigned int col)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U) {
        return 0;
    }
    return g_g53_value_real[row][col] ? 1 : 0;
}

const char *v5_settings_axis_table_motion_model_value(void)
{
    return g_motion_model_value[0] ? g_motion_model_value : "--";
}

int v5_settings_axis_table_motion_model_value_is_real(void)
{
    return g_motion_model_real ? 1 : 0;
}

const char *v5_settings_axis_table_bus_pulse_value(void)
{
    return g_bus_pulse_value[0] ? g_bus_pulse_value : "--";
}

int v5_settings_axis_table_bus_pulse_value_is_real(void)
{
    return g_bus_pulse_real ? 1 : 0;
}

unsigned int v5_settings_axis_table_row_count(void)
{
    return (unsigned int)(sizeof(kAxisRows) / sizeof(kAxisRows[0]));
}

unsigned int v5_settings_axis_table_column_count(void)
{
    return (unsigned int)(sizeof(kAxisColumns) / sizeof(kAxisColumns[0]));
}

const V5SettingsAxisRowSpec *v5_settings_axis_table_rows(void)
{
    return kAxisRows;
}

const V5SettingsAxisColumnSpec *v5_settings_axis_table_columns(void)
{
    return kAxisColumns;
}
