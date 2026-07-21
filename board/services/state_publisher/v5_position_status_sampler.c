#define _GNU_SOURCE

#include "v5_position_status_sampler.h"

#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define V5_POSITION_COMPONENT "v5-position-display"
#define V5_BUS_STATUS_MAGIC 0x56425553u
#define V5_BUS_STATUS_VERSION 1u
#define V5_BUS_VALID 1u
#define V5_BUS_MASTER_LINK_UP (1u << 0)
#define V5_BUS_MASTER_STATE_OP (1u << 1)
#define V5_BUS_MASTER_ALL_OP (1u << 2)
#define V5_BUS_JOINT_SLAVE_OP (1u << 0)

static void memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

static uint32_t bus_crc32(const V5BusStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0u; i < offsetof(V5BusStatusBlock, crc32); ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int load_symbol(void *library, const char *name, void *target)
{
    void *symbol;
    const char *error;
    dlerror();
    symbol = dlsym(library, name);
    error = dlerror();
    if (error || !symbol) {
        fprintf(stderr, "native_hal_symbol_unavailable:%s:%s\n", name,
            error ? error : "missing");
        return 0;
    }
    memcpy(target, &symbol, sizeof(symbol));
    return 1;
}

static int hal_api_open(V5HalApi *api)
{
    if (!api) return 0;
    memset(api, 0, sizeof(*api));
    api->component_id = -1;
    api->library = dlopen("liblinuxcnchal.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!api->library) {
        fprintf(stderr, "native_hal_library_unavailable:%s\n", dlerror());
        return 0;
    }
    if (!load_symbol(api->library, "hal_init", &api->init) ||
        !load_symbol(api->library, "hal_ready", &api->ready) ||
        !load_symbol(api->library, "hal_exit", &api->exit) ||
        !load_symbol(api->library, "hal_get_pin_value_by_name", &api->get_pin)) {
        return 0;
    }
    api->component_id = api->init(V5_POSITION_COMPONENT);
    if (api->component_id <= 0 || api->ready(api->component_id) != 0) {
        fprintf(stderr, "native_hal_component_unavailable\n");
        return 0;
    }
    return 1;
}

static void hal_api_close(V5HalApi *api)
{
    if (!api) return;
    if (api->component_id > 0 && api->exit) api->exit(api->component_id);
    if (api->library) dlclose(api->library);
    memset(api, 0, sizeof(*api));
    api->component_id = -1;
}

static int bind_pin(
    V5HalApi *api,
    V5HalRef *reference,
    const char *name,
    V5HalType expected)
{
    V5HalType actual = V5_HAL_TYPE_UNINITIALIZED;
    V5HalData *data = NULL;
    bool connected = false;
    if (!api || !reference || !name ||
        api->get_pin(name, &actual, &data, &connected) != 0 ||
        actual != expected || !data) {
        fprintf(stderr, "native_position_pin_unavailable:%s\n", name);
        return 0;
    }
    reference->type = actual;
    reference->data = data;
    return 1;
}

static uint32_t pin_u32(const V5HalRef *reference)
{
    return reference->data->u;
}

static int pin_bit(const V5HalRef *reference)
{
    return reference->data->b ? 1 : 0;
}

static double pin_float(const V5HalRef *reference)
{
    return reference->data->f;
}

static int bind_formatted(
    V5HalApi *api,
    V5HalRef *reference,
    V5HalType expected,
    const char *format,
    unsigned int index)
{
    char name[128];
    if (snprintf(name, sizeof(name), format, index) >= (int)sizeof(name)) {
        return 0;
    }
    return bind_pin(api, reference, name, expected);
}

static int hal_pins_bind(V5HalApi *api, V5PositionHalPins *pins)
{
    static const char axes[V5_POSITION_ROTARY_AXIS_COUNT] = {'a', 'b', 'c'};
    unsigned int joint;
    unsigned int axis;
    char name[128];
    if (!api || !pins) return 0;
    memset(pins, 0, sizeof(*pins));
#define BIND(member, pin_name, pin_type) \
    do { if (!bind_pin(api, &pins->member, pin_name, pin_type)) return 0; } while (0)
    BIND(display_valid, "v5-native-hal-owner.display-metadata-valid", V5_HAL_BIT);
    BIND(display_generation, "v5-native-hal-owner.display-metadata-generation", V5_HAL_U32);
    BIND(display_active_mask, "v5-native-hal-owner.display-active-mask", V5_HAL_U32);
    BIND(display_commit_seq, "v5-native-hal-owner.display-commit-seq", V5_HAL_U32);
    BIND(mapping_valid, "v5-native-hal-owner.home-table-mapping-valid", V5_HAL_BIT);
    BIND(mapping_generation, "v5-native-hal-owner.home-table-map-gen", V5_HAL_U32);
    BIND(mapping_active_mask, "v5-native-hal-owner.home-table-active-mask", V5_HAL_U32);
    BIND(mapping_commit_seq, "v5-native-hal-owner.home-table-commit-seq", V5_HAL_U32);
    BIND(spindle_speed_rps, "spindle.0.speed-cmd-rps", V5_HAL_FLOAT);
    BIND(linear_velocity_per_second, "motion.current-vel", V5_HAL_FLOAT);
    BIND(feed_override_ratio, "motion.feed-override", V5_HAL_FLOAT);
    BIND(spindle_override_ratio, "spindle.0.override", V5_HAL_FLOAT);
    BIND(router_valid, "v5-bus-axis-router.mapping-valid", V5_HAL_BIT);
    BIND(router_generation, "v5-bus-axis-router.latched-mapping-generation", V5_HAL_U32);
    BIND(router_active_mask, "v5-bus-axis-router.latched-active-mask", V5_HAL_U32);
    BIND(master_link_up, "lcec.0.link-up", V5_HAL_BIT);
    BIND(master_state_op, "lcec.0.state-op", V5_HAL_BIT);
    BIND(master_all_op, "lcec.0.all-op", V5_HAL_BIT);
    BIND(slaves_responding, "lcec.0.slaves-responding", V5_HAL_U32);
#undef BIND
    for (joint = 0u; joint < V5_POSITION_AXIS_COUNT; ++joint) {
        if (!bind_formatted(api, &pins->display_axis_code[joint], V5_HAL_U32,
                "v5-native-hal-owner.display-axis-code-%02u", joint) ||
            !bind_formatted(api, &pins->display_unit_per_count[joint], V5_HAL_FLOAT,
                "v5-native-hal-owner.display-unit-per-count-%02u", joint) ||
            !bind_formatted(api, &pins->home_generation[joint], V5_HAL_U32,
                "v5-native-hal-owner.home-mapping-generation-%02u", joint) ||
            !bind_formatted(api, &pins->home_status_slot[joint], V5_HAL_U32,
                "v5-native-hal-owner.home-status-slot-%02u", joint) ||
            !bind_formatted(api, &pins->home_axis_code[joint], V5_HAL_U32,
                "v5-native-hal-owner.home-axis-code-%02u", joint) ||
            !bind_formatted(api, &pins->home_slave_position[joint], V5_HAL_U32,
                "v5-native-hal-owner.home-slave-position-%02u", joint) ||
            !bind_formatted(api, &pins->home_counts_per_unit[joint], V5_HAL_FLOAT,
                "v5-native-hal-owner.home-counts-per-unit-%02u", joint) ||
            !bind_formatted(api, &pins->actual[joint], V5_HAL_FLOAT,
                "joint.%u.pos-fb", joint) ||
            !bind_formatted(api, &pins->commanded[joint], V5_HAL_FLOAT,
                "joint.%u.pos-cmd", joint) ||
            !bind_formatted(api, &pins->slave_statusword[joint], V5_HAL_U32,
                "lcec.0.s%u.statusword", joint)) {
            return 0;
        }
    }
    for (axis = 0u; axis < V5_POSITION_ROTARY_AXIS_COUNT; ++axis) {
        if (snprintf(name, sizeof(name),
                "v5-native-hal-owner.wcp-%c-valid", axes[axis]) >=
                (int)sizeof(name) ||
            !bind_pin(api, &pins->checkpoint_valid[axis], name, V5_HAL_BIT) ||
            snprintf(name, sizeof(name),
                "v5-native-hal-owner.wcp-%c-generation", axes[axis]) >=
                (int)sizeof(name) ||
            !bind_pin(api, &pins->checkpoint_generation[axis], name, V5_HAL_U32) ||
            snprintf(name, sizeof(name),
                "v5-native-hal-owner.wcp-%c-logical-counts", axes[axis]) >=
                (int)sizeof(name) ||
            !bind_pin(api, &pins->checkpoint_logical[axis], name, V5_HAL_FLOAT) ||
            snprintf(name, sizeof(name),
                "v5-native-hal-owner.wcp-%c-base-counts", axes[axis]) >=
                (int)sizeof(name) ||
            !bind_pin(api, &pins->checkpoint_base[axis], name, V5_HAL_FLOAT) ||
            snprintf(name, sizeof(name),
                "v5-native-hal-owner.wcp-%c-runtime-counts", axes[axis]) >=
                (int)sizeof(name) ||
            !bind_pin(api, &pins->checkpoint_runtime[axis], name, V5_HAL_FLOAT)) {
            return 0;
        }
    }
    return 1;
}

static int sample_checkpoint(
    const V5PositionHalPins *pins,
    unsigned int axis,
    V5PositionRotaryCheckpoint *checkpoint)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t generation_before;
        uint32_t generation_after;
        int valid_before;
        int valid_after;
        memory_barrier();
        valid_before = pin_bit(&pins->checkpoint_valid[axis]);
        generation_before = pin_u32(&pins->checkpoint_generation[axis]);
        checkpoint->logical_counts = pin_float(&pins->checkpoint_logical[axis]);
        checkpoint->base_counts = pin_float(&pins->checkpoint_base[axis]);
        checkpoint->runtime_counts = pin_float(&pins->checkpoint_runtime[axis]);
        memory_barrier();
        generation_after = pin_u32(&pins->checkpoint_generation[axis]);
        valid_after = pin_bit(&pins->checkpoint_valid[axis]);
        if (valid_before && valid_after && generation_before &&
            generation_before == generation_after &&
            isfinite(checkpoint->logical_counts) &&
            isfinite(checkpoint->base_counts) &&
            isfinite(checkpoint->runtime_counts)) {
            checkpoint->valid = 1;
            checkpoint->generation = generation_before;
            return 1;
        }
    }
    memset(checkpoint, 0, sizeof(*checkpoint));
    return 0;
}

