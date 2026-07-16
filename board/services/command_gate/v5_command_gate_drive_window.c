#include "v5_command_gate.h"
#include "v5_command_gate_ipc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_id_ok(const char *run_id)
{
    const unsigned char *p = (const unsigned char *)run_id;
    size_t length = 0U;
    if (!p || !*p) {
        return 0;
    }
    while (*p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == ':')) {
            return 0;
        }
        if (++length > 64U) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int emit_result(
    int ok,
    const char *code,
    const char *run_id,
    int initial_machine_enabled,
    int final_machine_enabled)
{
    printf(
        "{\"ok\":%s,\"code\":\"%s\",\"run_id\":\"%s\","
        "\"initial_machine_enabled\":%s,\"final_machine_enabled\":%s}\n",
        ok ? "true" : "false",
        code && code[0] ? code : "COMMAND_GATE_UNAVAILABLE",
        run_id && run_id[0] ? run_id : "",
        initial_machine_enabled ? "true" : "false",
        final_machine_enabled ? "true" : "false");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    V5CommandRequest request;
    V5CommandPrepared prepared;
    V5CommandGateResult result;
    const char *run_id = argc > 2 ? argv[2] : "";
    int restore = 0;
    int ok;

    memset(&request, 0, sizeof(request));
    if (argc < 3 || !run_id_ok(run_id)) {
        return emit_result(0, "DRIVE_WRITE_WINDOW_BAD_ARGUMENTS", run_id, 0, 0);
    }
    if (strcmp(argv[1], "begin") == 0 && argc == 3) {
        request.kind = V5_COMMAND_DRIVE_WRITE_BEGIN;
    } else if (strcmp(argv[1], "finish") == 0 && argc == 4 &&
               (strcmp(argv[3], "0") == 0 || strcmp(argv[3], "1") == 0)) {
        request.kind = V5_COMMAND_DRIVE_WRITE_FINISH;
        restore = argv[3][0] == '1';
        request.enabled_value = restore;
    } else if (strcmp(argv[1], "abort") == 0 && argc == 3) {
        request.kind = V5_COMMAND_DRIVE_WRITE_ABORT;
    } else {
        return emit_result(0, "DRIVE_WRITE_WINDOW_BAD_ARGUMENTS", run_id, 0, 0);
    }
    request.text_value = run_id;
    if (!v5_command_gate_prepare(&request, &prepared) || !prepared.accepted) {
        return emit_result(0, "DRIVE_WRITE_WINDOW_COMMAND_REJECTED", run_id, 0, 0);
    }
    ok = v5_command_gate_send_prepared(&prepared, &request, &result, 5000U) &&
         result.send_status == V5_COMMAND_GATE_SEND_SENT && result.executed &&
         result.machine_enable_known;
    return emit_result(
        ok,
        result.readback_code,
        run_id,
        result.drive_window_initial_machine_enabled,
        result.drive_window_final_machine_enabled);
}
