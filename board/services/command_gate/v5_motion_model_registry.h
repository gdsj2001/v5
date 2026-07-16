#ifndef V5_MOTION_MODEL_REGISTRY_H
#define V5_MOTION_MODEL_REGISTRY_H

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define V5_MOTION_MODEL_ID_XYZAC_TRT 1U
#define V5_MOTION_MODEL_ID_XYZBC_TRT 2U
#define V5_MOTION_MODEL_MAX_ACTIVE_AXES 8U
#define V5_MOTION_MODEL_MAX_STATUS_SLOTS 8U
#define V5_MOTION_MODEL_G53_CENTER_COUNT 3U
#define V5_MOTION_MODEL_WCS_COMPONENT_COUNT 3U

typedef struct V5MotionModelDescriptor {
    unsigned int registry_id;
    const char *canonical;
    const char *display;
    const char *aliases[4];
    const char *kins_module;
    const char *kins_coordinates;
    const char *traj_coordinates;
    unsigned int wrapped_rotary_mask; /* motmod EmcPose target bits for active rotary axes */
    char first_rotary_axis;
    char second_rotary_axis;
    unsigned int first_status_slot;
    unsigned int second_status_slot;
    unsigned int first_g53_center;
    unsigned int second_g53_center;
    unsigned int first_center_wcs_component;
    unsigned int second_center_wcs_component;
    unsigned int active_axis_count;
    char active_axes[V5_MOTION_MODEL_MAX_ACTIVE_AXES];
    unsigned int active_status_slots[V5_MOTION_MODEL_MAX_ACTIVE_AXES];
} V5MotionModelDescriptor;

static const V5MotionModelDescriptor v5_motion_model_registry[] = {
    {
        V5_MOTION_MODEL_ID_XYZAC_TRT,
        "XYZAC_TRT",
        "AC摇篮",
        {"AC", "AC摇篮", "XYZAC", "XYZAC_TRT"},
        "xyzac-trt-kins",
        "XYZAC",
        "X Y Z A C",
        40U,
        'A',
        'C',
        3U,
        4U,
        0U,
        2U,
        0U,
        2U,
        5U,
        {'X', 'Y', 'Z', 'A', 'C'},
        {0U, 1U, 2U, 3U, 4U},
    },
    {
        V5_MOTION_MODEL_ID_XYZBC_TRT,
        "XYZBC_TRT",
        "BC摇篮",
        {"BC", "BC摇篮", "XYZBC", "XYZBC_TRT"},
        "xyzbc-trt-kins",
        "XYZBC",
        "X Y Z B C",
        48U,
        'B',
        'C',
        3U,
        4U,
        1U,
        2U,
        1U,
        2U,
        5U,
        {'X', 'Y', 'Z', 'B', 'C'},
        {0U, 1U, 2U, 3U, 4U},
    },
};

