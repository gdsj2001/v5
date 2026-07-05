#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#include <stdio.h>
#include <stdlib.h>

static int v5_program_runtime_write_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G90\nG0 X0 Y0\nM2\n", fp);
    fclose(fp);
    return 1;
}

int main(void)
{
    const char *path = "v5_program_runtime_smoke.ngc";
    V5ProgramRuntime runtime;
    V5ProgramOpenResult open_result;
    V5CommandRequest start_request;
    V5CommandPrepared prepared;
    int ok;

    if (!v5_program_runtime_write_input(path)) {
        return 1;
    }

    v5_program_runtime_init(&runtime);
    ok = v5_program_runtime_open_file(&runtime, path, &open_result);
    remove(path);

    if (!ok || !open_result.ok) {
        v5_program_runtime_destroy(&runtime);
        return 2;
    }
    if (!v5_program_runtime_prepare_start(&runtime, &start_request)) {
        v5_program_runtime_destroy(&runtime);
        return 3;
    }
    if (!v5_command_gate_prepare(&start_request, &prepared)) {
        v5_program_runtime_destroy(&runtime);
        return 4;
    }

    printf(
        "v5 program runtime: open=%d generation=%u bytes=%lu lines=%u display=%s start=%s owner=%s\n",
        open_result.ok,
        open_result.generation,
        (unsigned long)open_result.byte_count,
        open_result.line_count,
        open_result.display_name,
        prepared.name,
        prepared.owner);

    v5_program_runtime_destroy(&runtime);
    return 0;
}
