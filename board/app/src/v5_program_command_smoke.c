#include "v5_command_program.h"
#include "v5_command_start.h"
#include "v5_linuxcncrsh_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char line[384];
    unsigned int program_preview_count;
    unsigned int program_loaded_epoch;
    char program_source_path[384];
    char program_source_sha256[65];
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

    if (v5_program_controller_runtime(&controller)->preview_trajectory_count < 2U) {
        v5_program_controller_destroy(&controller);
        return 3;
    }
    if (!v5_program_runtime_has_first_point_metadata(v5_program_controller_runtime(&controller)) ||
        strcmp(v5_program_runtime_source_path(v5_program_controller_runtime(&controller)), path) != 0 ||
        strcmp(v5_program_runtime_source_sha256(v5_program_controller_runtime(&controller)), "92ca1bb7da9d380699ee0252adb25d5013ceebe6b011a0d58334894357e679e8") != 0) {
        v5_program_controller_destroy(&controller);
        return 12;
    }
    program_preview_count = v5_program_controller_runtime(&controller)->preview_trajectory_count;
    program_loaded_epoch = open_result.loaded_epoch;
    snprintf(program_source_path, sizeof(program_source_path), "%s", open_result.source_path);
    snprintf(program_source_sha256, sizeof(program_source_sha256), "%s", open_result.source_sha256);

    if (!v5_command_start_prepare(
            v5_program_controller_runtime(&controller),
            &start_prepared)) {
        v5_program_controller_destroy(&controller);
        return 4;
    }

    if (!v5_program_runtime_prepare_start(
            v5_program_controller_runtime(&controller),
            &start_request)) {
        v5_program_controller_destroy(&controller);
        return 5;
    }
    if (!v5_linuxcncrsh_format_line(&start_prepared, &start_request, line, sizeof(line))) {
        v5_program_controller_destroy(&controller);
        return 6;
    }
    if (strcmp(line, "Set Open v5_program_command_smoke.ngc\nSet Mode Auto\nSet Run 0") != 0) {
        v5_program_controller_destroy(&controller);
        return 13;
    }

    if (!v5_program_runtime_set_mdi_line(&controller.runtime, "G4 P0")) {
        v5_program_controller_destroy(&controller);
        return 7;
    }
    if (!v5_command_start_prepare(v5_program_controller_runtime(&controller), &start_prepared)) {
        v5_program_controller_destroy(&controller);
        return 8;
    }
    if (!v5_program_runtime_prepare_start(v5_program_controller_runtime(&controller), &start_request)) {
        v5_program_controller_destroy(&controller);
        return 9;
    }
    if (!v5_linuxcncrsh_format_line(&start_prepared, &start_request, line, sizeof(line))) {
        v5_program_controller_destroy(&controller);
        return 10;
    }
    if (strcmp(start_prepared.name, "mdi_run") != 0 || strcmp(line, "Set MDI G4 P0") != 0) {
        v5_program_controller_destroy(&controller);
        return 11;
    }

    printf(
        "v5 program command: open=%d generation=%u epoch=%u bytes=%lu lines=%u preview=%u display=%s source=%s sha256=%s start=%s owner=%s line=%s\n",
        open_result.ok,
        open_result.generation,
        program_loaded_epoch,
        (unsigned long)open_result.byte_count,
        open_result.line_count,
        program_preview_count,
        open_result.display_name,
        program_source_path,
        program_source_sha256,
        start_prepared.name,
        start_prepared.owner,
        line);

    v5_program_controller_destroy(&controller);
    return 0;
}