static inline int v5_motion_model_descriptor_valid(const V5MotionModelDescriptor *model)
{
    unsigned int i;
    unsigned int j;
    unsigned int traj_index = 0U;
    unsigned int kins_index = 0U;
    const char *p;

    unsigned int first_rotary_seen = 0U;
    unsigned int second_rotary_seen = 0U;
    unsigned int expected_wrapped_mask = 0U;

    if (!model || model->registry_id == 0U ||
        !model->canonical || !model->canonical[0] ||
        !model->display || !model->display[0] ||
        !model->kins_module || !model->kins_module[0] ||
        !model->kins_coordinates || !model->kins_coordinates[0] ||
        !model->traj_coordinates || !model->traj_coordinates[0] ||
        model->active_axis_count == 0U ||
        model->active_axis_count > V5_MOTION_MODEL_MAX_ACTIVE_AXES ||
        model->first_rotary_axis == model->second_rotary_axis ||
        model->first_status_slot >= V5_MOTION_MODEL_MAX_STATUS_SLOTS ||
        model->second_status_slot >= V5_MOTION_MODEL_MAX_STATUS_SLOTS ||
        model->first_g53_center >= V5_MOTION_MODEL_G53_CENTER_COUNT ||
        model->second_g53_center >= V5_MOTION_MODEL_G53_CENTER_COUNT ||
        model->first_g53_center == model->second_g53_center ||
        model->first_center_wcs_component >= V5_MOTION_MODEL_WCS_COMPONENT_COUNT ||
        model->second_center_wcs_component >= V5_MOTION_MODEL_WCS_COMPONENT_COUNT ||
        model->first_center_wcs_component == model->second_center_wcs_component) {
        return 0;
    }
    for (i = 0U; i < model->active_axis_count; ++i) {
        unsigned char axis = (unsigned char)model->active_axes[i];
        if (!isalpha(axis) || (char)toupper(axis) != model->active_axes[i]) {
            return 0;
        }
        if (model->active_status_slots[i] >= V5_MOTION_MODEL_MAX_STATUS_SLOTS) {
            return 0;
        }
        if (model->active_axes[i] == model->first_rotary_axis &&
            model->active_status_slots[i] == model->first_status_slot) {
            first_rotary_seen = 1U;
        }
        if (model->active_axes[i] == model->second_rotary_axis &&
            model->active_status_slots[i] == model->second_status_slot) {
            second_rotary_seen = 1U;
        }
        for (j = 0U; j < i; ++j) {
            if (model->active_axes[j] == model->active_axes[i] ||
                model->active_status_slots[j] == model->active_status_slots[i]) {
                return 0;
            }
        }
    }
    for (p = model->traj_coordinates; *p; ++p) {
        if (!isalpha((unsigned char)*p)) {
            continue;
        }
        if (traj_index >= model->active_axis_count ||
            (char)toupper((unsigned char)*p) != model->active_axes[traj_index]) {
            return 0;
        }
        ++traj_index;
    }
    for (p = model->kins_coordinates; *p; ++p) {
        if (!isalpha((unsigned char)*p)) {
            continue;
        }
        if (kins_index >= model->active_axis_count ||
            (char)toupper((unsigned char)*p) != model->active_axes[kins_index]) {
            return 0;
        }
        ++kins_index;
    }
    switch (model->first_rotary_axis) {
    case 'A': expected_wrapped_mask |= 8U; break;
    case 'B': expected_wrapped_mask |= 16U; break;
    case 'C': expected_wrapped_mask |= 32U; break;
    default: return 0;
    }
    switch (model->second_rotary_axis) {
    case 'A': expected_wrapped_mask |= 8U; break;
    case 'B': expected_wrapped_mask |= 16U; break;
    case 'C': expected_wrapped_mask |= 32U; break;
    default: return 0;
    }
    return traj_index == model->active_axis_count &&
        kins_index == model->active_axis_count &&
        first_rotary_seen && second_rotary_seen &&
        model->wrapped_rotary_mask == expected_wrapped_mask;
}

static inline int v5_motion_model_axis_for_status_slot(
    const V5MotionModelDescriptor *model,
    unsigned int status_slot,
    char *axis_out)
{
    unsigned int i;
    if (axis_out) {
        *axis_out = '\0';
    }
    if (!axis_out || !v5_motion_model_descriptor_valid(model)) {
        return 0;
    }
    for (i = 0U; i < model->active_axis_count; ++i) {
        if (model->active_status_slots[i] == status_slot) {
            *axis_out = model->active_axes[i];
            return 1;
        }
    }
    return 0;
}

static inline int v5_motion_model_status_slot_for_axis(
    const V5MotionModelDescriptor *model,
    char axis,
    unsigned int *status_slot_out)
{
    unsigned int i;
    char wanted = (char)toupper((unsigned char)axis);
    if (status_slot_out) {
        *status_slot_out = 0U;
    }
    if (!status_slot_out || !v5_motion_model_descriptor_valid(model)) {
        return 0;
    }
    for (i = 0U; i < model->active_axis_count; ++i) {
        if (model->active_axes[i] == wanted) {
            *status_slot_out = model->active_status_slots[i];
            return 1;
        }
    }
    return 0;
}

static inline int v5_motion_model_same_status_slots(
    const V5MotionModelDescriptor *left,
    const V5MotionModelDescriptor *right)
{
    unsigned int i;
    char axis;
    if (!v5_motion_model_descriptor_valid(left) ||
        !v5_motion_model_descriptor_valid(right) ||
        left->active_axis_count != right->active_axis_count) {
        return 0;
    }
    for (i = 0U; i < left->active_axis_count; ++i) {
        if (!v5_motion_model_axis_for_status_slot(
                right, left->active_status_slots[i], &axis)) {
            return 0;
        }
    }
    return 1;
}

