#include "v5_command_table.h"

static const V5CommandEntry kV5Commands[] = {
    {"program_open", "program_runtime"},
    {"start", "native_linuxcncrsh"},
    {"mdi_run", "native_linuxcncrsh"},
    {"pause", "native_linuxcncrsh"},
    {"resume", "native_linuxcncrsh"},
    {"home", "native_home_mode_gate"},
    {"jog_increment", "native_linuxcncrsh"},
    {"jog_continuous", "native_linuxcncrsh"},
    {"jog_stop", "native_linuxcncrsh"},
    {"estop_force", "native_safety"},
    {"estop_reset", "native_safety"},
    {"wcs_select", "native_linuxcncrsh"},
    {"work_zero", "native_work_zero"},
    {"g92_clear", "native_linuxcncrsh"},
    {"rtcp_set", "native_rtcp_control"},
    {"feed_override_set", "native_linuxcncrsh"},
    {"spindle_override_set", "native_linuxcncrsh"},
    {"first_point", "native_first_point"},
    {"axis_zero_position", "native_axis_zero_position"},
};

const V5CommandEntry *v5_command_table_entries(size_t *count)
{
    if (count) {
        *count = v5_command_table_count();
    }
    return kV5Commands;
}

size_t v5_command_table_count(void)
{
    return sizeof(kV5Commands) / sizeof(kV5Commands[0]);
}