static int sample_source(
    const V5PositionHalPins *pins,
    V5PositionSourceSnapshot *source)
{
    uint32_t display_generation;
    uint32_t display_mask;
    uint32_t display_commit;
    uint32_t mapping_generation;
    uint32_t mapping_mask;
    uint32_t mapping_commit;
    int mapping_valid;
    size_t axis;
    if (!pins || !source) return 0;
    memset(source, 0, sizeof(*source));
    memory_barrier();
    if (!pin_bit(&pins->display_valid)) return 0;
    display_generation = pin_u32(&pins->display_generation);
    display_mask = pin_u32(&pins->display_active_mask);
    display_commit = pin_u32(&pins->display_commit_seq);
    for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
        source->display.axis_code[axis] =
            pin_u32(&pins->display_axis_code[axis]);
        source->display.unit_per_count[axis] =
            pin_float(&pins->display_unit_per_count[axis]);
        source->actual[axis] = pin_float(&pins->actual[axis]);
        source->commanded[axis] = pin_float(&pins->commanded[axis]);
    }
    source->spindle_speed_rpm = pin_float(&pins->spindle_speed_rps) * 60.0;
    source->linear_velocity_mm_per_min =
        pin_float(&pins->linear_velocity_per_second) * 60.0;
    source->feed_override_percent =
        pin_float(&pins->feed_override_ratio) * 100.0;
    source->spindle_override_percent =
        pin_float(&pins->spindle_override_ratio) * 100.0;
    memory_barrier();
    if (!pin_bit(&pins->display_valid) ||
        pin_u32(&pins->display_generation) != display_generation ||
        pin_u32(&pins->display_active_mask) != display_mask ||
        pin_u32(&pins->display_commit_seq) != display_commit) {
        return 0;
    }
    source->display.generation = display_generation;
    source->display.active_mask = display_mask;
    source->display.commit_seq = display_commit;

    mapping_valid = pin_bit(&pins->mapping_valid);
    mapping_generation = pin_u32(&pins->mapping_generation);
    mapping_mask = pin_u32(&pins->mapping_active_mask);
    mapping_commit = pin_u32(&pins->mapping_commit_seq);
    if (!mapping_valid && !mapping_generation && !mapping_mask && !mapping_commit) {
        source->mapping.state = V5_POSITION_MAPPING_ABSENT;
    } else if (!mapping_valid || !mapping_generation || !mapping_mask ||
               !mapping_commit) {
        source->mapping.state = V5_POSITION_MAPPING_INVALID;
    } else {
        source->mapping.state = V5_POSITION_MAPPING_VALID;
        source->mapping.generation = mapping_generation;
        source->mapping.active_mask = mapping_mask;
        source->mapping.commit_seq = mapping_commit;
        for (axis = 0u; axis < V5_POSITION_AXIS_COUNT; ++axis) {
            source->mapping.joints[axis].generation =
                pin_u32(&pins->home_generation[axis]);
            source->mapping.joints[axis].status_slot =
                pin_u32(&pins->home_status_slot[axis]);
            source->mapping.joints[axis].axis_code =
                pin_u32(&pins->home_axis_code[axis]);
            source->mapping.joints[axis].counts_per_unit =
                pin_float(&pins->home_counts_per_unit[axis]);
        }
        memory_barrier();
        if (!pin_bit(&pins->mapping_valid) ||
            pin_u32(&pins->mapping_generation) != mapping_generation ||
            pin_u32(&pins->mapping_active_mask) != mapping_mask ||
            pin_u32(&pins->mapping_commit_seq) != mapping_commit) {
            source->mapping.state = V5_POSITION_MAPPING_INVALID;
        }
    }
    for (axis = 0u; axis < V5_POSITION_ROTARY_AXIS_COUNT; ++axis) {
        sample_checkpoint(pins, (unsigned int)axis, &source->checkpoint[axis]);
    }
    return 1;
}

