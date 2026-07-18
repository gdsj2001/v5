#include "v5_coordinate_panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char k_axes[V5_COORDINATE_AXIS_COUNT] = {'X', 'Y', 'Z', 'A', 'C'};

static void text_unavailable(char *out, size_t cap)
{
    if (!out || cap == 0u) {
        return;
    }
    snprintf(out, cap, "--.---");
}

static void text_value(char *out, size_t cap, double value)
{
    double scaled;
    if (!out || cap == 0u) {
        return;
    }
    scaled = value * 1000.0;
    if (isfinite(scaled)) {
        value = (scaled >= 0.0 ? floor(scaled + 1.0e-9) : ceil(scaled - 1.0e-9)) / 1000.0;
    }
    if (value == 0.0) {
        value = 0.0;
    }
    snprintf(out, cap, "%+010.3f", value);
}

static void text_speed(char *out, size_t cap, int valid, double value, const char *suffix)
{
    if (!valid) {
        snprintf(out, cap, "--");
        return;
    }
    if (value > -0.05 && value < 0.05) {
        value = 0.0;
    }
    snprintf(out, cap, "%.1f%s", value, suffix ? suffix : "");
}

void v5_coordinate_panel_from_status(const V5UiStatusView *status, V5CoordinatePanelSnapshot *panel)
{
    unsigned int i;
    int mcs_valid;
    int cmd_valid;

    if (!panel) {
        return;
    }
    memset(panel, 0, sizeof(*panel));

    if (!status) {
        for (i = 0; i < V5_COORDINATE_AXIS_COUNT; ++i) {
            panel->lines[i].axis = k_axes[i];
            text_unavailable(panel->lines[i].mcs_text, sizeof(panel->lines[i].mcs_text));
            text_unavailable(panel->lines[i].cmd_text, sizeof(panel->lines[i].cmd_text));
            text_unavailable(panel->lines[i].following_error_text, sizeof(panel->lines[i].following_error_text));
        }
        snprintf(panel->modal_text, sizeof(panel->modal_text), "--");
        snprintf(panel->spindle_speed_text, sizeof(panel->spindle_speed_text), "--");
        snprintf(panel->linear_velocity_text, sizeof(panel->linear_velocity_text), "--");
        snprintf(panel->feed_override_text, sizeof(panel->feed_override_text), "--");
        snprintf(panel->spindle_override_text, sizeof(panel->spindle_override_text), "--");
        return;
    }

    mcs_valid = (status->valid_mask & V5_STATUS_VALID_MCS) != 0u;
    cmd_valid = (status->valid_mask & V5_STATUS_VALID_CMD_MCS) != 0u;
    for (i = 0; i < V5_COORDINATE_AXIS_COUNT; ++i) {
        panel->lines[i].axis = k_axes[i];
        panel->lines[i].mcs_valid = mcs_valid;
        panel->lines[i].cmd_valid = cmd_valid;
        panel->lines[i].following_error_valid = mcs_valid && cmd_valid;
        if (mcs_valid) {
            text_value(panel->lines[i].mcs_text, sizeof(panel->lines[i].mcs_text), status->mcs[i]);
        } else {
            text_unavailable(panel->lines[i].mcs_text, sizeof(panel->lines[i].mcs_text));
        }
        if (cmd_valid) {
            text_value(panel->lines[i].cmd_text, sizeof(panel->lines[i].cmd_text), status->cmd_mcs[i]);
        } else {
            text_unavailable(panel->lines[i].cmd_text, sizeof(panel->lines[i].cmd_text));
        }
        if (mcs_valid && cmd_valid) {
            text_value(panel->lines[i].following_error_text, sizeof(panel->lines[i].following_error_text), status->raw_mcs[i] - status->raw_cmd_mcs[i]);
        } else {
            text_unavailable(panel->lines[i].following_error_text, sizeof(panel->lines[i].following_error_text));
        }
    }

    snprintf(panel->modal_text, sizeof(panel->modal_text), "--");
    text_speed(panel->spindle_speed_text, sizeof(panel->spindle_speed_text), (status->valid_mask & V5_STATUS_VALID_SPINDLE_SPEED) != 0u, status->spindle_speed_rpm, "rpm");
    text_speed(panel->linear_velocity_text, sizeof(panel->linear_velocity_text), (status->valid_mask & V5_STATUS_VALID_LINEAR_VELOCITY) != 0u, status->linear_velocity_mm_per_min, "mm/min");
    text_speed(panel->feed_override_text, sizeof(panel->feed_override_text), (status->valid_mask & V5_STATUS_VALID_FEED_OVERRIDE) != 0u, status->feedrate_override, "%");
    text_speed(panel->spindle_override_text, sizeof(panel->spindle_override_text), (status->valid_mask & V5_STATUS_VALID_SPINDLE_OVERRIDE) != 0u, status->spindle_override, "%");
}
