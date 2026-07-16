#include "v5_command_gate_ipc.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

void v5_command_gate_result_init(V5CommandGateResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
}

static void copy_cstr(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (!src || !src[0]) {
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void result_from_frame(V5CommandGateResult *result, const V5CommandGateIpcResponseFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->send_status = frame->send_status;
    result->executed = frame->executed;
    result->machine_on_requested = frame->machine_on_requested;
    result->machine_on_status = frame->machine_on_status;
    result->safety_estop_known = frame->safety_estop_known;
    result->safety_estop_active = frame->safety_estop_active;
    result->machine_enable_known = frame->machine_enable_known;
    result->machine_enabled = frame->machine_enabled;
    result->drive_window_initial_machine_enabled = frame->machine_on_requested;
    result->drive_window_final_machine_enabled = frame->machine_enabled;
    result->estop_clean_generation = frame->estop_clean_generation;
    result->estop_clean_active = frame->estop_clean_active;
    result->estop_clean_terminal = frame->estop_clean_terminal;
    result->estop_clean_ok = frame->estop_clean_ok;
    copy_cstr(result->estop_clean_code, sizeof(result->estop_clean_code), frame->estop_clean_code);
    copy_cstr(result->command_line, sizeof(result->command_line), frame->command_line);
    copy_cstr(result->readback_code, sizeof(result->readback_code), frame->readback_code);
}

static void settings_result_init(V5SettingsApplyAxisCommitResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->scale_chain.code, sizeof(result->scale_chain.code), "%s", "IPC_NOT_ATTEMPTED");
}

static void settings_result_from_frame(
    V5SettingsApplyAxisCommitResult *result,
    const V5CommandGateIpcResponseFrame *frame)
{
    if (!result || !frame) {
        return;
    }
    result->owner_written = frame->settings_owner_written ? 1 : 0;
    result->source_readback_confirmed = frame->settings_source_readback_confirmed ? 1 : 0;
    result->restart_pending = frame->settings_restart_pending ? 1 : 0;
    result->scale_chain.attempted = frame->settings_scale_chain_attempted ? 1 : 0;
    result->scale_chain.scale_recomputed = frame->settings_scale_recomputed ? 1 : 0;
    result->scale_chain.raw_limits_recomputed = frame->settings_raw_limits_recomputed ? 1 : 0;
    result->scale_chain.effective_scale = frame->settings_effective_scale;
    result->scale_chain.raw_zero_position = frame->settings_raw_zero_position;
    result->scale_chain.raw_min_limit = frame->settings_raw_min_limit;
    result->scale_chain.raw_max_limit = frame->settings_raw_max_limit;
    copy_cstr(result->readback_value, sizeof(result->readback_value), frame->settings_readback_value);
    copy_cstr(result->scale_chain.code, sizeof(result->scale_chain.code), frame->settings_scale_chain_code);
}

#ifndef _WIN32
static int set_socket_timeout(int fd, unsigned int timeout_ms)
{
    struct timeval tv;
    if (timeout_ms == 0U) {
        timeout_ms = 1000U;
    }
    tv.tv_sec = (time_t)(timeout_ms / 1000U);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static int read_all(int fd, void *data, size_t size)
{
    char *p = (char *)data;
    while (size > 0U) {
        ssize_t n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return 0;
        }
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static int gate_connect(unsigned int timeout_ms)
{
    struct sockaddr_un addr;
    int fd;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    (void)set_socket_timeout(fd, timeout_ms);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", V5_COMMAND_GATE_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int transact_response(
    const V5CommandGateIpcRequestFrame *request,
    V5CommandGateIpcResponseFrame *response,
    unsigned int timeout_ms)
{
    int fd;
    int attempt;
    if (!request || !response) {
        return 0;
    }
    memset(response, 0, sizeof(*response));
    for (attempt = 0; attempt < 2; ++attempt) {
        fd = gate_connect(timeout_ms);
        if (fd < 0) {
            return 0;
        }
        if (write_all(fd, request, sizeof(*request)) && read_all(fd, response, sizeof(*response)) &&
            response->magic == V5_COMMAND_GATE_IPC_MAGIC &&
            response->version == V5_COMMAND_GATE_IPC_VERSION &&
            response->size == (uint32_t)sizeof(*response)) {
            close(fd);
            return 1;
        }
        close(fd);
    }
    return 0;
}

static int transact(const V5CommandGateIpcRequestFrame *request, V5CommandGateResult *result, unsigned int timeout_ms)
{
    V5CommandGateIpcResponseFrame response;
    v5_command_gate_result_init(result);
    if (!transact_response(request, &response, timeout_ms)) {
        return 0;
    }
    result_from_frame(result, &response);
    return 1;
}
#endif

static void init_request_frame(V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcOp op)
{
    memset(frame, 0, sizeof(*frame));
    frame->magic = V5_COMMAND_GATE_IPC_MAGIC;
    frame->version = V5_COMMAND_GATE_IPC_VERSION;
    frame->size = (uint32_t)sizeof(*frame);
    frame->op = (uint32_t)op;
}

int v5_command_gate_send_prepared(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)prepared;
    (void)request;
    (void)timeout_ms;
    v5_command_gate_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    unsigned int i;
    v5_command_gate_result_init(result);
    if (!prepared || !prepared->accepted || !request || request->kind == V5_COMMAND_UI_LOCAL) {
        if (result) {
            result->send_status = V5_COMMAND_GATE_SEND_INVALID;
        }
        return 0;
    }
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_EXECUTE);
    frame.kind = (int32_t)request->kind;
    frame.index_value = (int32_t)request->index_value;
    frame.enabled_value = (int32_t)request->enabled_value;
    frame.axis_mask = request->axis_mask;
    frame.axis_value = request->axis_value;
    frame.increment_value = request->increment_value;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) {
        frame.point_axis[i] = request->point_axis[i];
    }
    copy_cstr(frame.text_value, sizeof(frame.text_value), request->text_value);
    copy_cstr(frame.secondary_text_value, sizeof(frame.secondary_text_value), request->secondary_text_value);
    copy_cstr(frame.mode_value, sizeof(frame.mode_value), request->mode_value);
    return transact(&frame, result, timeout_ms);
#endif
}

