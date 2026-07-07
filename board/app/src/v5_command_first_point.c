#include "v5_command_first_point.h"

#include <string.h>

int v5_command_first_point_prepare(const V5ProgramRuntime *runtime, V5CommandPrepared *prepared, V5CommandRequest *request)
{
    unsigned int axis_mask = 0U;
    double axis[V5_COMMAND_AXIS_COUNT];
    unsigned int i;

    if (!runtime || !prepared || !request || !v5_program_runtime_has_first_point_metadata(runtime) ||
        !v5_program_runtime_first_point_axes(runtime, axis, &axis_mask)) {
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->kind = V5_COMMAND_FIRST_POINT;
    request->text_value = v5_program_runtime_source_path(runtime);
    request->secondary_text_value = v5_program_runtime_source_sha256(runtime);
    request->mode_value = V5_FIRST_POINT_SEQUENCE;
    request->index_value = (int)v5_program_runtime_loaded_epoch(runtime);
    request->axis_mask = axis_mask;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        request->point_axis[i] = axis[i];
    }
    if (!request->text_value || !request->text_value[0] ||
        !request->secondary_text_value || strlen(request->secondary_text_value) != 64U ||
        request->index_value <= 0 || !request->axis_mask) {
        return 0;
    }
    return v5_command_gate_prepare(request, prepared);
}
