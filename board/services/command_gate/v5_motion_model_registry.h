#ifndef V5_MOTION_MODEL_REGISTRY_H
#define V5_MOTION_MODEL_REGISTRY_H

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct V5MotionModelDescriptor {
    unsigned int registry_id;
    const char *canonical;
    const char *display;
    const char *aliases[4];
    const char *kins_module;
    const char *kins_coordinates;
    const char *traj_coordinates;
    unsigned int wrapped_rotary_mask;
    char first_rotary_axis;
    char second_rotary_axis;
    unsigned int first_status_slot;
    unsigned int second_status_slot;
    unsigned int first_g53_center;
    unsigned int second_g53_center;
    unsigned int first_center_wcs_component;
    unsigned int second_center_wcs_component;
    unsigned int first_world_axis_component;
} V5MotionModelDescriptor;

static const V5MotionModelDescriptor v5_motion_model_registry[] = {
    {
        1U,
        "XYZAC_TRT",
        "AC摇篮",
        {"AC", "AC摇篮", "XYZAC", "XYZAC_TRT"},
        "xyzac-trt-kins",
        "XYZAC",
        "X Y Z A C",
        24U,
        'A',
        'C',
        3U,
        4U,
        0U,
        2U,
        0U,
        2U,
        0U,
    },
    {
        2U,
        "XYZBC_TRT",
        "BC摇篮",
        {"BC", "BC摇篮", "XYZBC", "XYZBC_TRT"},
        "xyzbc-trt-kins",
        "XYZBC",
        "X Y Z B C",
        24U,
        'B',
        'C',
        3U,
        4U,
        1U,
        2U,
        1U,
        2U,
        1U,
    },
};

static inline size_t v5_motion_model_registry_count(void)
{
    return sizeof(v5_motion_model_registry) / sizeof(v5_motion_model_registry[0]);
}

static inline const V5MotionModelDescriptor *v5_motion_model_registry_at(size_t index)
{
    return index < v5_motion_model_registry_count() ? &v5_motion_model_registry[index] : 0;
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
    if (!normalized[0]) {
        return 0;
    }
    for (model_i = 0U; model_i < v5_motion_model_registry_count(); ++model_i) {
        const V5MotionModelDescriptor *model = &v5_motion_model_registry[model_i];
        size_t alias_i;
        char candidate[64];

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