int v5_command_gate_send_prepared_home(
    const V5CommandPrepared *prepared,
    const V5CommandRequest *request,
    V5CommandGateResult *result,
    unsigned int timeout_ms,
    unsigned long long run_id,
    unsigned int generation)
{
#ifdef _WIN32
    (void)prepared; (void)request; (void)timeout_ms; (void)run_id; (void)generation;
    v5_command_gate_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    unsigned int i;
    v5_command_gate_result_init(result);
    if (!prepared || !prepared->accepted || !request || !run_id || !generation ||
        (request->kind != V5_COMMAND_HOME && request->kind != V5_COMMAND_AXIS_ZERO_POSITION)) {
        if (result) result->send_status = V5_COMMAND_GATE_SEND_INVALID;
        return 0;
    }
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_EXECUTE);
    frame.kind = (int32_t)request->kind;
    frame.index_value = (int32_t)request->index_value;
    frame.enabled_value = (int32_t)request->enabled_value;
    frame.axis_mask = request->axis_mask;
    frame.home_run_id = (uint64_t)run_id;
    frame.home_generation = generation;
    frame.axis_value = request->axis_value;
    frame.increment_value = request->increment_value;
    for (i = 0U; i < V5_COMMAND_AXIS_COUNT; ++i) frame.point_axis[i] = request->point_axis[i];
    copy_cstr(frame.text_value, sizeof(frame.text_value), request->text_value);
    copy_cstr(frame.secondary_text_value, sizeof(frame.secondary_text_value), request->secondary_text_value);
    copy_cstr(frame.mode_value, sizeof(frame.mode_value), request->mode_value);
    return transact(&frame, result, timeout_ms);
#endif
}

