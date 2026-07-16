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
#define V5_MOTION_MODEL_KEY_CAP 64U
#define V5_MOTION_MODEL_MAX_TRANSITION_AXES (V5_MOTION_MODEL_MAX_ACTIVE_AXES * 2U)

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

typedef struct V5MotionModelAxisTransition {
    char axis;
    unsigned int current_active;
    unsigned int current_status_slot;
    unsigned int target_active;
    unsigned int target_status_slot;
} V5MotionModelAxisTransition;

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

static inline int v5_motion_model_normalize_checked(
    const char *text,
    char *out,
    size_t out_cap)
{
    const unsigned char *begin;
    const unsigned char *end;
    size_t used = 0U;

    if (!out || out_cap == 0U) {
        return 0;
    }
    out[0] = '\0';
    if (!text) {
        return 0;
    }
    begin = (const unsigned char *)text;
    while (*begin && isspace(*begin)) {
        ++begin;
    }
    end = begin + strlen((const char *)begin);
    while (end > begin && isspace(end[-1])) {
        --end;
    }
    if (end == begin || (size_t)(end - begin) >= out_cap) {
        return 0;
    }
    while (begin < end) {
        unsigned char ch = *begin++;
        out[used++] = ch < 0x80U ? (char)toupper(ch) : (char)ch;
    }
    out[used] = '\0';
    return 1;
}

static inline void v5_motion_model_normalize(const char *text, char *out, size_t out_cap)
{
    (void)v5_motion_model_normalize_checked(text, out, out_cap);
}