static int bus_axis_valid(uint32_t code)
{
    return code == (uint32_t)'X' || code == (uint32_t)'Y' ||
        code == (uint32_t)'Z' || code == (uint32_t)'A' ||
        code == (uint32_t)'B' || code == (uint32_t)'C';
}

static int sample_bus(
    const V5PositionHalPins *pins,
    uint32_t writer_identity,
    uint32_t sequence,
    uint32_t source_generation,
    uint64_t timestamp,
    V5BusStatusBlock *block)
{
    uint32_t table_generation;
    uint32_t router_generation;
    uint32_t table_mask;
    uint32_t router_mask;
    uint32_t seen_axes = 0u;
    uint32_t seen_slaves = 0u;
    size_t joint;
    int valid = 1;
    memset(block, 0, sizeof(*block));
    block->magic = V5_BUS_STATUS_MAGIC;
    block->version = V5_BUS_STATUS_VERSION;
    block->size = (uint32_t)sizeof(*block);
    block->sequence = sequence;
    block->writer_identity = writer_identity;
    block->joint_count = V5_BUS_JOINT_COUNT;
    block->source_generation = source_generation;
    block->monotonic_ns = timestamp;
    memory_barrier();
    table_generation = pin_u32(&pins->mapping_generation);
    router_generation = pin_u32(&pins->router_generation);
    table_mask = pin_u32(&pins->mapping_active_mask);
    router_mask = pin_u32(&pins->router_active_mask);
    if (!pin_bit(&pins->mapping_valid) || !pin_bit(&pins->router_valid) ||
        !table_generation || table_generation != router_generation ||
        table_mask != router_mask || table_mask != 0x1fu) {
        valid = 0;
    }
    if (valid) {
        block->mapping_generation = table_generation;
        block->active_mask = table_mask;
        if (pin_bit(&pins->master_link_up))
            block->master_flags |= V5_BUS_MASTER_LINK_UP;
        if (pin_bit(&pins->master_state_op))
            block->master_flags |= V5_BUS_MASTER_STATE_OP;
        if (pin_bit(&pins->master_all_op))
            block->master_flags |= V5_BUS_MASTER_ALL_OP;
        block->slaves_responding = pin_u32(&pins->slaves_responding);
        for (joint = 0u; joint < V5_BUS_JOINT_COUNT; ++joint) {
            uint32_t axis_code = pin_u32(&pins->home_axis_code[joint]);
            uint32_t slave = pin_u32(&pins->home_slave_position[joint]);
            uint32_t statusword;
            uint32_t axis_bit;
            uint32_t slave_bit;
            if (pin_u32(&pins->home_generation[joint]) != table_generation ||
                !bus_axis_valid(axis_code) || slave >= V5_BUS_JOINT_COUNT) {
                valid = 0;
                break;
            }
            axis_bit = 1u << (axis_code - (uint32_t)'A');
            slave_bit = 1u << slave;
            if ((seen_axes & axis_bit) || (seen_slaves & slave_bit)) {
                valid = 0;
                break;
            }
            seen_axes |= axis_bit;
            seen_slaves |= slave_bit;
            statusword = pin_u32(&pins->slave_statusword[slave]);
            if (statusword > 0xffffu) {
                valid = 0;
                break;
            }
            block->joints[joint].valid = 1u;
            block->joints[joint].axis_code = axis_code;
            block->joints[joint].slave_position = slave;
            block->joints[joint].flags =
                (statusword & 0x006fu) == 0x0027u ?
                V5_BUS_JOINT_SLAVE_OP : 0u;
            block->joints[joint].statusword = statusword;
        }
        memory_barrier();
        if (!pin_bit(&pins->mapping_valid) || !pin_bit(&pins->router_valid) ||
            pin_u32(&pins->mapping_generation) != table_generation ||
            pin_u32(&pins->router_generation) != router_generation ||
            pin_u32(&pins->mapping_active_mask) != table_mask ||
            pin_u32(&pins->router_active_mask) != router_mask) {
            valid = 0;
        }
    }
    if (!valid) {
        memset(&block->mapping_generation, 0,
            offsetof(V5BusStatusBlock, crc32) -
            offsetof(V5BusStatusBlock, mapping_generation));
        block->joint_count = V5_BUS_JOINT_COUNT;
        block->source_generation = source_generation;
        block->monotonic_ns = timestamp;
    } else {
        block->valid = V5_BUS_VALID;
    }
    block->crc32 = bus_crc32(block);
    return valid;
}

int v5_position_status_sampler_open(V5PositionStatusSampler *sampler)
{
    if (!sampler) return 0;
    memset(sampler, 0, sizeof(*sampler));
    sampler->hal_api.component_id = -1;
    if (!hal_api_open(&sampler->hal_api) ||
        !hal_pins_bind(&sampler->hal_api, &sampler->pins)) {
        v5_position_status_sampler_close(sampler);
        return 0;
    }
    return 1;
}

void v5_position_status_sampler_close(V5PositionStatusSampler *sampler)
{
    if (sampler) hal_api_close(&sampler->hal_api);
}

int v5_position_status_sampler_sample_source(
    const V5PositionStatusSampler *sampler,
    V5PositionSourceSnapshot *source)
{
    return sampler ? sample_source(&sampler->pins, source) : 0;
}

int v5_position_status_sampler_sample_bus(
    const V5PositionStatusSampler *sampler,
    uint32_t writer_identity,
    uint32_t sequence,
    uint32_t source_generation,
    uint64_t timestamp,
    V5BusStatusBlock *block)
{
    return sampler ? sample_bus(
        &sampler->pins, writer_identity, sequence,
        source_generation, timestamp, block) : 0;
}