static inline size_t v5_motion_model_registry_count(void)
{
    return sizeof(v5_motion_model_registry) / sizeof(v5_motion_model_registry[0]);
}

static inline int v5_motion_model_registry_valid(void)
{
    size_t i;
    size_t j;
    for (i = 0U; i < v5_motion_model_registry_count(); ++i) {
        const V5MotionModelDescriptor *model = &v5_motion_model_registry[i];
        if (!v5_motion_model_descriptor_valid(model)) {
            return 0;
        }
        for (j = 0U; j < i; ++j) {
            const V5MotionModelDescriptor *other = &v5_motion_model_registry[j];
            if (model->registry_id == other->registry_id ||
                strcmp(model->canonical, other->canonical) == 0 ||
                strcmp(model->kins_module, other->kins_module) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static inline const V5MotionModelDescriptor *v5_motion_model_registry_at(size_t index)
{
    return v5_motion_model_registry_valid() && index < v5_motion_model_registry_count()
        ? &v5_motion_model_registry[index] : 0;
}

static inline void v5_motion_model_normalize(const char *text, char *out, size_t out_cap)
{
    const unsigned char *begin;
    const unsigned char *end;
    size_t used = 0U;

    if (!out || out_cap == 0U) {
        return;
    }
    out[0] = '\0';
    if (!text) {
        return;
    }
    begin = (const unsigned char *)text;
    while (*begin && isspace(*begin)) {
        ++begin;
    }
    end = begin + strlen((const char *)begin);
    while (end > begin && isspace(end[-1])) {
        --end;
    }
    while (begin < end && used + 1U < out_cap) {
        unsigned char ch = *begin++;
        out[used++] = ch < 0x80U ? (char)toupper(ch) : (char)ch;
    }
    out[used] = '\0';
}

static inline const V5MotionModelDescriptor *v5_motion_model_find(const char *text)
{
    char normalized[64];
    size_t model_i;

    v5_motion_model_normalize(text, normalized, sizeof(normalized));
    if (!normalized[0] || !v5_motion_model_registry_valid()) {
        return 0;
    }
    for (model_i = 0U; model_i < v5_motion_model_registry_count(); ++model_i) {
        const V5MotionModelDescriptor *model = &v5_motion_model_registry[model_i];
        size_t alias_i;
        char candidate[64];

        if (!v5_motion_model_descriptor_valid(model)) {
            continue;
        }
        v5_motion_model_normalize(model->canonical, candidate, sizeof(candidate));
        if (strcmp(normalized, candidate) == 0) {
            return model;
        }
        v5_motion_model_normalize(model->display, candidate, sizeof(candidate));
        if (strcmp(normalized, candidate) == 0) {
            return model;
        }
        for (alias_i = 0U; alias_i < sizeof(model->aliases) / sizeof(model->aliases[0]); ++alias_i) {
            if (!model->aliases[alias_i]) {
                continue;
            }
            v5_motion_model_normalize(model->aliases[alias_i], candidate, sizeof(candidate));
            if (strcmp(normalized, candidate) == 0) {
                return model;
            }
        }
    }
    return 0;
}

static inline int v5_motion_model_registry_index(
    const V5MotionModelDescriptor *model,
    unsigned int *index_out)
{
    size_t model_i;

    if (index_out) {
        *index_out = 0U;
    }
    if (!model) {
        return 0;
    }
    for (model_i = 0U; model_i < v5_motion_model_registry_count(); ++model_i) {
        if (model == &v5_motion_model_registry[model_i] ||
            strcmp(model->canonical, v5_motion_model_registry[model_i].canonical) == 0) {
            if (index_out) {
                *index_out = (unsigned int)model_i;
            }
            return 1;
        }
    }
    return 0;
}

static inline int v5_motion_model_dropdown_options(char *out, size_t out_cap)
{
    size_t model_i;
    size_t used = 0U;

    if (!out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!v5_motion_model_registry_valid()) {
        return 0;
    }
    for (model_i = 0U; model_i < v5_motion_model_registry_count(); ++model_i) {
        int rc = snprintf(
            out + used,
            out_cap - used,
            "%s%s",
            model_i == 0U ? "" : "\n",
            v5_motion_model_registry[model_i].display);
        if (rc < 0 || (size_t)rc >= out_cap - used) {
            out[0] = '\0';
            return 0;
        }
        used += (size_t)rc;
    }
    return used > 0U;
}

#endif
