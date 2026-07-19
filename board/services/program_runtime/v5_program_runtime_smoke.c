#include "v5_command_gate.h"
#include "v5_program_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int v5_program_runtime_write_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs("G90\nG0 X0 Y0 Z5\nG1 X1 Y0 F100\nG1 X1 Y1\nM2\n", fp);
    fclose(fp);
    return 1;
}

static int v5_program_runtime_write_multisegment_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fputs(
        "G90\n"
        "G0 X0 Y0 Z5\n"
        "G1 X1 Y0 F100\n"
        "G1 X1 Y1\n"
        "G0 X5 Y5\n"
        "G53 G1 X9 Y9 Z0\n"
        "G0 X5 Y5 Z5\n"
        "G1 X6 Y5\n"
        "G1 X6 Y6\n"
        "M2\n",
        fp);
    fclose(fp);
    return 1;
}

static int v5_program_runtime_write_long_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    unsigned int i;
    if (!fp) {
        return 0;
    }
    fputs("G90\nG0 X0 Y0 Z0\n", fp);
    for (i = 0U; i < 700U; ++i) {
        fprintf(fp, "G1 X%u Y%u F100\n", i + 1U, i + 1U);
    }
    fclose(fp);
    return 1;
}

static int v5_program_runtime_write_modal_arc_bc_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    fputs(
        "G21G90G17\n"
        "G0X0Y0Z0\n"
        "G1X5Y0\n"
        "G3X10Y5I0J5\n"
        "G91G1X1B30C45\n"
        "G55\n"
        "G1X1\n",
        fp);
    fclose(fp);
    return 1;
}

static int v5_program_runtime_write_oversize_input(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, (long)V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    fputc('\n', fp);
    fclose(fp);
    return 1;
}

