#include "v5_command_gate.h"

#include "v5_command_table.h"

#include <string.h>

static const char *v5_command_gate_name_for_kind(V5CommandKind kind)
{
    switch (kind) {
    case V5_COMMAND_PROGRAM_OPEN:
        return "program_open";
    case V5_COMMAND_START:
        return "start";
    case V5_COMMAND_MDI_RUN:
        return "mdi_run";
    case V5_COMMAND_PAUSE:
        return "pause";
    case V5_COMMAND_RESUME:
        return "resume";
    case V5_COMMAND_HOME:
        return "home";
    case V5_COMMAND_JOG_INCREMENT:
        return "jog_increment";
    case V5_COMMAND_JOG_CONTINUOUS:
        return "jog_continuous";
    case V5_COMMAND_JOG_STOP:
        return "jog_stop";
    case V5_COMMAND_ESTOP_FORCE:
        return "estop_force";
    case V5_COMMAND_ESTOP_RESET:
        return "estop_reset";
    case V5_COMMAND_WCS_SELECT:
        return "wcs_select";
    case V5_COMMAND_WORK_ZERO:
        return "work_zero";
    case V5_COMMAND_G92_CLEAR:
        return "g92_clear";
    case V5_COMMAND_RTCP_SET:
        return "rtcp_set";
    case V5_COMMAND_FEED_OVERRIDE_SET:
        return "feed_override_set";
    case V5_COMMAND_SPINDLE_OVERRIDE_SET:
        return "spindle_override_set";
    case V5_COMMAND_FIRST_POINT:
        return "first_point";
    case V5_COMMAND_ROTARY_EQUIV_ZERO:
        return "rotary_equiv_zero";
    default:
        return 0;
    }
}

int v5_command_gate_prepare(const V5CommandRequest *request, V5CommandPrepared *prepared)
{
    const char *name;
    const V5CommandEntry *entries;
    size_t count;
    size_t i;

    if (!request || !prepared) {
        return 0;
    }

    memset(prepared, 0, sizeof(*prepared));
    name = v5_command_gate_name_for_kind(request->kind);
    if (!name) {
        return 0;
    }

    entries = v5_command_table_entries(&count);
    for (i = 0; i < count; ++i) {
        if (strcmp(entries[i].name, name) == 0) {
            prepared->kind = request->kind;
            prepared->name = entries[i].name;
            prepared->owner = entries[i].owner;
            prepared->accepted = 1;
            return 1;
        }
    }

    return 0;
}