int v5_command_gate_probe_home_status(
    unsigned long long run_id,
    unsigned int generation,
    V5CommandGateHomeStatus *status,
    unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)run_id; (void)generation; (void)timeout_ms;
    if (status) memset(status, 0, sizeof(*status));
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    V5CommandGateIpcResponseFrame response;
    if (!status || !run_id || !generation) return 0;
    memset(status, 0, sizeof(*status));
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_PROBE_HOME_STATUS);
    frame.home_run_id = (uint64_t)run_id;
    frame.home_generation = generation;
    if (!transact_response(&frame, &response, timeout_ms) ||
        response.home_run_id != (uint64_t)run_id || response.home_generation != generation) return 0;
    status->run_id = (unsigned long long)response.home_run_id;
    status->generation = response.home_generation;
    status->phase = response.home_phase;
    status->failure_phase = response.home_failure_phase;
    status->current_axis_mask = response.home_current_axis_mask;
    status->active = response.home_active;
    status->terminal = response.home_terminal;
    status->cancelled = response.home_cancelled;
    status->detail_valid = response.home_detail_valid;
    status->actual = response.home_actual;
    status->target = response.home_target;
    copy_cstr(status->mode, sizeof(status->mode), response.home_mode);
    copy_cstr(status->current_axes, sizeof(status->current_axes), response.home_current_axes);
    copy_cstr(status->direct_reason, sizeof(status->direct_reason), response.home_direct_reason);
    return 1;
#endif
}

int v5_command_gate_probe_safety(V5CommandGateResult *result, unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)timeout_ms;
    v5_command_gate_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_PROBE_SAFETY);
    return transact(&frame, result, timeout_ms);
#endif
}

int v5_command_gate_probe_estop_clean(
    unsigned int generation,
    V5CommandGateEstopCleanStatus *status,
    unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)generation; (void)timeout_ms;
    if (status) memset(status, 0, sizeof(*status));
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    V5CommandGateIpcResponseFrame response;
    if (!status) return 0;
    memset(status, 0, sizeof(*status));
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_PROBE_ESTOP_CLEAN);
    frame.estop_clean_generation = generation;
    if (!transact_response(&frame, &response, timeout_ms) ||
        (generation != 0U && response.estop_clean_generation != generation)) {
        return 0;
    }
    status->generation = response.estop_clean_generation;
    status->active = response.estop_clean_active;
    status->terminal = response.estop_clean_terminal;
    status->ok = response.estop_clean_ok;
    copy_cstr(status->code, sizeof(status->code), response.estop_clean_code);
    return response.send_status == V5_COMMAND_GATE_SEND_SENT;
#endif
}

int v5_command_gate_settings_axis_commit(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result,
    unsigned int timeout_ms)
{
#ifdef _WIN32
    (void)request;
    (void)timeout_ms;
    settings_result_init(result);
    return 0;
#else
    V5CommandGateIpcRequestFrame frame;
    V5CommandGateIpcResponseFrame response;
    settings_result_init(result);
    if (!request || !request->project_root || !request->axis || !request->field_key ||
        !request->field_name || !request->value_text) {
        return 0;
    }
    init_request_frame(&frame, V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_COMMIT);
    frame.settings_axis_index = request->axis_index;
    frame.settings_owner_generation = request->owner_generation;
    frame.settings_readback_token = request->readback_token;
    copy_cstr(frame.settings_project_root, sizeof(frame.settings_project_root), request->project_root);
    copy_cstr(frame.settings_axis, sizeof(frame.settings_axis), request->axis);
    copy_cstr(frame.settings_field_key, sizeof(frame.settings_field_key), request->field_key);
    copy_cstr(frame.settings_field_name, sizeof(frame.settings_field_name), request->field_name);
    copy_cstr(frame.settings_value_text, sizeof(frame.settings_value_text), request->value_text);
    if (!transact_response(&frame, &response, timeout_ms)) {
        return 0;
    }
    settings_result_from_frame(result, &response);
    return response.send_status == V5_COMMAND_GATE_SEND_SENT &&
           response.settings_source_readback_confirmed ? 1 : 0;
#endif
}