int main(void)
{
    const char *path = "v5_program_runtime_smoke.ngc";
    V5ProgramRuntime runtime;
    V5ProgramOpenResult open_result;
    V5ProgramDeleteResult delete_result;
    V5CommandRequest start_request;
    V5CommandPrepared prepared;
    unsigned int program_preview_count;
    unsigned int program_loaded_epoch;
    char program_source_path[384];
    char program_source_sha256[65];
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
    if (open_result.max_gcode_bytes != V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES ||
        open_result.preview_point_capacity != V5_PROGRAM_PREVIEW_POINT_COUNT ||
        !open_result.code || strcmp(open_result.code, "OK") != 0) {
        v5_program_runtime_destroy(&runtime);
        return 15;
    }
    if (runtime.preview_trajectory_count < 3U) {
        v5_program_runtime_destroy(&runtime);
        return 3;
    }
    if (v5_program_runtime_prepare_start(&runtime, &start_request) ||
        v5_program_runtime_publish_scene_ready(
            &runtime, open_result.loaded_epoch + 1U, 11ULL, 7U) ||
        v5_program_runtime_scene_ready(&runtime) ||
        v5_program_runtime_prepare_start(&runtime, &start_request) ||
        !v5_program_runtime_publish_scene_ready(
            &runtime, open_result.loaded_epoch, 11ULL, 7U) ||
        !v5_program_runtime_scene_ready(&runtime) ||
        !v5_program_runtime_prepare_start(&runtime, &start_request)) {
        v5_program_runtime_destroy(&runtime);
        return 4;
    }
    {
        int point_wcs_index = -1;
        if (v5_program_runtime_preview_program_wcs_index(&runtime) != 0 ||
            v5_program_runtime_preview_wcs_mask(&runtime) != 1U ||
            !v5_program_runtime_preview_wcs_index(&runtime, 0U, &point_wcs_index) ||
            point_wcs_index != 0) {
            v5_program_runtime_destroy(&runtime);
            return 14;
        }
    }
    {
        double first_point_axis[V5_COMMAND_AXIS_COUNT];
        unsigned int first_point_mask = 0U;
        if (!v5_program_runtime_first_point_axes(&runtime, first_point_axis, &first_point_mask) ||
            first_point_mask != (V5_COMMAND_AXIS_X_MASK | V5_COMMAND_AXIS_Y_MASK | V5_COMMAND_AXIS_Z_MASK) ||
            first_point_axis[0] != 0.0 || first_point_axis[1] != 0.0 || first_point_axis[2] != 5.0) {
            v5_program_runtime_destroy(&runtime);
            return 11;
        }
    }
    if (!v5_program_runtime_has_first_point_metadata(&runtime) ||
        strcmp(v5_program_runtime_source_path(&runtime), path) != 0 ||
        v5_program_runtime_loaded_epoch(&runtime) != open_result.loaded_epoch ||
        open_result.source_path == 0 || strcmp(open_result.source_path, path) != 0 ||
        strcmp(v5_program_runtime_source_sha256(&runtime), "94fc8d2bf8127ec3d40fc11d4bc538ebd2b378e1c794ecb69e7e3d2200a7581d") != 0 ||
        open_result.source_sha256 == 0 || strcmp(open_result.source_sha256, "94fc8d2bf8127ec3d40fc11d4bc538ebd2b378e1c794ecb69e7e3d2200a7581d") != 0) {
        v5_program_runtime_destroy(&runtime);
        return 10;
    }
    program_preview_count = runtime.preview_trajectory_count;
    program_loaded_epoch = open_result.loaded_epoch;
    snprintf(program_source_path, sizeof(program_source_path), "%s", open_result.source_path);
    snprintf(program_source_sha256, sizeof(program_source_sha256), "%s", open_result.source_sha256);
    if (!v5_command_gate_prepare(&start_request, &prepared)) {
        v5_program_runtime_destroy(&runtime);
        return 5;
    }
    if (start_request.kind != V5_COMMAND_START ||
        !start_request.text_value ||
        strcmp(start_request.text_value, path) != 0 ||
        strcmp(prepared.name, "start") != 0 ||
        strcmp(prepared.owner, "native_linuxcncrsh") != 0) {
        v5_program_runtime_destroy(&runtime);
        return 20;
    }

    if (!v5_program_runtime_write_multisegment_input(path)) {
        v5_program_runtime_destroy(&runtime);
        return 18;
    }
    ok = v5_program_runtime_open_file(&runtime, path, &open_result);
    remove(path);
    if (!ok || !open_result.ok ||
        runtime.preview_segment_count != 2U ||
        runtime.preview_trajectory_count != 6U ||
        strcmp(runtime.preview_strategy, "modal_g123") != 0 ||
        runtime.preview_trajectory[0].axis[0] != 0.0 ||
        runtime.preview_trajectory[0].axis[1] != 0.0 ||
        runtime.preview_trajectory[2].axis[0] != 1.0 ||
        runtime.preview_trajectory[2].axis[1] != 1.0 ||
        runtime.preview_trajectory[3].axis[0] != 5.0 ||
        runtime.preview_trajectory[3].axis[1] != 5.0 ||
        runtime.preview_trajectory[5].axis[0] != 6.0 ||
        runtime.preview_trajectory[5].axis[1] != 6.0 ||
        !v5_program_runtime_preview_break_before(&runtime, 0U) ||
        !v5_program_runtime_preview_break_before(&runtime, 3U) ||
        v5_program_runtime_preview_break_before(&runtime, 1U)) {
        v5_program_runtime_destroy(&runtime);
        return 19;
    }

    if (!v5_program_runtime_write_long_input(path)) {
        v5_program_runtime_destroy(&runtime);
        return 12;
    }
    ok = v5_program_runtime_open_file(&runtime, path, &open_result);
    remove(path);
    if (!ok || !open_result.ok ||
        !runtime.preview_decimated || runtime.preview_truncated ||
        runtime.preview_candidate_count <= runtime.preview_kept_count ||
        runtime.preview_segment_count != 1U ||
        runtime.preview_trajectory_count != V5_PROGRAM_PREVIEW_POINT_COUNT ||
        strcmp(runtime.preview_strategy, "lod_modal_g123_decimated") != 0 ||
        open_result.preview_truncated != runtime.preview_truncated) {
        v5_program_runtime_destroy(&runtime);
        return 13;
    }

    if (!v5_program_runtime_write_modal_arc_bc_input(path)) {
        v5_program_runtime_destroy(&runtime);
        return 21;
    }
    ok = v5_program_runtime_open_file(&runtime, path, &open_result);
    remove(path);
    if (!ok || !open_result.ok || runtime.preview_trajectory_count < 20U ||
        runtime.preview_wcs_mask != 3U || !runtime.preview_wcs_mixed ||
        runtime.preview_trajectory[runtime.preview_trajectory_count - 1U].axis[0] != 12.0 ||
        runtime.preview_trajectory[runtime.preview_trajectory_count - 1U].axis[3] != 30.0 ||
        runtime.preview_trajectory[runtime.preview_trajectory_count - 1U].axis[4] != 45.0 ||
        runtime.preview_segment_count != 2U) {
        v5_program_runtime_destroy(&runtime);
        return 22;
    }

    if (!v5_program_runtime_write_oversize_input(path)) {
        v5_program_runtime_destroy(&runtime);
        return 16;
    }
    ok = v5_program_runtime_open_file(&runtime, path, &open_result);
    remove(path);
    if (ok || open_result.ok ||
        !open_result.code || strcmp(open_result.code, "PROGRAM_GCODE_SIZE_LIMIT_EXCEEDED") != 0 ||
        open_result.max_gcode_bytes != V5_PROGRAM_RUNTIME_MAX_GCODE_BYTES ||
        v5_program_runtime_has_open_program(&runtime)) {
        v5_program_runtime_destroy(&runtime);
        return 17;
    }

    if (!v5_program_runtime_set_mdi_line(&runtime, "G4 P0")) {
        v5_program_runtime_destroy(&runtime);
        return 6;
    }
    if (!v5_program_runtime_prepare_start(&runtime, &start_request) || start_request.kind != V5_COMMAND_MDI_RUN) {
        v5_program_runtime_destroy(&runtime);
        return 7;
    }
    if (!start_request.text_value || strcmp(start_request.text_value, "G4 P0") != 0) {
        v5_program_runtime_destroy(&runtime);
        return 8;
    }
    if (!v5_command_gate_prepare(&start_request, &prepared) || strcmp(prepared.name, "mdi_run") != 0) {
        v5_program_runtime_destroy(&runtime);
        return 9;
    }

    if (!v5_program_runtime_write_input(path) ||
        !v5_program_runtime_open_file(&runtime, path, &open_result)) {
        v5_program_runtime_destroy(&runtime);
        return 23;
    }
    {
        unsigned int generation_before_delete = runtime.generation;
        FILE *deleted_file;
        if (!v5_program_runtime_delete_file(&runtime, path, &delete_result) ||
            !delete_result.ok || !delete_result.removed ||
            !delete_result.cleared_loaded_program ||
            strcmp(delete_result.code, "OK") != 0 ||
            delete_result.generation != generation_before_delete + 1U ||
            v5_program_runtime_has_open_program(&runtime)) {
            remove(path);
            v5_program_runtime_destroy(&runtime);
            return 24;
        }
        deleted_file = fopen(path, "rb");
        if (deleted_file) {
            fclose(deleted_file);
            remove(path);
            v5_program_runtime_destroy(&runtime);
            return 25;
        }
        if (v5_program_runtime_delete_file(&runtime, path, &delete_result) ||
            delete_result.ok || delete_result.removed ||
            strcmp(delete_result.code, "PROGRAM_DELETE_NOT_REGULAR") != 0) {
            v5_program_runtime_destroy(&runtime);
            return 26;
        }
    }

    printf(
        "v5 program runtime: open=%d generation=%u epoch=%u bytes=%lu lines=%u preview=%u display=%s source=%s sha256=%s mdi=%s owner=%s\n",
        open_result.ok,
        open_result.generation,
        program_loaded_epoch,
        (unsigned long)open_result.byte_count,
        open_result.line_count,
        program_preview_count,
        open_result.display_name,
        program_source_path,
        program_source_sha256,
        v5_program_runtime_mdi_text(&runtime),
        prepared.owner);

    v5_program_runtime_destroy(&runtime);
    return 0;
}
