#ifndef V5_BACKEND_READINESS_PROTOCOL_H
#define V5_BACKEND_READINESS_PROTOCOL_H

#include <stdint.h>

#define V5_BACKEND_READINESS_SOCKET_PATH \
    "/run/8ax_v5_product_ui/v5_backend_readiness.sock"
#define V5_BACKEND_READINESS_MAGIC 0x56354252u
#define V5_BACKEND_READINESS_VERSION 2u
#define V5_BACKEND_READINESS_CODE_CAP 64u
#define V5_BACKEND_DRIVE_IDENTITY_SHA256_SIZE 32u

enum V5BackendReadinessOperation {
    V5_BACKEND_READINESS_OP_STATUS = 1,
    V5_BACKEND_READINESS_OP_ARM = 2,
    V5_BACKEND_READINESS_OP_DRIVE_VERIFY = 3,
    V5_BACKEND_READINESS_OP_DRIVE_INVALIDATE = 4
};

enum V5BackendReadinessStatus {
    V5_BACKEND_READINESS_STATUS_OK = 0,
    V5_BACKEND_READINESS_STATUS_UNAVAILABLE = 1,
    V5_BACKEND_READINESS_STATUS_BAD_REQUEST = 2,
    V5_BACKEND_READINESS_STATUS_REVOKED = 3
};

typedef struct V5BackendReadinessRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t operation;
    uint64_t request_id;
    uint32_t generation;
    uint32_t drive_mapping_generation;
    uint32_t drive_axis_mask;
    uint32_t drive_readback_count;
    uint64_t drive_readback_started_ns;
    uint8_t drive_transaction_sha256[V5_BACKEND_DRIVE_IDENTITY_SHA256_SIZE];
} V5BackendReadinessRequest;

typedef struct V5BackendReadinessResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t operation;
    uint64_t request_id;
    uint32_t status;
    uint32_t owner_pid;
    uint64_t owner_start_ticks;
    uint32_t generation;
    uint32_t armed;
    uint32_t backend_data_ready;
    uint32_t motion_backend_ready;
    uint32_t revoked;
    uint32_t cpu_contract_ready;
    uint32_t expected_slaves;
    uint32_t actual_slaves;
    uint32_t expected_wkc;
    uint32_t actual_wkc;
    uint32_t wkc_complete;
    uint32_t all_op;
    uint32_t dc_phased;
    uint32_t dc_time_valid;
    uint32_t dc_time_age_cycles;
    uint32_t dc_time_ok_seq;
    uint32_t dc_time_error_count;
    uint32_t drive_verified;
    uint32_t drive_mapping_generation;
    uint32_t drive_axis_mask;
    uint32_t drive_readback_count;
    uint64_t drive_readback_started_ns;
    uint64_t drive_verified_ns;
    uint8_t drive_transaction_sha256[V5_BACKEND_DRIVE_IDENTITY_SHA256_SIZE];
    uint64_t cpu_contract_ready_ns;
    uint64_t first_full_wkc_ns;
    uint64_t dc_fresh_pair_ready_ns;
    uint64_t backend_data_ready_ns;
    uint64_t backend_ready_published_ns;
    char code[V5_BACKEND_READINESS_CODE_CAP];
} V5BackendReadinessResponse;

#endif
