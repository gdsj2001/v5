#include "v5_command_motion.h"
#include "v5_command_override.h"
#include "v5_command_rtcp.h"
#include "v5_command_safety.h"
#include "v5_command_wcs.h"
#include "v5_linuxcncrsh_client.h"
#include "v5_native_readback.h"

#include <stdio.h>
#include <string.h>

typedef int (*V5PrepareFn)(V5CommandPrepared *, V5CommandRequest *);

static int expect_line(const char *label, const char *expected, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    char line[128];
    if (!v5_linuxcncrsh_format_line(prepared, request, line, sizeof(line))) {
        return 0;
    }
    if (strcmp(line, expected) != 0) {
        printf("%s mismatch: %s != %s\n", label, line, expected);
        return 0;
    }
    printf("%s=%s\n", label, line);
    return 1;
}

static int check_simple(const char *label, const char *expected, V5PrepareFn fn)
{
    V5CommandPrepared prepared;
    V5CommandRequest request;
    return fn(&prepared, &request) && expect_line(label, expected, &prepared, &request);
}

static int expect_estop_reset_sequence(void)
{
    char line[128];
    if (!v5_linuxcncrsh_format_estop_reset_sequence(line, sizeof(line))) {
        return 0;
    }
    if (strcmp(line, "Set EStop Off | Get Estop | Set Machine On") != 0) {
        printf("estop_reset_sequence mismatch: %s\n", line);
        return 0;
    }
    printf("estop_reset_sequence=%s\n", line);
    return 1;
}

int main(void)
{
    V5CommandPrepared prepared;
    V5CommandRequest request;
    V5NativeReadback readback;
    int rtcp_enabled = 0;

    if (!check_simple("pause", "Set Pause", v5_command_pause_prepare)) {
        return 1;
    }
    if (!check_simple("resume", "Set Resume", v5_command_resume_prepare)) {
        return 2;
    }
    {
        char home_line[64];
        if (!v5_command_home_prepare(&prepared, &request) ||
            !v5_linuxcncrsh_format_home_sequence(home_line, sizeof(home_line)) ||
            strcmp(home_line, "Set Mode Manual | Set Home -1") != 0) {
            return 12;
        }
        printf("home=%s\n", home_line);
    }
    if (!check_simple("estop", "Set Machine Off", v5_command_estop_force_prepare)) {
        return 3;
    }
    if (!check_simple("estop_reset", "Set EStop Off", v5_command_estop_reset_prepare) || !expect_estop_reset_sequence()) {
        return 4;
    }
    if (!check_simple("g92_clear", "Set MDI G92.1", v5_command_g92_clear_prepare)) {
        return 5;
    }
    if (!v5_command_wcs_select_prepare(1, &prepared, &request) ||
        !expect_line("wcs", "Set MDI G55", &prepared, &request)) {
        return 6;
    }
    if (!v5_command_wcs_select_prepare(8, &prepared, &request) ||
        !expect_line("wcs_g593", "Set MDI G59.3", &prepared, &request)) {
        return 13;
    }
    if (!v5_command_work_zero_prepare(1, 'X', &prepared, &request) ||
        !expect_line("work_zero", "Set MDI G10 L20 P2 X0", &prepared, &request)) {
        return 7;
    }
    if (!v5_command_rtcp_prepare(1, &prepared, &request) ||
        !expect_line("rtcp_on", "Set MDI M128", &prepared, &request)) {
        return 8;
    }
    if (!v5_command_jog_increment_prepare('X', 10.0, 1, &prepared, &request) ||
        !expect_line("jog_plus", "Set Jog_Incr X 100.000 10.000", &prepared, &request)) {
        return 14;
    }
    if (!v5_command_jog_increment_prepare('X', 10.0, 0, &prepared, &request) ||
        !expect_line("jog_minus", "Set Jog_Incr X -100.000 10.000", &prepared, &request)) {
        return 15;
    }
    if (!v5_command_feed_override_prepare(120, &prepared, &request) ||
        !expect_line("feed_override", "Set Feed_Override 120", &prepared, &request)) {
        return 9;
    }
    if (!v5_command_spindle_override_prepare(80, &prepared, &request) ||
        !expect_line("spindle_override", "Set Spindle_Override 80", &prepared, &request)) {
        return 10;
    }

    v5_native_readback_init(&readback);
    v5_native_readback_set_rtcp_actual(&readback, 1);
    if (!v5_command_rtcp_actual_known(&readback, &rtcp_enabled) || !rtcp_enabled) {
        return 11;
    }
    if (!v5_command_rtcp_toggle_prepare(&readback, &prepared, &request) ||
        !expect_line("rtcp_toggle_off", "Set MDI M129", &prepared, &request)) {
        return 16;
    }
    v5_native_readback_set_rtcp_actual(&readback, 0);
    if (!v5_command_rtcp_toggle_prepare(&readback, &prepared, &request) ||
        !expect_line("rtcp_toggle_on", "Set MDI M128", &prepared, &request)) {
        return 17;
    }
    v5_native_readback_set_unavailable(&readback, "rtcp_missing");
    if (v5_command_rtcp_toggle_prepare(&readback, &prepared, &request)) {
        return 18;
    }

    printf("v5 command wrappers smoke passed\n");
    return 0;
}
