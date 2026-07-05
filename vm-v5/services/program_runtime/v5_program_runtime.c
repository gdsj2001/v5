#include "v5_program_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void v5_program_runtime_clear(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    free(runtime->gcode_text);
    runtime->gcode_text = 0;
    runtime->gcode_size = 0U;
    runtime->line_count = 0U;
    runtime->loaded = 0;
    runtime->display_name[0] = '\0';
}

static unsigned int v5_program_runtime_count_lines(const char *text, size_t size)
{
    size_t i;
    unsigned int lines = 0U;

    for (i = 0U; i < size; ++i) {
        if (text[i] == '\n') {
            ++lines;
        }
    }
    if (size > 0U && text[size - 1U] != '\n') {
        ++lines;
    }
    return lines;
}

static const char *v5_program_runtime_basename(const char *path)
{
    const char *name = path;
    const char *p;

    if (!path) {
        return "";
    }

    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

static void v5_program_runtime_set_result(
    const V5ProgramRuntime *runtime,
    V5ProgramOpenResult *result,
    int ok)
{
    if (!result) {
        return;
    }

    result->ok = ok;
    result->generation = runtime ? runtime->generation : 0U;
    result->byte_count = runtime ? runtime->gcode_size : 0U;
    result->line_count = runtime ? runtime->line_count : 0U;
    result->display_name = runtime ? runtime->display_name : "";
}

void v5_program_runtime_init(V5ProgramRuntime *runtime)
{
    if (!runtime) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

void v5_program_runtime_destroy(V5ProgramRuntime *runtime)
{
    v5_program_runtime_clear(runtime);
}

int v5_program_runtime_open_file(
    V5ProgramRuntime *runtime,
    const char *path,
    V5ProgramOpenResult *result)
{
    FILE *fp;
    long length;
    size_t size;
    char *buffer;
    size_t read_count;
    const char *display_name;

    if (!runtime || !path || !path[0]) {
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    length = ftell(fp);
    if (length < 0L || fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    size = (size_t)length;
    buffer = (char *)malloc(size + 1U);
    if (!buffer) {
        fclose(fp);
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    read_count = fread(buffer, 1U, size, fp);
    fclose(fp);
    if (read_count != size) {
        free(buffer);
        v5_program_runtime_set_result(runtime, result, 0);
        return 0;
    }

    buffer[size] = '\0';
    v5_program_runtime_clear(runtime);
    runtime->gcode_text = buffer;
    runtime->gcode_size = size;
    runtime->line_count = v5_program_runtime_count_lines(buffer, size);
    runtime->loaded = 1;
    ++runtime->generation;

    display_name = v5_program_runtime_basename(path);
    snprintf(runtime->display_name, sizeof(runtime->display_name), "%s", display_name);

    v5_program_runtime_set_result(runtime, result, 1);
    return 1;
}

int v5_program_runtime_has_open_program(const V5ProgramRuntime *runtime)
{
    return runtime && runtime->loaded && runtime->gcode_text && runtime->gcode_size > 0U;
}

int v5_program_runtime_prepare_start(
    const V5ProgramRuntime *runtime,
    V5CommandRequest *request)
{
    if (!v5_program_runtime_has_open_program(runtime) || !request) {
        return 0;
    }

    memset(request, 0, sizeof(*request));
    request->kind = V5_COMMAND_START;
    return 1;
}
