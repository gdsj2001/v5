#include "v5_command_program.h"
#include "v5_command_start.h"
#include "v5_linuxcncrsh_client.h"

#include <stdio.h>
#include <stdlib.h>

static int v5_program_command_write_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G90\nG1 X1.0 F100\nM2\n", fp);
    fclose(fp);
    return 1;
}

int main(void)
{
    const char *path = "v5_program_command_smoke.ngc";
    V5ProgramController controller;
    V5ProgramOpenResult open_result;
    V5CommandPrepared start_prepared;
    V5CommandRequest start_request;
    char line[128];
    int ok;

    if (!v5_program_command_write_input(path)) {
        return 1;
    }

    v5_program_controller_init(&controller);
    ok = v5_command_program_open(&controller, path, &open_result);
    remove(path);
    if (!ok || !open_result.ok) {
        v5_program_controller_destroy(&controller);
        return 2;
    }

    if (!v5_command_start_prepare(
            v5_program_controller_runtime(&controller),
            &start_prepared)) {
        v5_program_controller_destroy(&controller);
        return 3;
    }

    if (!v5_program_runtime_prepare_start(
            v5_program_controller_runtime(&controller),
            &start_request)) {
        v5_program_controller_destroy(&controller);
        return 4;
    }
    if (!v5_linuxcncrsh_format_line(&start_prepared, &start_request, line, sizeof(line))) {
        v5_program_controller_destroy(&controller);
        return 5;
    }

    printf(
        "v5 program command: open=%d generation=%u bytes=%lu lines=%u display=%s start=%s owner=%s line=%s\n",
        open_result.ok,
        open_result.generation,
        (unsigned long)open_result.byte_count,
        open_result.line_count,
        open_result.display_name,
        start_prepared.name,
        start_prepared.owner,
        line);

    v5_program_controller_destroy(&controller);
    return 0;
}
