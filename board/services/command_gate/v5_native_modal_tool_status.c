#include "v5_native_modal_tool_status.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_MODAL_TOOL_MAGIC 0x564D544Cu
#define V5_MODAL_TOOL_VERSION 4u

typedef struct V5NativeModalToolStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t modal_valid;
    uint32_t tool_valid;
    uint32_t tool_length_valid;
    uint32_t interpreter_idle_valid;
    uint32_t interpreter_paused_valid;
    int32_t tool_number;
    uint32_t interpreter_idle;
    uint32_t interpreter_paused;
    uint64_t monotonic_ns;
    char modal_text[V5_NATIVE_MODAL_TOOL_STATUS_TEXT_CAP];
    double tool_length_mm;
    uint32_t all_homed_valid;
    uint32_t all_homed;
    uint32_t current_line_valid;
    int32_t current_line;
    uint32_t motion_line_valid;
    int32_t motion_line;
    uint32_t mdi_run_valid;
    uint32_t mdi_run_active;
    int32_t mdi_run_line;
    char mdi_run_command[V5_NATIVE_READBACK_MDI_COMMAND_CAP];
    uint32_t crc32;
    uint32_t reserved4;
    uint32_t reserved5;
} V5NativeModalToolStatusBlock;

static const char *modal_tool_status_path(const char *path)
{
    return (path && path[0]) ? path : V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_PATH;
}

