#include "v5_native_modal_tool_status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "v5_native_modal_tool_status_smoke.bin";
    V5NativeReadback readback;

    unlink(path);
    v5_native_readback_init(&readback);
    if (v5_native_modal_tool_status_read(path, 1000U, &readback) || v5_native_readback_modal_known(&readback)) {
        return 1;
    }
    if (!strstr(readback.unavailable_reason, "missing")) {
        return 2;
    }
    if (!v5_native_modal_tool_status_write_ex(
            path,
            1,
            "G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97",
            1,
            7,
            1,
            123.456,
            1,
            1,
            1,
            0,
            1,
            1,
            1,
            12,
            1,
            9,
            1,
            1,
            12,
            "G4 P0")) {
        return 3;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_modal_tool_status_read(path, 1000U, &readback) ||
        !v5_native_readback_modal_known(&readback) ||
        strcmp(readback.modal_text, "G0 G17 G21 G40 G49 G54 G64 G80 G90 G94 G97") != 0 ||
        !v5_native_readback_tool_known(&readback) ||
        readback.tool_number != 7 ||
        !v5_native_readback_tool_length_known(&readback) ||
        !v5_native_readback_interpreter_idle_known(&readback) ||
        !readback.interpreter_idle ||
        !v5_native_readback_current_line_known(&readback) ||
        readback.current_line != 12 ||
        !v5_native_readback_motion_line_known(&readback) ||
        readback.motion_line != 9 ||
        !v5_native_readback_mdi_run_known(&readback) ||
        !readback.mdi_run_active ||
        readback.mdi_run_line != 12 ||
        strcmp(readback.mdi_run_command, "G4 P0") != 0 ||
        !v5_native_readback_all_homed_known(&readback) ||
        !readback.all_homed ||
        fabs(readback.tool_length_mm - 123.456) > 0.0001) {
        unlink(path);
        return 4;
    }
    v5_native_readback_clear_all_homed(&readback);
    if (v5_native_readback_all_homed_known(&readback) || readback.all_homed) {
        unlink(path);
        return 7;
    }
    if (!v5_native_modal_tool_status_write(path, 0, "", 0, -1, 0, 0.0)) {
        unlink(path);
        return 5;
    }
    v5_native_readback_init(&readback);
    if (v5_native_modal_tool_status_read(path, 1000U, &readback) || v5_native_readback_modal_known(&readback)) {
        unlink(path);
        return 6;
    }
    unlink(path);
    printf("v5 native modal/tool status: modal=memory tool=T7 length=123.456 homed=1 current_line=12 motion_line=9 invalid=fail_closed\n");
    return 0;
}
