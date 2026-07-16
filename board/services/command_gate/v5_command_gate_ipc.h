#ifndef V5_COMMAND_GATE_IPC_H
#define V5_COMMAND_GATE_IPC_H

#include "v5_command_gate.h"
#include "v5_settings_apply.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_COMMAND_GATE_SOCKET_PATH "/run/8ax_v5_product_ui/v5_command_gate.sock"
#define V5_COMMAND_GATE_IPC_MAGIC 0x56354347u
#define V5_COMMAND_GATE_IPC_VERSION 4u
#define V5_COMMAND_GATE_TEXT_CAP 512u
#define V5_COMMAND_GATE_SECONDARY_TEXT_CAP 128u
#define V5_COMMAND_GATE_MODE_CAP 64u
#define V5_COMMAND_GATE_LINE_CAP 384u
#define V5_COMMAND_GATE_PROJECT_ROOT_CAP 256u
#define V5_COMMAND_GATE_AXIS_CAP 16u
#define V5_COMMAND_GATE_FIELD_KEY_CAP 64u
#define V5_COMMAND_GATE_FIELD_NAME_CAP 128u
#define V5_COMMAND_GATE_SETTING_VALUE_CAP 128u

#define V5_COMMAND_GATE_SEND_UNAVAILABLE 0
#define V5_COMMAND_GATE_SEND_SENT 1
#define V5_COMMAND_GATE_SEND_INVALID -1
#define V5_COMMAND_GATE_SEND_IO_ERROR -2

typedef enum V5CommandGateIpcOp {
    V5_COMMAND_GATE_IPC_OP_EXECUTE = 1,
    V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY = 2,
    V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_COMMIT = 3,
    V5_COMMAND_GATE_IPC_OP_PROBE_HOME_STATUS = 4,
    V5_COMMAND_GATE_IPC_OP_PROBE_ESTOP_CLEAN = 5,
} V5CommandGateIpcOp;

typedef struct V5CommandGateIpcRequestFrame {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t op;
    int32_t kind;
    int32_t index_value;
    int32_t enabled_value;
    uint32_t axis_mask;
    uint64_t home_run_id;
    uint32_t home_generation;
    uint32_t estop_clean_generation;
    double axis_value;
    double increment_value;
    double point_axis[V5_COMMAND_AXIS_COUNT];
    char text_value[V5_COMMAND_GATE_TEXT_CAP];
    char secondary_text_value[V5_COMMAND_GATE_SECONDARY_TEXT_CAP];
    char mode_value[V5_COMMAND_GATE_MODE_CAP];
    uint32_t settings_axis_index;
    uint32_t settings_owner_generation;
    uint32_t settings_readback_token;
    char settings_project_root[V5_COMMAND_GATE_PROJECT_ROOT_CAP];
    char settings_axis[V5_COMMAND_GATE_AXIS_CAP];
    char settings_field_key[V5_COMMAND_GATE_FIELD_KEY_CAP];
    char settings_field_name[V5_COMMAND_GATE_FIELD_NAME_CAP];
    char settings_value_text[V5_COMMAND_GATE_SETTING_VALUE_CAP];
} V5CommandGateIpcRequestFrame;

typedef struct V5CommandGateIpcResponseFrame {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    int32_t send_status;
    int32_t executed;
    int32_t machine_on_requested;
    int32_t machine_on_status;
    int32_t safety_estop_known;
    int32_t safety_estop_active;
    int32_t machine_enable_known;
    int32_t machine_enabled;
    uint32_t estop_clean_generation;
    int32_t estop_clean_active;
    int32_t estop_clean_terminal;
    int32_t estop_clean_ok;
    char estop_clean_code[64];
    char command_line[V5_COMMAND_GATE_LINE_CAP];
    char readback_code[64];
    int32_t settings_owner_written;
    int32_t settings_source_readback_confirmed;
    int32_t settings_restart_pending;
    int32_t settings_scale_chain_attempted;
    int32_t settings_scale_recomputed;
    int32_t settings_raw_limits_recomputed;
    double settings_effective_scale;
    double settings_raw_zero_position;
    double settings_raw_min_limit;
    double settings_raw_max_limit;
    char settings_readback_value[64];
    char settings_scale_chain_code[64];
    uint64_t home_run_id;
    uint32_t home_generation;
    uint32_t home_phase;
    uint32_t home_failure_phase;
    uint32_t home_current_axis_mask;
    int32_t home_active;
    int32_t home_terminal;
    int32_t home_cancelled;
    int32_t home_detail_valid;
    double home_actual;
    double home_target;
    char home_mode[8];
    char home_current_axes[16];
    char home_direct_reason[64];
} V5CommandGateIpcResponseFrame;

typedef struct V5CommandGateHomeStatus {
    unsigned long long run_id;
    unsigned int generation;
    unsigned int phase;
    unsigned int failure_phase;
    unsigned int current_axis_mask;
    int active;
    int terminal;
    int cancelled;
    int detail_valid;
    double actual;
    double target;
    char mode[8];
    char current_axes[16];
    char direct_reason[64];
} V5CommandGateHomeStatus;

typedef struct V5CommandGateEstopCleanStatus {
    unsigned int generation;
    int active;
    int terminal;
    int ok;
    char code[64];
} V5CommandGateEstopCleanStatus;

typedef struct V5CommandGateResult {
    int send_status;
    int executed;
    int machine_on_requested;
    int machine_on_status;
    int safety_estop_known;
    int safety_estop_active;
    int machine_enable_known;
    int machine_enabled;
    /* Local client aliases decoded from existing wire fields for drive-window EXECUTE. */
    int drive_window_initial_machine_enabled;
    int drive_window_final_machine_enabled;
    unsigned int estop_clean_generation;
    int estop_clean_active;
    int estop_clean_terminal;
    int estop_clean_ok;
    char estop_clean_code[64];
    char command_line[V5_COMMAND_GATE_LINE_CAP];
    char readback_code[64];
} V5CommandGateResult;

void v5_command_gate_result_init(V5CommandGateResult *result);
int v5_command_gate_send_prepared(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms);
int v5_command_gate_send_prepared_home(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms,
    unsigned long long run_id,
    unsigned int generation);
int v5_command_gate_probe_home_status(
    unsigned long long run_id,
    unsigned int generation,
    V5CommandGateHomeStatus *status,
    unsigned int timeout_ms);
int v5_command_gate_probe_estop_clean(
    unsigned int generation,
    V5CommandGateEstopCleanStatus *status,
    unsigned int timeout_ms);
int v5_command_gate_probe_safety(V5CommandGateResult *result, unsigned int timeout_ms);
int v5_command_gate_settings_axis_commit(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result,
    unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
