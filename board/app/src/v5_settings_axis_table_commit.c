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

int v5_settings_axis_current_driver_mode_is_pulse(void)
{
    const char *mode = v5_settings_axis_table_bus_pulse_value();
    if (!mode) return 0;
    return strstr(mode, "pulse") || strstr(mode, "Pulse") || strstr(mode, "PULSE") || strstr(mode, "脉冲");
}

int v5_settings_axis_axis_zero_col_index(void)
{
    return v5_settings_axis_column_index("zero");
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
    row_spec = &g_v5_axis_table_rows[row];
    if (!row_spec->enabled) {
        return 0;
    }
    pulse = v5_settings_axis_current_driver_mode_is_pulse();
    driver_mode = pulse ? "pulse" : "bus";
    target_scope = pulse ? "pulse_mechanical_home_offset" : "bus_count_domain_zero";
    apply_mode = pulse ? "persist_home_offset" : "count_domain_zero";
    if (v5_settings_axis_axis_row_slave_is_nat(row)) {
        log_axis_zero_request_event(row_spec, driver_mode, target_scope, apply_mode, "", "", 0);
        return 0;
    }
    slave_index[0] = '\0';
    slave_value = v5_settings_axis_row_value(row, "slave");
    v5_settings_axis_slave_option_extract_id(slave_value, slave_index, sizeof(slave_index));
    if (v5_settings_axis_slave_id_is_nat(slave_index)) {
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
    if (g_v5_axis_table_axis_zero_callback) {
        g_v5_axis_table_axis_zero_callback(row_spec->axis, driver_mode, target_scope, apply_mode, slave_index, home_offset ? home_offset : "", g_v5_axis_table_axis_zero_callback_user_data);
    }
    return 1;
}


static void set_axis_value_from_double(unsigned int row, const char *field_key, double value)
{
    char text[V5_AXIS_VALUE_CAP];
    if (!field_key || row >= v5_settings_axis_table_row_count() || !isfinite(value)) {
        return;
    }
    v5_settings_axis_format_double_compact(text, sizeof(text), value);
    v5_settings_axis_set_value(row, field_key, text, 1);
}

int v5_settings_axis_table_commit_g53_value(unsigned int row, unsigned int col, const char *value)
{
    const char *key;
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult commit_result;
    char id[96];
    int ok;
    if (!v5_settings_axis_table_g53_value_is_editable(row, col) || !value || !value[0]) return 0;
    key = v5_settings_axis_g53_field_key(row, col);
    if (!key) return 0;
    if (!v5_settings_axis_g53_field_id(row, col, id, sizeof(id))) {
        snprintf(id, sizeof(id), "g53_%u_%u", row, col);
    }
    memset(&request, 0, sizeof(request));
    memset(&commit_result, 0, sizeof(commit_result));
    request.project_root = g_v5_axis_table_project_root;
    request.axis = "G53";
    request.axis_index = row;
    request.field_key = key;
    request.field_name = id;
    request.value_text = value;
    request.owner_generation = v5_settings_axis_settings_axis_owner_generation(id);
    request.readback_token = v5_settings_axis_settings_axis_readback_token(id, value);
    ok = v5_command_gate_settings_axis_commit(&request, &commit_result, 3000U);
    if (ok) {
        v5_settings_axis_set_g53_value(row, col, commit_result.readback_value, 1);
    }
    log_axis_param_event(id, value, ok);
    if (ok) {
        v5_settings_axis_notify_settings_axis_commit_success();
    }
    return ok;
}

static void apply_motion_model_binding_readback(const char *value)
{
    const V5MotionModelDescriptor *target = v5_motion_model_find(value);
    const V5MotionModelDescriptor *alternate = 0;
    char target_axis[2];
    char alternate_axis[2];
    char target_slave[V5_AXIS_VALUE_CAP];
    char alternate_slave[V5_AXIS_VALUE_CAP];
    const char *new_target = 0;
    const char *new_alternate = 0;
    int target_row;
    int alternate_row;
    unsigned int slave_col;
    size_t i;
    if (!target) {
        return;
    }
    for (i = 0U; i < v5_motion_model_registry_count(); ++i) {
        const V5MotionModelDescriptor *candidate = v5_motion_model_registry_at(i);
        if (candidate && candidate->first_status_slot == target->first_status_slot &&
            candidate->first_rotary_axis != target->first_rotary_axis) {
            alternate = candidate;
            break;
        }
    }
    if (!alternate) {
        return;
    }
    target_axis[0] = target->first_rotary_axis;
    target_axis[1] = '\0';
    alternate_axis[0] = alternate->first_rotary_axis;
    alternate_axis[1] = '\0';
    target_row = v5_settings_axis_row_index_for_axis_name(target_axis);
    alternate_row = v5_settings_axis_row_index_for_axis_name(alternate_axis);
    if (target_row < 0 || alternate_row < 0) {
        return;
    }
    for (slave_col = 0U; slave_col < v5_settings_axis_table_column_count(); ++slave_col) {
        if (strcmp(g_v5_axis_table_columns[slave_col].field_key, "slave") == 0) {
            break;
        }
    }
    if (slave_col >= v5_settings_axis_table_column_count()) {
        return;
    }
    snprintf(target_slave, sizeof(target_slave), "%s", v5_settings_axis_table_value((unsigned int)target_row, slave_col));
    snprintf(alternate_slave, sizeof(alternate_slave), "%s", v5_settings_axis_table_value((unsigned int)alternate_row, slave_col));
    if (strcmp(target_slave, "NAT") == 0 && strcmp(alternate_slave, "NAT") != 0) {
        new_target = alternate_slave;
        new_alternate = "NAT";
    } else if (strcmp(target_slave, "NAT") != 0 && strcmp(alternate_slave, target_slave) == 0) {
        new_target = target_slave;
        new_alternate = "NAT";
    } else {
        return;
    }
    if (!v5_settings_axis_resident_parameter_table_set_axis(
            V5_SETTINGS_PARAMETER_DISK_SELF, target_axis, "slave", new_target) ||
        !v5_settings_axis_resident_parameter_table_set_axis(
            V5_SETTINGS_PARAMETER_DISK_SELF, alternate_axis, "slave", new_alternate)) {
        return;
    }
    v5_settings_axis_set_value((unsigned int)target_row, "slave", new_target, 1);
    v5_settings_axis_set_value((unsigned int)alternate_row, "slave", new_alternate, 1);
    v5_settings_axis_apply_drive_display_for_row((unsigned int)target_row);
    v5_settings_axis_apply_drive_display_for_row((unsigned int)alternate_row);
    v5_settings_axis_refresh_axis_row_refs((unsigned int)target_row);
    v5_settings_axis_refresh_axis_row_refs((unsigned int)alternate_row);
}

int v5_settings_axis_table_commit_motion_model(const char *value)
{
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult commit_result;
    int ok;
    if (!value || !value[0]) {
        return 0;
    }
    memset(&request, 0, sizeof(request));
    memset(&commit_result, 0, sizeof(commit_result));
    request.project_root = g_v5_axis_table_project_root;
    request.axis = "RTCP";
    request.axis_index = 0U;
    request.field_key = "motion_model";
    request.field_name = "motion_model";
    request.value_text = value;
    request.owner_generation = v5_settings_axis_settings_axis_owner_generation("motion_model");
    request.readback_token = v5_settings_axis_settings_axis_readback_token("motion_model", value);
    ok = v5_command_gate_settings_axis_commit(&request, &commit_result, 3000U);
    log_axis_param_event("motion_model", value, ok);
    if (ok) {
        apply_motion_model_binding_readback(commit_result.readback_value);
        v5_settings_axis_set_motion_model_value(commit_result.readback_value, 1);
        v5_settings_axis_notify_settings_axis_commit_success();
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
    row_spec = &g_v5_axis_table_rows[row];
    col_spec = &g_v5_axis_table_columns[col];
    if (v5_settings_axis_table_cell_is_disabled(row, col)) {
        return 0;
    }
    if (strcmp(col_spec->field_key, "zero") == 0) {
        return 0;
    }
    if (strcmp(col_spec->field_key, "encoder_bits") == 0 &&
        (strcmp(value, "NAT") == 0 || strcmp(value, "--") == 0)) {
        return 0;
    }
    if (v5_settings_axis_axis_field_requires_integer_value(col_spec->field_key) &&
        !v5_settings_axis_axis_integer_text_is_valid(value)) {
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
    request.project_root = g_v5_axis_table_project_root;
    request.axis = row_spec->axis;
    request.axis_index = row;
    request.field_key = col_spec->field_key;
    request.field_name = id;
    request.value_text = value;
    request.owner_generation = v5_settings_axis_settings_axis_owner_generation(id);
    request.readback_token = v5_settings_axis_settings_axis_readback_token(id, value);
    ok = v5_command_gate_settings_axis_commit(&request, &commit_result, 3000U);
    if (ok && commit_result.scale_chain.raw_limits_recomputed) {
        set_axis_value_from_double(row, "soft_minus", commit_result.scale_chain.raw_min_limit);
        set_axis_value_from_double(row, "soft_plus", commit_result.scale_chain.raw_max_limit);
        if (commit_result.scale_chain.scale_recomputed && commit_result.scale_chain.effective_scale > 0.0) {
            set_axis_value_from_double(row, "precision", 1.0 / commit_result.scale_chain.effective_scale);
        }
    }
    if (ok) {
        v5_settings_axis_set_value(row, col_spec->field_key, commit_result.readback_value, 1);
        if (v5_settings_axis_same_key(col_spec->field_key, "slave")) {
            v5_settings_axis_apply_drive_display_for_row(row);
        }
    }
    {
        log_axis_param_event(id, value, ok);
    }
    if (ok) {
        v5_settings_axis_notify_settings_axis_commit_success();
    }
    return ok;
}
