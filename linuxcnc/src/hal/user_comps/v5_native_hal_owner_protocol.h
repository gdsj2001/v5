#ifndef V5_NATIVE_HAL_OWNER_PROTOCOL_H
#define V5_NATIVE_HAL_OWNER_PROTOCOL_H

#include <stdint.h>

#define V5_NATIVE_HAL_OWNER_SOCKET_PATH "/run/8ax_v5_product_ui/v5_native_hal_owner.sock"
#define V5_NATIVE_HAL_OWNER_MAGIC 0x5635484fu
#define V5_NATIVE_HAL_OWNER_VERSION 7u
#define V5_NATIVE_HAL_OWNER_CODE_CAP 64u
#define V5_NATIVE_HOME_JOINT_COUNT 5u

#define V5_NATIVE_HOME_CONFIG_ACTIVE 0x1u
#define V5_NATIVE_HOME_CONFIG_COMMIT 0x2u
#define V5_NATIVE_HOME_CONFIG_HOME_READY 0x4u
#define V5_NATIVE_HOME_CONFIG_FLAG_MASK \
    (V5_NATIVE_HOME_CONFIG_ACTIVE | V5_NATIVE_HOME_CONFIG_COMMIT | \
     V5_NATIVE_HOME_CONFIG_HOME_READY)

enum V5NativeHalOwnerOperation {
    V5_NATIVE_HAL_OWNER_OP_STATUS = 1,
    V5_NATIVE_HAL_OWNER_OP_ESTOP_FORCE = 2,
    V5_NATIVE_HAL_OWNER_OP_ESTOP_RESET = 3,
    V5_NATIVE_HAL_OWNER_OP_RTCP_SET = 4,
    V5_NATIVE_HAL_OWNER_OP_RTCP_FORCE_OFF = 5,
    V5_NATIVE_HAL_OWNER_OP_WCHECKPOINT_STATUS = 6,
    V5_NATIVE_HAL_OWNER_OP_HOME_CONFIG = 7,
    V5_NATIVE_HAL_OWNER_OP_HOME_STATUS = 8
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
    uint32_t flags;
    uint32_t home_status_slot;
    uint32_t axis_code;
    uint32_t home_slave_position;
    uint32_t home_mapping_generation;
    uint32_t home_expected_active_mask;
    uint32_t home_config_commit_seq;
    double home_zero_counts;
    double home_counts_per_unit;
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
    uint32_t wcheckpoint_valid;
    uint32_t wcheckpoint_generation;
    double wcheckpoint_logical_counts;
    double wcheckpoint_base_counts;
    double wcheckpoint_runtime_counts;
    uint32_t home_config_readback_valid;
    uint32_t home_mapping_generation;
    uint32_t home_config_active_mask;
    uint32_t home_config_commit_seq;
    uint32_t home_config_mask;
    uint32_t status_home_router_mapping_valid;
    uint32_t status_home_router_mapping_generation;
    uint32_t status_home_router_active_mask;
    uint32_t status_home_router_commit_seq;
    uint32_t status_home_router_rejected_commit_seq;
    uint32_t home_status_consistent;
    uint32_t home_active_mask;
    uint32_t home_complete_mask;
    uint32_t home_failed_mask;
    uint32_t home_cancelled_mask;
    uint32_t home_current_joint;
    uint32_t home_current_mask;
    uint32_t home_axis_code;
    uint32_t home_axis_code_by_joint[V5_NATIVE_HOME_JOINT_COUNT];
    uint32_t home_phase;
    uint32_t status_home_failure_phase;
    uint32_t home_failure;
    uint32_t home_transaction;
    uint32_t home_joint_transaction;
    uint32_t home_expected_active_mask;
    uint32_t status_home_terminal_mask;
    uint32_t home_detail_readback_valid;
    uint32_t home_detail_motion_active;
    uint32_t home_detail_generation;
    uint32_t rtcp_home_request_transaction;
    uint32_t rtcp_home_ack_transaction;
    double home_start_counts;
    double home_actual_counts;
    double home_target_counts;
    double home_error_counts;
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
    uint32_t active_field_mask;
    uint64_t monotonic_ns;
    double centers[V5_NATIVE_G53_CENTER_COUNT][V5_NATIVE_G53_AXIS_COUNT];
    char motion_model[V5_NATIVE_G53_MODEL_CAP];
    uint32_t crc32;
    uint32_t reserved1;
} V5NativeG53StatusBlockWire;

#endif
