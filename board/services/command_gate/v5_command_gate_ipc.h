#ifndef V5_COMMAND_GATE_IPC_H
#define V5_COMMAND_GATE_IPC_H

#include "v5_command_gate.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_COMMAND_GATE_SOCKET_PATH "/run/8ax_v5_product_ui/v5_command_gate.sock"
#define V5_COMMAND_GATE_IPC_MAGIC 0x56354347u
#define V5_COMMAND_GATE_IPC_VERSION 1u
#define V5_COMMAND_GATE_TEXT_CAP 512u
#define V5_COMMAND_GATE_SECONDARY_TEXT_CAP 128u
#define V5_COMMAND_GATE_MODE_CAP 64u
#define V5_COMMAND_GATE_LINE_CAP 384u

#define V5_COMMAND_GATE_SEND_UNAVAILABLE 0
#define V5_COMMAND_GATE_SEND_SENT 1
#define V5_COMMAND_GATE_SEND_INVALID -1
#define V5_COMMAND_GATE_SEND_IO_ERROR -2

typedef enum V5CommandGateIpcOp {
    V5_COMMAND_GATE_IPC_OP_EXECUTE = 1,
    V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY = 2,
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
    double axis_value;
    double increment_value;
    double point_axis[V5_COMMAND_AXIS_COUNT];
    char text_value[V5_COMMAND_GATE_TEXT_CAP];
    char secondary_text_value[V5_COMMAND_GATE_SECONDARY_TEXT_CAP];
    char mode_value[V5_COMMAND_GATE_MODE_CAP];
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
    char command_line[V5_COMMAND_GATE_LINE_CAP];
    char readback_code[64];
} V5CommandGateIpcResponseFrame;

typedef struct V5CommandGateResult {
    int send_status;
    int executed;
    int machine_on_requested;
    int machine_on_status;
    int safety_estop_known;
    int safety_estop_active;
    int machine_enable_known;
    int machine_enabled;
    char command_line[V5_COMMAND_GATE_LINE_CAP];
    char readback_code[64];
} V5CommandGateResult;

void v5_command_gate_result_init(V5CommandGateResult *result);
int v5_command_gate_send_prepared(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms);
int v5_command_gate_probe_safety(V5CommandGateResult *result, unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