static uint64_t modal_tool_monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t modal_tool_crc32_like(const V5NativeModalToolStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    size_t limit = offsetof(V5NativeModalToolStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < limit; ++i) {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static int modal_tool_block_fresh(const V5NativeModalToolStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    if (!block || block->magic != V5_MODAL_TOOL_MAGIC || block->version != V5_MODAL_TOOL_VERSION ||
        block->size != (uint32_t)sizeof(*block) || block->crc32 != modal_tool_crc32_like(block) || !block->valid) {
        return 0;
    }
    now = modal_tool_monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(max_age_ms ? max_age_ms : V5_NATIVE_MODAL_TOOL_STATUS_DEFAULT_MAX_AGE_MS) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

int v5_native_modal_tool_status_read(const char *path, unsigned int max_age_ms, V5NativeReadback *readback)
{
    FILE *fp;
    V5NativeModalToolStatusBlock block;
    const char *actual_path;
    if (!readback) {
        return 0;
    }
    actual_path = modal_tool_status_path(path);
    fp = fopen(actual_path, "rb");
    if (!fp) {
        v5_native_readback_set_unavailable(readback, "modal_tool_status_block_missing");
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        v5_native_readback_set_unavailable(readback, "modal_tool_status_block_short_read");
        return 0;
    }
    fclose(fp);
    if (!modal_tool_block_fresh(&block, max_age_ms)) {
        v5_native_readback_set_unavailable(readback, "modal_tool_status_block_invalid_or_stale");
        return 0;
    }
    v5_native_readback_init(readback);
    if (block.modal_valid && block.modal_text[0]) {
        block.modal_text[V5_NATIVE_MODAL_TOOL_STATUS_TEXT_CAP - 1U] = '\0';
        v5_native_readback_set_modal_actual(readback, block.modal_text);
    }
    if (block.tool_valid && block.tool_number >= 0) {
        v5_native_readback_set_tool_actual(
            readback,
            (int)block.tool_number,
            block.tool_length_valid && isfinite(block.tool_length_mm),
            block.tool_length_mm);
    }
    if (block.interpreter_idle_valid) {
        v5_native_readback_set_interpreter_idle(readback, block.interpreter_idle != 0U);
    }
    if (block.interpreter_paused_valid) {
        v5_native_readback_set_interpreter_paused(readback, block.interpreter_paused != 0U);
    }
    if (block.all_homed_valid) {
        v5_native_readback_set_all_homed(readback, block.all_homed != 0U);
    }
    if (block.current_line_valid) {
        v5_native_readback_set_current_line(readback, (int)block.current_line);
    }
    if (block.motion_line_valid) {
        v5_native_readback_set_motion_line(readback, (int)block.motion_line);
    }
    if (block.mdi_run_valid) {
        block.mdi_run_command[V5_NATIVE_READBACK_MDI_COMMAND_CAP - 1U] = '\0';
        v5_native_readback_set_mdi_run_actual(
            readback,
            block.mdi_run_active != 0U,
            (int)block.mdi_run_line,
            block.mdi_run_command);
    }
    return v5_native_readback_modal_known(readback) || v5_native_readback_tool_known(readback) ||
           v5_native_readback_interpreter_idle_known(readback) ||
           v5_native_readback_interpreter_known(readback) ||
           v5_native_readback_current_line_known(readback) ||
           v5_native_readback_motion_line_known(readback) ||
           v5_native_readback_mdi_run_known(readback) ||
           v5_native_readback_all_homed_known(readback);
}

int v5_native_modal_tool_status_write_ex(
    const char *path,
    int valid,
    const char *modal_text,
    int tool_valid,
    int tool_number,
    int tool_length_valid,
    double tool_length_mm,
    int interpreter_idle_valid,
    int interpreter_idle,
    int interpreter_paused_valid,
    int interpreter_paused,
    int all_homed_valid,
    int all_homed,
    int current_line_valid,
    int current_line,
    int motion_line_valid,
    int motion_line,
    int mdi_run_valid,
    int mdi_run_active,
    int mdi_run_line,
    const char *mdi_run_command)
{
    FILE *fp;
    V5NativeModalToolStatusBlock block;
    const char *actual_path = modal_tool_status_path(path);
    size_t modal_len;
    size_t command_len;

    memset(&block, 0, sizeof(block));
    block.magic = V5_MODAL_TOOL_MAGIC;
    block.version = V5_MODAL_TOOL_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.valid = valid ? 1U : 0U;
    block.modal_valid = (valid && modal_text && modal_text[0]) ? 1U : 0U;
    block.tool_valid = (valid && tool_valid && tool_number >= 0) ? 1U : 0U;
    block.tool_number = block.tool_valid ? (int32_t)tool_number : -1;
    block.tool_length_valid = (block.tool_valid && tool_length_valid && isfinite(tool_length_mm)) ? 1U : 0U;
    block.tool_length_mm = block.tool_length_valid ? tool_length_mm : 0.0;
    block.interpreter_idle_valid = (valid && interpreter_idle_valid) ? 1U : 0U;
    block.interpreter_idle = (block.interpreter_idle_valid && interpreter_idle) ? 1U : 0U;
    block.interpreter_paused_valid = (valid && interpreter_paused_valid) ? 1U : 0U;
    block.interpreter_paused = (block.interpreter_paused_valid && interpreter_paused) ? 1U : 0U;
    block.all_homed_valid = (valid && all_homed_valid) ? 1U : 0U;
    block.all_homed = (block.all_homed_valid && all_homed) ? 1U : 0U;
    block.current_line_valid = (valid && current_line_valid) ? 1U : 0U;
    block.current_line = block.current_line_valid && current_line > 0 ? (int32_t)current_line : 0;
    block.motion_line_valid = (valid && motion_line_valid) ? 1U : 0U;
    block.motion_line = block.motion_line_valid && motion_line > 0 ? (int32_t)motion_line : 0;
    block.mdi_run_valid = (valid && mdi_run_valid) ? 1U : 0U;
    block.mdi_run_active = (block.mdi_run_valid && mdi_run_active) ? 1U : 0U;
    block.mdi_run_line = block.mdi_run_valid && mdi_run_line > 0 ? (int32_t)mdi_run_line : 0;
    if (block.modal_valid) {
        modal_len = strlen(modal_text);
        if (modal_len >= sizeof(block.modal_text)) {
            modal_len = sizeof(block.modal_text) - 1U;
        }
        memcpy(block.modal_text, modal_text, modal_len);
        block.modal_text[modal_len] = '\0';
    }
    if (block.mdi_run_valid && mdi_run_command && mdi_run_command[0]) {
        command_len = strlen(mdi_run_command);
        if (command_len >= sizeof(block.mdi_run_command)) {
            command_len = sizeof(block.mdi_run_command) - 1U;
        }
        memcpy(block.mdi_run_command, mdi_run_command, command_len);
        block.mdi_run_command[command_len] = '\0';
    }
    block.monotonic_ns = modal_tool_monotonic_ns();
    block.crc32 = modal_tool_crc32_like(&block);
    fp = fopen(actual_path, "wb");
    if (!fp) {
        return 0;
    }
    if (fwrite(&block, 1U, sizeof(block), fp) != sizeof(block)) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}
int v5_native_modal_tool_status_write(
    const char *path,
    int valid,
    const char *modal_text,
    int tool_valid,
    int tool_number,
    int tool_length_valid,
    double tool_length_mm)
{
    return v5_native_modal_tool_status_write_ex(
        path,
        valid,
        modal_text,
        tool_valid,
        tool_number,
        tool_length_valid,
        tool_length_mm,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0);
}
