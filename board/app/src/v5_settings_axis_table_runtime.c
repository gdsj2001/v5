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

void v5_settings_axis_set_g53_value(unsigned int row, unsigned int col, const char *value, int real)
{
    if (row >= V5_G53_ROW_COUNT || col >= 3U || !value || !value[0]) {
        return;
    }
    snprintf(g_v5_axis_table_g53_values[row][col], V5_AXIS_VALUE_CAP, "%s", value);
    g_v5_axis_table_g53_value_real[row][col] = real ? 1U : 0U;
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

const char *v5_settings_axis_g53_field_key(unsigned int row, unsigned int col)
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

void v5_settings_axis_format_double_compact(char *out, size_t cap, double value)
{
    if (!out || cap == 0U) return;
    snprintf(out, cap, "%.12g", value);
}

int v5_settings_axis_g53_field_id(unsigned int row, unsigned int col, char *out, size_t out_cap)
{
    const char *key = v5_settings_axis_g53_field_key(row, col);
    if (!key || !out || out_cap == 0U) return 0;
    if (row == 0U && col == 1U) {
        snprintf(out, out_cap, "g53_A_center_y");
        return 1;
    }
    if (row == 0U && col == 2U) {
        snprintf(out, out_cap, "g53_A_center_z");
        return 1;
    }
    if (row == 1U && col == 0U) {
        snprintf(out, out_cap, "g53_B_center_x");
        return 1;
    }
    if (row == 1U && col == 2U) {
        snprintf(out, out_cap, "g53_B_center_z");
        return 1;
    }
    if (row == 2U && col == 0U) {
        snprintf(out, out_cap, "g53_C_center_x");
        return 1;
    }
    if (row == 2U && col == 1U) {
        snprintf(out, out_cap, "g53_C_center_y");
        return 1;
    }
    snprintf(out, out_cap, "g53_%s", key);
    return 1;
}

void v5_settings_axis_load_g53_disk_parameter_tables(void)
{
    unsigned int r;
    unsigned int c;
    char value[V5_AXIS_VALUE_CAP];
    for (r = 0U; r < V5_G53_ROW_COUNT; ++r) {
        for (c = 0U; c < 3U; ++c) {
            const char *key;
            if (!v5_settings_axis_table_g53_value_is_editable(r, c)) continue;
            if (g53_value_is_native_geometry_owner(r, c)) continue;
            key = v5_settings_axis_g53_field_key(r, c);
            if (key && v5_settings_axis_resident_parameter_table_read_axis(g53_disk_table(r, c), "G53", key, value, sizeof(value))) {
                v5_settings_axis_set_g53_value(r, c, value, 1);
            }
        }
    }
}

void v5_settings_axis_set_motion_model_value(const char *value, int real)
{
    if (!value || !value[0]) {
        return;
    }
    snprintf(g_v5_axis_table_motion_model_value, V5_AXIS_VALUE_CAP, "%s", value);
    g_v5_axis_table_motion_model_real = real ? 1U : 0U;
}

static void set_bus_pulse_value(const char *value, int real)
{
    if (!value || !value[0]) {
        return;
    }
    snprintf(g_v5_axis_table_bus_pulse_value, V5_AXIS_VALUE_CAP, "%s", value);
    g_v5_axis_table_bus_pulse_real = real ? 1U : 0U;
}

void v5_settings_axis_load_self_setting_parameter_table(void)
{
    char value[V5_AXIS_VALUE_CAP];
    if (v5_settings_axis_resident_parameter_table_read_axis(V5_SETTINGS_PARAMETER_DISK_SELF,
                                           "SETTINGS", "bus_pulse_setting", value, sizeof(value))) {
        set_bus_pulse_value(value, 1);
    }
    v5_settings_axis_load_slave_options_from_resident_self_table();
}

void v5_settings_axis_parse_runtime_ini_text(const char *text)
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
    while (v5_settings_axis_next_text_line(&cursor, line, sizeof(line))) {
        char *eq;
        v5_settings_axis_trim_in_place(line);
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
                    unsigned int idx = v5_settings_axis_axis_letter_index(axis);
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
        v5_settings_axis_trim_in_place(line);
        v5_settings_axis_trim_in_place(eq + 1);
        if (current_traj) {
            if (v5_settings_axis_same_key(line, "COORDINATES")) {
                unsigned int pos = 0U;
                const char *p = eq + 1;
                while (*p && pos < V5_AXIS_TABLE_MAX_ROWS) {
                    if (isalpha((unsigned char)*p)) {
                        unsigned int idx = v5_settings_axis_axis_letter_index((char)toupper((unsigned char)*p));
                        if (idx < V5_AXIS_TABLE_MAX_ROWS) {
                            joint_axis_map[pos++] = (int)idx;
                        }
                    }
                    ++p;
                }
            }
        } else if (current_axis >= 0) {
            V5AxisIniRow *row = &rows[current_axis];
            if (v5_settings_axis_same_key(line, "TYPE")) v5_settings_axis_copy_axis_value(row->type, sizeof(row->type), eq + 1);
            else if (v5_settings_axis_same_key(line, "DIRECTION_MODE")) v5_settings_axis_copy_axis_value(row->direction_mode, sizeof(row->direction_mode), eq + 1);
            else if (v5_settings_axis_same_key(line, "MIN_LIMIT")) v5_settings_axis_copy_axis_value(row->min_limit, sizeof(row->min_limit), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_LIMIT")) v5_settings_axis_copy_axis_value(row->max_limit, sizeof(row->max_limit), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_VELOCITY")) v5_settings_axis_copy_axis_value(row->max_velocity, sizeof(row->max_velocity), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_ACCELERATION")) v5_settings_axis_copy_axis_value(row->max_acceleration, sizeof(row->max_acceleration), eq + 1);
            else if (v5_settings_axis_same_key(line, "PITCH")) v5_settings_axis_copy_axis_value(row->pitch, sizeof(row->pitch), eq + 1);
            else if (v5_settings_axis_same_key(line, "MOTOR_REV")) v5_settings_axis_copy_axis_value(row->motor_rev, sizeof(row->motor_rev), eq + 1);
            else if (v5_settings_axis_same_key(line, "LOAD_REV")) v5_settings_axis_copy_axis_value(row->load_rev, sizeof(row->load_rev), eq + 1);
            else if (v5_settings_axis_same_key(line, "SCALE")) v5_settings_axis_copy_axis_value(row->scale, sizeof(row->scale), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME")) v5_settings_axis_copy_axis_value(row->home, sizeof(row->home), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME_SEQUENCE")) v5_settings_axis_copy_axis_value(row->home_sequence, sizeof(row->home_sequence), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME_SEARCH_VEL")) v5_settings_axis_copy_axis_value(row->home_search_vel, sizeof(row->home_search_vel), eq + 1);
            else if (v5_settings_axis_same_key(line, "BACKLASH")) v5_settings_axis_copy_axis_value(row->backlash, sizeof(row->backlash), eq + 1);
        } else if (current_joint >= 0) {
            V5AxisIniRow *row = &rows[current_joint];
            if (v5_settings_axis_same_key(line, "TYPE") && !row->type[0]) v5_settings_axis_copy_axis_value(row->type, sizeof(row->type), eq + 1);
            else if (v5_settings_axis_same_key(line, "DIRECTION_MODE") && !row->direction_mode[0]) v5_settings_axis_copy_axis_value(row->direction_mode, sizeof(row->direction_mode), eq + 1);
            else if (v5_settings_axis_same_key(line, "MIN_LIMIT") && !row->min_limit[0]) v5_settings_axis_copy_axis_value(row->min_limit, sizeof(row->min_limit), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_LIMIT") && !row->max_limit[0]) v5_settings_axis_copy_axis_value(row->max_limit, sizeof(row->max_limit), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_VELOCITY") && !row->max_velocity[0]) v5_settings_axis_copy_axis_value(row->max_velocity, sizeof(row->max_velocity), eq + 1);
            else if (v5_settings_axis_same_key(line, "MAX_ACCELERATION") && !row->max_acceleration[0]) v5_settings_axis_copy_axis_value(row->max_acceleration, sizeof(row->max_acceleration), eq + 1);
            else if (v5_settings_axis_same_key(line, "PITCH")) v5_settings_axis_copy_axis_value(row->pitch, sizeof(row->pitch), eq + 1);
            else if (v5_settings_axis_same_key(line, "MOTOR_REV")) v5_settings_axis_copy_axis_value(row->motor_rev, sizeof(row->motor_rev), eq + 1);
            else if (v5_settings_axis_same_key(line, "LOAD_REV")) v5_settings_axis_copy_axis_value(row->load_rev, sizeof(row->load_rev), eq + 1);
            else if (v5_settings_axis_same_key(line, "SCALE")) v5_settings_axis_copy_axis_value(row->scale, sizeof(row->scale), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME")) v5_settings_axis_copy_axis_value(row->home, sizeof(row->home), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME_SEQUENCE")) v5_settings_axis_copy_axis_value(row->home_sequence, sizeof(row->home_sequence), eq + 1);
            else if (v5_settings_axis_same_key(line, "HOME_SEARCH_VEL")) v5_settings_axis_copy_axis_value(row->home_search_vel, sizeof(row->home_search_vel), eq + 1);
            else if (v5_settings_axis_same_key(line, "BACKLASH")) v5_settings_axis_copy_axis_value(row->backlash, sizeof(row->backlash), eq + 1);
        } else if (current_rtcp) {
            if (v5_settings_axis_same_key(line, "MOTION_MODEL") || v5_settings_axis_same_key(line, "MODEL")) v5_settings_axis_set_motion_model_value(eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_A_Y")) v5_settings_axis_set_g53_value(0U, 1U, eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_A_Z")) v5_settings_axis_set_g53_value(0U, 2U, eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_B_X")) v5_settings_axis_set_g53_value(1U, 0U, eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_B_Z")) v5_settings_axis_set_g53_value(1U, 2U, eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_C_X")) v5_settings_axis_set_g53_value(2U, 0U, eq + 1, 1);
            else if (v5_settings_axis_same_key(line, "G53_C_Y")) v5_settings_axis_set_g53_value(2U, 1U, eq + 1, 1);
        }
    }

    for (i = 0U; i < V5_AXIS_TABLE_MAX_ROWS; ++i) {
        V5AxisIniRow *row = &rows[i];
        if (row->type[0]) {
            v5_settings_axis_set_value(i, "axis_mode", strcmp(row->type, "ANGULAR") == 0 ? "旋转" : (strcmp(row->type, "VIRTUAL") == 0 ? "虚拟" : "直线"), 1);
        }
        if (row->direction_mode[0]) v5_settings_axis_set_value(i, "direction_mode", row->direction_mode, 1);
        if (row->scale[0]) {
            double scale = strtod(row->scale, 0);
            if (scale > 0.0) {
                v5_settings_axis_set_value_from_double(i, "precision", 1.0 / scale);
            }
        }
        if (row->pitch[0]) v5_settings_axis_set_value(i, "pitch", row->pitch, 1);
        if (row->motor_rev[0]) v5_settings_axis_set_value(i, "motor_rev", row->motor_rev, 1);
        if (row->load_rev[0]) v5_settings_axis_set_value(i, "load_rev", row->load_rev, 1);
        if (row->home_sequence[0]) v5_settings_axis_set_value(i, "home_order", strcmp(row->home_sequence, "999") == 0 ? "禁用" : row->home_sequence, 1);
        if (row->home_search_vel[0]) {
            double vel = strtod(row->home_search_vel, 0);
            if (vel > 0.0) v5_settings_axis_set_value(i, "home_direction", "+", 1);
            else if (vel < 0.0) v5_settings_axis_set_value(i, "home_direction", "-", 1);
            else v5_settings_axis_set_value(i, "home_direction", "0", 1);
        }
        if (row->min_limit[0]) v5_settings_axis_set_value(i, "soft_minus", row->min_limit, 1);
        if (row->home[0]) v5_settings_axis_set_value(i, "zero", row->home, 1);
        if (row->max_limit[0]) v5_settings_axis_set_value(i, "soft_plus", row->max_limit, 1);
        if (row->max_velocity[0]) v5_settings_axis_set_max_velocity_from_runtime_ini(i, row->max_velocity);
        if (row->max_acceleration[0]) v5_settings_axis_set_value(i, "max_acceleration", row->max_acceleration, 1);
        if (row->backlash[0]) v5_settings_axis_set_value(i, "backlash", row->backlash, 1);
    }
}

void v5_settings_axis_mark_missing_encoder_bits_unavailable(void)
{
    int col = v5_settings_axis_column_index("encoder_bits");
    unsigned int r;
    if (col < 0) return;
    for (r = 0U; r < v5_settings_axis_table_row_count(); ++r) {
        if (!g_v5_axis_table_value_real[r][(unsigned int)col]) {
            v5_settings_axis_set_value(r, "encoder_bits", "--", 0);
        }
    }
}
