#ifndef V5_BUS_STATUS_READER_H
#define V5_BUS_STATUS_READER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_BUS_STATUS_DEFAULT_PATH "/dev/shm/v5_native_bus_status.bin"
#define V5_BUS_STATUS_DEFAULT_MAX_AGE_MS 1000U
#define V5_BUS_STATUS_JOINT_COUNT 5U

#define V5_BUS_MASTER_LINK_UP (1U << 0)
#define V5_BUS_MASTER_STATE_OP (1U << 1)
#define V5_BUS_MASTER_ALL_OP (1U << 2)
#define V5_BUS_JOINT_SLAVE_OP (1U << 0)

typedef struct V5BusJointStatus {
    int valid;
    char axis;
    uint32_t slave_position;
    uint32_t flags;
    uint32_t statusword;
} V5BusJointStatus;

typedef struct V5BusStatus {
    int valid;
    uint32_t writer_identity;
    uint32_t mapping_generation;
    uint32_t active_mask;
    uint32_t master_flags;
    uint32_t slaves_responding;
    uint32_t active_count;
    uint32_t source_generation;
    uint64_t monotonic_ns;
    V5BusJointStatus joints[V5_BUS_STATUS_JOINT_COUNT];
} V5BusStatus;

void v5_bus_status_init(V5BusStatus *status);
int v5_bus_status_read(
    const char *path,
    unsigned int max_age_ms,
    V5BusStatus *status);

#ifdef __cplusplus
}
#endif

#endif
