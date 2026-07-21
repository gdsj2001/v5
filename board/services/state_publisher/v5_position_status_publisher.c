#define _GNU_SOURCE

#include "v5_position_status_publisher_core.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define V5_POSITION_COMPONENT "v5-position-display"
#define V5_POSITION_LOCK_PATH "/run/8ax/v5_position_status_publisher.lock"
#define V5_POSITION_PID_PATH "/run/8ax/v5_position_status_publisher.pid"
#define V5_BUS_STATUS_DEFAULT_PATH "/dev/shm/v5_native_bus_status.bin"
#define V5_BUS_STATUS_MAGIC 0x56425553u
#define V5_BUS_STATUS_VERSION 1u
#define V5_BUS_JOINT_COUNT 5u
#define V5_BUS_VALID 1u
#define V5_BUS_MASTER_LINK_UP (1u << 0)
#define V5_BUS_MASTER_STATE_OP (1u << 1)
#define V5_BUS_MASTER_ALL_OP (1u << 2)
#define V5_BUS_JOINT_SLAVE_OP (1u << 0)
#define V5_POSITION_HEARTBEAT_NS 100000000ull
#define V5_BUS_SAMPLE_NS 200000000ull
#define V5_STATUS_LOG_NS 5000000000ull
#define V5_MIN_INTERVAL_MS 20u

typedef enum V5HalType {
    V5_HAL_TYPE_UNSPECIFIED = -1,
    V5_HAL_TYPE_UNINITIALIZED = 0,
    V5_HAL_BIT = 1,
    V5_HAL_FLOAT = 2,
    V5_HAL_S32 = 3,
    V5_HAL_U32 = 4,
    V5_HAL_PORT = 5
} V5HalType;

typedef union V5HalData {
    volatile bool b;
    volatile int32_t s;
    volatile uint32_t u;
    volatile double f;
    volatile int p;
} V5HalData;

typedef struct V5HalApi {
    void *library;
    int component_id;
    int (*init)(const char *name);
    int (*ready)(int component_id);
    int (*exit)(int component_id);
    int (*get_pin)(
        const char *name,
        V5HalType *type,
        V5HalData **data,
        bool *connected);
} V5HalApi;

typedef struct V5HalRef {
    V5HalType type;
    V5HalData *data;
} V5HalRef;