static inline const char *v5_motion_model_identity_key_at(
    const V5MotionModelDescriptor *model,
    size_t index)
{
    if (!model) {
        return 0;
    }
    if (index == 0U) {
        return model->canonical;
    }
    if (index == 1U) {
        return model->display;
    }
    index -= 2U;
    return index < sizeof(model->aliases) / sizeof(model->aliases[0])
        ? model->aliases[index] : 0;
}

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
    char normalized[V5_MOTION_MODEL_KEY_CAP];

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
    if (!v5_motion_model_normalize_checked(
            model->canonical, normalized, sizeof(normalized)) ||
        !v5_motion_model_normalize_checked(
            model->display, normalized, sizeof(normalized))) {
        return 0;
    }
    for (i = 0U; i < sizeof(model->aliases) / sizeof(model->aliases[0]); ++i) {
        if (model->aliases[i] &&
            !v5_motion_model_normalize_checked(
                model->aliases[i], normalized, sizeof(normalized))) {
            return 0;
        }
    }
    for (i = 0U; i < model->active_axis_count; ++i) {
        unsigned char axis = (unsigned char)model->active_axes[i];
        if (!isalpha(axis) || (char)toupper(axis) != model->active_axes[i]) {
            return 0;
        }
        if (model->active_status_slots[i] >= V5_MOTION_MODEL_MAX_STATUS_SLOTS ||
            model->active_status_slots[i] != i) {
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

static inline int v5_motion_model_build_axis_transition(
    const V5MotionModelDescriptor *current,
    const V5MotionModelDescriptor *target,
    V5MotionModelAxisTransition *transitions,
    size_t transition_cap,
    size_t *transition_count_out)
{
    size_t transition_count = 0U;
    unsigned int i;
    if (transition_count_out) {
        *transition_count_out = 0U;
    }
    if (!v5_motion_model_descriptor_valid(current) ||
        !v5_motion_model_descriptor_valid(target) ||
        !transitions || !transition_count_out ||
        transition_cap < current->active_axis_count) {
        return 0;
    }
    memset(transitions, 0, sizeof(*transitions) * transition_cap);
    for (i = 0U; i < current->active_axis_count; ++i) {
        V5MotionModelAxisTransition *transition = &transitions[transition_count++];
        transition->axis = current->active_axes[i];
        transition->current_active = 1U;
        transition->current_status_slot = current->active_status_slots[i];
    }
    for (i = 0U; i < target->active_axis_count; ++i) {
        size_t transition_i;
        V5MotionModelAxisTransition *transition = 0;
        for (transition_i = 0U; transition_i < transition_count; ++transition_i) {
            if (transitions[transition_i].axis == target->active_axes[i]) {
                transition = &transitions[transition_i];
                break;
            }
        }
        if (!transition) {
            if (transition_count >= transition_cap) {
                return 0;
            }
            transition = &transitions[transition_count++];
            transition->axis = target->active_axes[i];
        }
        transition->target_active = 1U;
        transition->target_status_slot = target->active_status_slots[i];
    }
    *transition_count_out = transition_count;
    return 1;
}

static inline size_t v5_motion_model_registry_count(void)
{
    return sizeof(v5_motion_model_registry) / sizeof(v5_motion_model_registry[0]);
}

static inline int v5_motion_model_registry_entries_valid(
    const V5MotionModelDescriptor *entries,
    size_t count)
{
    size_t i;
    size_t j;
    if (!entries || count == 0U) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        const V5MotionModelDescriptor *model = &entries[i];
        if (!v5_motion_model_descriptor_valid(model)) {
            return 0;
        }
        for (j = 0U; j < i; ++j) {
            const V5MotionModelDescriptor *other = &entries[j];
            size_t model_key_i;
            if (model->registry_id == other->registry_id ||
                strcmp(model->kins_module, other->kins_module) == 0) {
                return 0;
            }
            for (model_key_i = 0U;
                 model_key_i < 2U + sizeof(model->aliases) / sizeof(model->aliases[0]);
                 ++model_key_i) {
                const char *model_key = v5_motion_model_identity_key_at(model, model_key_i);
                size_t other_key_i;
                char normalized_model[V5_MOTION_MODEL_KEY_CAP];
                if (!model_key) {
                    continue;
                }
                if (!v5_motion_model_normalize_checked(
                        model_key, normalized_model, sizeof(normalized_model))) {
                    return 0;
                }
                for (other_key_i = 0U;
                     other_key_i < 2U + sizeof(other->aliases) / sizeof(other->aliases[0]);
                     ++other_key_i) {
                    const char *other_key = v5_motion_model_identity_key_at(other, other_key_i);
                    char normalized_other[V5_MOTION_MODEL_KEY_CAP];
                    if (!other_key) {
                        continue;
                    }
                    if (!v5_motion_model_normalize_checked(
                            other_key, normalized_other, sizeof(normalized_other)) ||
                        strcmp(normalized_model, normalized_other) == 0) {
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

static inline int v5_motion_model_registry_valid(void)
{
    return v5_motion_model_registry_entries_valid(
        v5_motion_model_registry, v5_motion_model_registry_count());
}

static inline const V5MotionModelDescriptor *v5_motion_model_registry_at(size_t index)
{
    return v5_motion_model_registry_valid() && index < v5_motion_model_registry_count()
        ? &v5_motion_model_registry[index] : 0;
}

static inline const V5MotionModelDescriptor *v5_motion_model_find(const char *text)
{
    char normalized[64];
    size_t model_i;

    if (!v5_motion_model_normalize_checked(text, normalized, sizeof(normalized)) ||
        !v5_motion_model_registry_valid()) {
        return 0;
    }
    for (model_i = 0U; model_i < v5_motion_model_registry_count(); ++model_i) {
        const V5MotionModelDescriptor *model = &v5_motion_model_registry[model_i];
        size_t alias_i;
        char candidate[64];

        if (!v5_motion_model_descriptor_valid(model)) {
            continue;
        }
        if (!v5_motion_model_normalize_checked(
                model->canonical, candidate, sizeof(candidate))) {
            return 0;
        }
        if (strcmp(normalized, candidate) == 0) {
            return model;
        }
        if (!v5_motion_model_normalize_checked(
                model->display, candidate, sizeof(candidate))) {
            return 0;
        }
        if (strcmp(normalized, candidate) == 0) {
            return model;
        }
        for (alias_i = 0U; alias_i < sizeof(model->aliases) / sizeof(model->aliases[0]); ++alias_i) {
            if (!model->aliases[alias_i]) {
                continue;
            }
            if (!v5_motion_model_normalize_checked(
                    model->aliases[alias_i], candidate, sizeof(candidate))) {
                return 0;
            }
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
