#ifndef V5_NATIVE_HAL_OWNER_PROTOCOL_H
#define V5_NATIVE_HAL_OWNER_PROTOCOL_H

#include <stdint.h>

#define V5_NATIVE_HAL_OWNER_SOCKET_PATH "/run/8ax_v5_product_ui/v5_native_hal_owner.sock"
#define V5_NATIVE_HAL_OWNER_MAGIC 0x5635484fu
#define V5_NATIVE_HAL_OWNER_VERSION 1u
#define V5_NATIVE_HAL_OWNER_CODE_CAP 64u

enum V5NativeHalOwnerOperation {
    V5_NATIVE_HAL_OWNER_OP_STATUS = 1,
    V5_NATIVE_HAL_OWNER_OP_ESTOP_FORCE = 2,
    V5_NATIVE_HAL_OWNER_OP_ESTOP_RESET = 3,
    V5_NATIVE_HAL_OWNER_OP_RTCP_SET = 4
};

enum V5NativeHalOwnerStatus {
    V5_NATIVE_HAL_OWNER_STATUS_OK = 0,
    V5_NATIVE_HAL_OWNER_STATUS_UNAVAILABLE = 1,
    V5_NATIVE_HAL_OWNER_STATUS_BAD_REQUEST = 2,
    V5_NATIVE_HAL_OWNER_STATUS_NOT_CONFIRMED = 3
};

typedef struct V5NativeHalOwnerRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t operation;
    uint64_t request_id;
    uint32_t target;
    uint32_t reserved;
} V5NativeHalOwnerRequest;

typedef struct V5NativeHalOwnerResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t operation;
    uint64_t request_id;
    uint32_t status;
    uint32_t safety_estop_known;
    uint32_t safety_estop_active;
    uint32_t machine_enable_known;
    uint32_t machine_enabled;
    uint32_t rtcp_actual_known;
    uint32_t rtcp_actual_active;
    uint32_t reserved;
    char code[V5_NATIVE_HAL_OWNER_CODE_CAP];
} V5NativeHalOwnerResponse;

#define V5_NATIVE_SAFETY_STATUS_MAGIC 0x56355346u
#define V5_NATIVE_SAFETY_STATUS_VERSION 2u
#define V5_NATIVE_SAFETY_STATUS_PATH "/dev/shm/v5_native_safety_latch.bin"

typedef struct V5NativeSafetyStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint64_t monotonic_ns;
    uint32_t safety_estop_known;
    uint32_t safety_estop_active;
    uint32_t machine_enable_known;
    uint32_t machine_enabled;
    uint32_t crc32;
    uint32_t reserved;
} V5NativeSafetyStatusBlock;

#define V5_NATIVE_RTCP_STATUS_MAGIC 0x56525443u
#define V5_NATIVE_RTCP_STATUS_VERSION 1u
#define V5_NATIVE_RTCP_STATUS_PATH "/dev/shm/v5_native_rtcp_status.bin"

typedef struct V5NativeRtcpStatusBlockWire {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t active;
    uint32_t reserved;
    uint64_t monotonic_ns;
    uint32_t crc32;
    uint32_t reserved2;
} V5NativeRtcpStatusBlockWire;

#define V5_NATIVE_G53_STATUS_MAGIC 0x56354753u
#define V5_NATIVE_G53_STATUS_VERSION 2u
#define V5_NATIVE_G53_STATUS_PATH "/dev/shm/v5_native_g53_geometry_status.bin"
#define V5_NATIVE_G53_CENTER_COUNT 3u
#define V5_NATIVE_G53_AXIS_COUNT 3u
#define V5_NATIVE_G53_MODEL_CAP 32u

typedef struct V5NativeG53StatusBlockWire {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t center_count;
    uint32_t axis_count;
    uint32_t epoch;
    uint32_t reserved0;
    uint64_t monotonic_ns;
    double centers[V5_NATIVE_G53_CENTER_COUNT][V5_NATIVE_G53_AXIS_COUNT];
    char motion_model[V5_NATIVE_G53_MODEL_CAP];
    uint32_t crc32;
    uint32_t reserved1;
} V5NativeG53StatusBlockWire;

#endif