typedef struct V5PositionHalPins {
    V5HalRef display_valid;
    V5HalRef display_generation;
    V5HalRef display_active_mask;
    V5HalRef display_commit_seq;
    V5HalRef display_axis_code[V5_POSITION_AXIS_COUNT];
    V5HalRef display_unit_per_count[V5_POSITION_AXIS_COUNT];
    V5HalRef mapping_valid;
    V5HalRef mapping_generation;
    V5HalRef mapping_active_mask;
    V5HalRef mapping_commit_seq;
    V5HalRef home_generation[V5_POSITION_AXIS_COUNT];
    V5HalRef home_status_slot[V5_POSITION_AXIS_COUNT];
    V5HalRef home_axis_code[V5_POSITION_AXIS_COUNT];
    V5HalRef home_slave_position[V5_POSITION_AXIS_COUNT];
    V5HalRef home_counts_per_unit[V5_POSITION_AXIS_COUNT];
    V5HalRef checkpoint_valid[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_generation[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_logical[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_base[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef checkpoint_runtime[V5_POSITION_ROTARY_AXIS_COUNT];
    V5HalRef actual[V5_POSITION_AXIS_COUNT];
    V5HalRef commanded[V5_POSITION_AXIS_COUNT];
    V5HalRef spindle_speed_rps;
    V5HalRef linear_velocity_per_second;
    V5HalRef feed_override_ratio;
    V5HalRef spindle_override_ratio;
    V5HalRef router_valid;
    V5HalRef router_generation;
    V5HalRef router_active_mask;
    V5HalRef master_link_up;
    V5HalRef master_state_op;
    V5HalRef master_all_op;
    V5HalRef slaves_responding;
    V5HalRef slave_statusword[V5_BUS_JOINT_COUNT];
} V5PositionHalPins;

typedef struct V5MappedWriter {
    int fd;
    void *page;
    size_t size;
    uint32_t sequence;
} V5MappedWriter;

typedef struct V5BusJointStatusBlock {
    uint32_t valid;
    uint32_t axis_code;
    uint32_t slave_position;
    uint32_t flags;
    uint32_t statusword;
} V5BusJointStatusBlock;

#pragma pack(push, 1)
typedef struct V5BusStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t sequence;
    uint32_t writer_identity;
    uint32_t mapping_generation;
    uint32_t active_mask;
    uint32_t master_flags;
    uint32_t slaves_responding;
    uint32_t joint_count;
    uint32_t source_generation;
    uint64_t monotonic_ns;
    V5BusJointStatusBlock joints[V5_BUS_JOINT_COUNT];
    uint32_t crc32;
    uint32_t reserved;
} V5BusStatusBlock;
#pragma pack(pop)

typedef char V5BusStatusBlockSize[
    sizeof(V5BusStatusBlock) == 164u ? 1 : -1];

typedef struct V5LifecycleLock {
    int fd;
    char lock_path[512];
    char pid_path[512];
    char record[128];
    uint32_t writer_identity;
} V5LifecycleLock;

static volatile sig_atomic_t stop_requested;

static void memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#endif
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0ull;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void stop_handler(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int mkdir_parent(const char *path)
{
    char copy[512];
    char *slash;
    if (!path || strlen(path) >= sizeof(copy)) return 0;
    strcpy(copy, path);
    slash = strrchr(copy, '/');
    if (!slash || slash == copy) return 1;
    *slash = '\0';
    if (mkdir(copy, 0755) == 0 || errno == EEXIST) return 1;
    return 0;
}

static uint32_t random_identity(void)
{
    uint32_t value = 0u;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t count = read(fd, &value, sizeof(value));
        close(fd);
        if (count != (ssize_t)sizeof(value)) value = 0u;
    }
    if (!value) {
        uint64_t now = monotonic_ns();
        value = (uint32_t)(now ^ (now >> 32) ^ (uint64_t)getpid());
        if (!value) value = 1u;
    }
    return value;
}

static int process_start_ticks(char *buffer, size_t size)
{
    char line[2048];
    char *tail;
    char *token;
    char *save = NULL;
    int field = 3;
    FILE *stream;

    if (!buffer || !size) return 0;
    stream = fopen("/proc/self/stat", "r");
    if (!stream) return 0;
    if (!fgets(line, sizeof(line), stream)) {
        fclose(stream);
        return 0;
    }
    fclose(stream);
    tail = strrchr(line, ')');
    if (!tail || tail[1] != ' ') return 0;
    token = strtok_r(tail + 2, " ", &save);
    while (token) {
        if (field == 22) {
            size_t length = strcspn(token, "\r\n");
            if (length + 1u > size) return 0;
            memcpy(buffer, token, length);
            buffer[length] = '\0';
            return 1;
        }
        ++field;
        token = strtok_r(NULL, " ", &save);
    }
    return 0;
}

static int write_pid_record(const char *path, const char *record)
{
    char temporary[600];
    int fd;
    size_t length;
    if (!path || !record || !mkdir_parent(path) ||
        snprintf(temporary, sizeof(temporary), "%s.%ld.tmp", path,
            (long)getpid()) >= (int)sizeof(temporary)) {
        return 0;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    length = strlen(record);
    if (write(fd, record, length) != (ssize_t)length || fsync(fd) != 0 ||
        close(fd) != 0 || rename(temporary, path) != 0) {
        close(fd);
        unlink(temporary);
        return 0;
    }
    return 1;
}

static int lifecycle_acquire(
    V5LifecycleLock *lifecycle,
    const char *lock_path,
    const char *pid_path)
{
    char start[64];
    size_t length;
    if (!lifecycle || !lock_path || !pid_path ||
        strlen(lock_path) >= sizeof(lifecycle->lock_path) ||
        strlen(pid_path) >= sizeof(lifecycle->pid_path) ||
        !mkdir_parent(lock_path) || !process_start_ticks(start, sizeof(start))) {
        return 0;
    }
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->fd = -1;
    strcpy(lifecycle->lock_path, lock_path);
    strcpy(lifecycle->pid_path, pid_path);
    lifecycle->fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lifecycle->fd < 0) return 0;
    if (flock(lifecycle->fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "position_publisher_already_running\n");
        close(lifecycle->fd);
        lifecycle->fd = -1;
        return 0;
    }
    lifecycle->writer_identity = random_identity();
    if (snprintf(lifecycle->record, sizeof(lifecycle->record), "%ld %s %u\n",
            (long)getpid(), start, lifecycle->writer_identity) >=
        (int)sizeof(lifecycle->record)) {
        return 0;
    }
    length = strlen(lifecycle->record);
    if (ftruncate(lifecycle->fd, 0) != 0 ||
        lseek(lifecycle->fd, 0, SEEK_SET) < 0 ||
        write(lifecycle->fd, lifecycle->record, length) != (ssize_t)length ||
        fsync(lifecycle->fd) != 0 ||
        !write_pid_record(pid_path, lifecycle->record)) {
        return 0;
    }
    return 1;
}

static void lifecycle_release(V5LifecycleLock *lifecycle)
{
    char record[128];
    FILE *stream;
    if (!lifecycle || lifecycle->fd < 0) return;
    stream = fopen(lifecycle->pid_path, "r");
    if (stream) {
        size_t count = fread(record, 1u, sizeof(record) - 1u, stream);
        fclose(stream);
        record[count] = '\0';
        if (strcmp(record, lifecycle->record) == 0) unlink(lifecycle->pid_path);
    }
    flock(lifecycle->fd, LOCK_UN);
    close(lifecycle->fd);
    lifecycle->fd = -1;
}

static int mapped_writer_open(
    V5MappedWriter *writer,
    const char *path,
    size_t size)
{
    if (!writer || !path || !size || !mkdir_parent(path)) return 0;
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
    writer->fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (writer->fd < 0 || ftruncate(writer->fd, (off_t)size) != 0) return 0;
    writer->page = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
        writer->fd, 0);
    if (writer->page == MAP_FAILED) {
        writer->page = NULL;
        return 0;
    }
    writer->size = size;
    return 1;
}

static void mapped_writer_close(V5MappedWriter *writer)
{
    if (!writer) return;
    if (writer->page) munmap(writer->page, writer->size);
    if (writer->fd >= 0) close(writer->fd);
    memset(writer, 0, sizeof(*writer));
    writer->fd = -1;
}

static uint32_t next_sequence(V5MappedWriter *writer)
{
    writer->sequence += 2u;
    if (!writer->sequence) writer->sequence = 2u;
    return writer->sequence;
}

static void mapped_publish(
    V5MappedWriter *writer,
    const void *payload,
    size_t sequence_offset,
    uint32_t even_sequence)
{
    unsigned char *page = (unsigned char *)writer->page;
    const unsigned char *source = (const unsigned char *)payload;
    uint32_t odd_sequence = even_sequence > 1u ? even_sequence - 1u : 1u;
    memcpy(page + sequence_offset, &odd_sequence, sizeof(odd_sequence));
    memory_barrier();
    memcpy(page, source, sequence_offset);
    memcpy(page + sequence_offset + sizeof(uint32_t),
        source + sequence_offset + sizeof(uint32_t),
        writer->size - sequence_offset - sizeof(uint32_t));
    memory_barrier();
    memcpy(page + sequence_offset, &even_sequence, sizeof(even_sequence));
    memory_barrier();
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

static int sleep_until(uint64_t deadline_ns)
{
    struct timespec deadline;
    deadline.tv_sec = (time_t)(deadline_ns / 1000000000ull);
    deadline.tv_nsec = (long)(deadline_ns % 1000000000ull);
    while (!stop_requested) {
        int result = clock_nanosleep(
            CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        if (result == 0) return 1;
        if (result != EINTR) return 0;
    }
    return 1;
}

static void usage(const char *program)
{
    printf("usage: %s [--path PATH] [--bus-path PATH] "
        "[--interval-ms 33] [--once]\n", program);
}

int main(int argc, char **argv)
{
    const char *position_path = V5_POSITION_DEFAULT_PATH;
    const char *bus_path = V5_BUS_STATUS_DEFAULT_PATH;
    unsigned int interval_ms = 33u;
    int once = 0;
    int canonical;
    char lock_path[600];
    char pid_path[600];
    V5LifecycleLock lifecycle;
    V5MappedWriter position_writer;
    V5MappedWriter bus_writer;
    V5HalApi hal_api;
    V5PositionHalPins pins;
    V5PositionDisplayStabilizer stabilizer;
    V5NativePositionStatusBlock block;
    V5NativePositionStatusBlock last_published;
    int last_published_valid = 0;
    uint64_t last_publish_ns = 0ull;
    uint64_t source_generation = 0ull;
    uint32_t bus_source_generation = 0u;
    uint64_t next_bus_sample = 0ull;
    uint64_t next_status_log = 0ull;
    uint64_t next_deadline;
    unsigned long samples = 0ul;
    unsigned long published = 0ul;
    unsigned long missed_slots = 0ul;
    unsigned int consecutive_failures = 0u;
    int result = 1;
    int index;

    memset(&lifecycle, 0, sizeof(lifecycle));
    lifecycle.fd = -1;
    memset(&position_writer, 0, sizeof(position_writer));
    position_writer.fd = -1;
    memset(&bus_writer, 0, sizeof(bus_writer));
    bus_writer.fd = -1;
    memset(&hal_api, 0, sizeof(hal_api));
    hal_api.component_id = -1;
    v5_position_display_stabilizer_reset(&stabilizer);
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--path") == 0 && index + 1 < argc) {
            position_path = argv[++index];
        } else if (strcmp(argv[index], "--bus-path") == 0 && index + 1 < argc) {
            bus_path = argv[++index];
        } else if (strcmp(argv[index], "--interval-ms") == 0 &&
                   index + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++index], &end, 10);
            if (!end || *end || value > 60000ul) {
                usage(argv[0]);
                return 2;
            }
            interval_ms = (unsigned int)value;
        } else if (strcmp(argv[index], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[index], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (interval_ms < V5_MIN_INTERVAL_MS) interval_ms = V5_MIN_INTERVAL_MS;
    canonical = strcmp(position_path, V5_POSITION_DEFAULT_PATH) == 0;
    if (canonical) {
        strcpy(lock_path, V5_POSITION_LOCK_PATH);
        strcpy(pid_path, V5_POSITION_PID_PATH);
    } else if (snprintf(lock_path, sizeof(lock_path), "%s.publisher.lock",
                   position_path) >= (int)sizeof(lock_path) ||
               snprintf(pid_path, sizeof(pid_path), "%s.publisher.pid",
                   position_path) >= (int)sizeof(pid_path)) {
        fprintf(stderr, "position_publisher_path_too_long\n");
        return 2;
    }
    if (!lifecycle_acquire(&lifecycle, lock_path, pid_path)) goto cleanup;
    if (!mapped_writer_open(
            &position_writer, position_path,
            sizeof(V5NativePositionStatusBlock)) ||
        !mapped_writer_open(&bus_writer, bus_path, sizeof(V5BusStatusBlock))) {
        fprintf(stderr, "native_position_mapping_unavailable\n");
        goto cleanup;
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "v5_position_status_publisher mlockall failed: errno=%d\n",
            errno);
        goto cleanup;
    }
    if (!hal_api_open(&hal_api) || !hal_pins_bind(&hal_api, &pins)) goto cleanup;
    signal(SIGTERM, stop_handler);
    signal(SIGINT, stop_handler);
    next_deadline = monotonic_ns();
    while (!stop_requested) {
        V5PositionSourceSnapshot source;
        uint64_t sample_now = monotonic_ns();
        uint32_t sequence;
        int should_publish;
        if (sample_source(&pins, &source)) {
            ++source_generation;
            sequence = position_writer.sequence + 2u;
            if (!sequence) sequence = 2u;
            if (v5_position_status_build(
                    &source, lifecycle.writer_identity, sequence, sample_now,
                    source_generation, &stabilizer, &block)) {
                should_publish = !last_published_valid ||
                    !v5_position_status_display_equal(&block, &last_published) ||
                    sample_now - last_publish_ns >= V5_POSITION_HEARTBEAT_NS;
                if (should_publish) {
                    next_sequence(&position_writer);
                    mapped_publish(&position_writer, &block,
                        offsetof(V5NativePositionStatusBlock, seq), block.seq);
                    last_published = block;
                    last_published_valid = 1;
                    last_publish_ns = sample_now;
                    ++published;
                }
                consecutive_failures = 0u;
                ++samples;
                if (sample_now >= next_bus_sample) {
                    V5BusStatusBlock bus_block;
                    uint32_t bus_sequence = next_sequence(&bus_writer);
                    ++bus_source_generation;
                    sample_bus(&pins, lifecycle.writer_identity, bus_sequence,
                        bus_source_generation, monotonic_ns(), &bus_block);
                    mapped_publish(&bus_writer, &bus_block,
                        offsetof(V5BusStatusBlock, sequence), bus_sequence);
                    next_bus_sample = sample_now + V5_BUS_SAMPLE_NS;
                }
                if (once) {
                    result = 0;
                    break;
                }
            } else {
                v5_position_display_stabilizer_reset(&stabilizer);
                ++consecutive_failures;
            }
        } else {
            v5_position_display_stabilizer_reset(&stabilizer);
            ++consecutive_failures;
        }
        if (consecutive_failures == 1u || sample_now >= next_status_log) {
            if (consecutive_failures) {
                fprintf(stderr,
                    "v5_position_status_publisher sample unavailable: "
                    "native_position_sample_invalid\n");
            }
        }
        if (sample_now >= next_status_log) {
            printf("v5_position_status_publisher samples=%lu published=%lu "
                "missed_slots=%lu\n", samples, published, missed_slots);
            fflush(stdout);
            samples = 0ul;
            published = 0ul;
            missed_slots = 0ul;
            next_status_log = sample_now + V5_STATUS_LOG_NS;
        }
        next_deadline += (uint64_t)interval_ms * 1000000ull;
        sample_now = monotonic_ns();
        if (sample_now > next_deadline) {
            uint64_t interval_ns = (uint64_t)interval_ms * 1000000ull;
            uint64_t missed = (sample_now - next_deadline) / interval_ns + 1ull;
            next_deadline += missed * interval_ns;
            missed_slots += (unsigned long)missed;
        }
        if (!sleep_until(next_deadline)) {
            fprintf(stderr, "position_publisher_sleep_failed\n");
            goto cleanup;
        }
    }
    if (stop_requested) result = 0;

cleanup:
    hal_api_close(&hal_api);
    mapped_writer_close(&bus_writer);
    mapped_writer_close(&position_writer);
    lifecycle_release(&lifecycle);
    return result;
}
