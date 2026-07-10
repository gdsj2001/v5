#include "v5_native_operator_error_status.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V5_OPERATOR_ERROR_MAGIC 0x564F4552u
#define V5_OPERATOR_ERROR_VERSION 1u

typedef struct V5NativeOperatorErrorStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t kind;
    uint32_t reserved0;
    uint64_t generation;
    uint64_t monotonic_ns;
    char source_id[V5_NATIVE_OPERATOR_ERROR_SOURCE_ID_CAP];
    char fingerprint[V5_NATIVE_OPERATOR_ERROR_FINGERPRINT_CAP];
    char title_cn[V5_NATIVE_OPERATOR_ERROR_TITLE_CAP];
    char reason_cn[V5_NATIVE_OPERATOR_ERROR_REASON_CAP];
    char next_cn[V5_NATIVE_OPERATOR_ERROR_NEXT_CAP];
    uint32_t crc32;
    uint32_t reserved1;
} V5NativeOperatorErrorStatusBlock;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static uint32_t block_crc32_like(const V5NativeOperatorErrorStatusBlock *block)
{
    const unsigned char *bytes = (const unsigned char *)block;
    const size_t limit = offsetof(V5NativeOperatorErrorStatusBlock, crc32);
    uint32_t hash = 2166136261u;
    size_t index;
    for (index = 0U; index < limit; ++index) {
        hash ^= (uint32_t)bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

static void copy_text(char *target, size_t capacity, const char *source)
{
    size_t length;
    if (!target || capacity == 0U) {
        return;
    }
    target[0] = '\0';
    if (!source || !source[0]) {
        return;
    }
    length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1U;
    }
    memcpy(target, source, length);
    target[length] = '\0';
}

static int block_is_fresh(const V5NativeOperatorErrorStatusBlock *block, unsigned int max_age_ms)
{
    uint64_t now;
    uint64_t max_age_ns;
    if (!block || block->magic != V5_OPERATOR_ERROR_MAGIC ||
        block->version != V5_OPERATOR_ERROR_VERSION ||
        block->size != (uint32_t)sizeof(*block) || !block->valid ||
        block->generation == 0ULL || block->crc32 != block_crc32_like(block)) {
        return 0;
    }
    now = monotonic_ns();
    if (!now || !block->monotonic_ns || now < block->monotonic_ns) {
        return 0;
    }
    max_age_ns = (uint64_t)(max_age_ms ? max_age_ms :
        V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_MAX_AGE_MS) * 1000000ULL;
    return now - block->monotonic_ns <= max_age_ns;
}

void v5_native_operator_error_status_init(V5NativeOperatorErrorStatus *status)
{
    if (status) {
        memset(status, 0, sizeof(*status));
    }
}

int v5_native_operator_error_status_read(
    const char *path,
    unsigned int max_age_ms,
    V5NativeOperatorErrorStatus *status)
{
    V5NativeOperatorErrorStatusBlock block;
    const char *actual_path = (path && path[0]) ? path :
        V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_PATH;
    FILE *stream;
    if (!status) {
        return 0;
    }
    v5_native_operator_error_status_init(status);
    stream = fopen(actual_path, "rb");
    if (!stream) {
        return 0;
    }
    if (fread(&block, 1U, sizeof(block), stream) != sizeof(block)) {
        fclose(stream);
        return 0;
    }
    fclose(stream);
    if (!block_is_fresh(&block, max_age_ms)) {
        return 0;
    }
    block.source_id[sizeof(block.source_id) - 1U] = '\0';
    block.fingerprint[sizeof(block.fingerprint) - 1U] = '\0';
    block.title_cn[sizeof(block.title_cn) - 1U] = '\0';
    block.reason_cn[sizeof(block.reason_cn) - 1U] = '\0';
    block.next_cn[sizeof(block.next_cn) - 1U] = '\0';
    if (!block.title_cn[0] || !block.reason_cn[0] || !block.next_cn[0]) {
        return 0;
    }
    status->generation = block.generation;
    status->kind = block.kind;
    copy_text(status->source_id, sizeof(status->source_id), block.source_id);
    copy_text(status->fingerprint, sizeof(status->fingerprint), block.fingerprint);
    copy_text(status->title_cn, sizeof(status->title_cn), block.title_cn);
    copy_text(status->reason_cn, sizeof(status->reason_cn), block.reason_cn);
    copy_text(status->next_cn, sizeof(status->next_cn), block.next_cn);
    return 1;
}

int v5_native_operator_error_status_write(
    const char *path,
    int valid,
    uint32_t kind,
    uint64_t generation,
    const char *source_id,
    const char *fingerprint,
    const char *title_cn,
    const char *reason_cn,
    const char *next_cn)
{
    V5NativeOperatorErrorStatusBlock block;
    const char *actual_path = (path && path[0]) ? path :
        V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_PATH;
    FILE *stream;
    memset(&block, 0, sizeof(block));
    block.magic = V5_OPERATOR_ERROR_MAGIC;
    block.version = V5_OPERATOR_ERROR_VERSION;
    block.size = (uint32_t)sizeof(block);
    block.valid = valid ? 1U : 0U;
    block.kind = valid ? kind : 0U;
    block.generation = valid ? generation : 0ULL;
    block.monotonic_ns = monotonic_ns();
    copy_text(block.source_id, sizeof(block.source_id), source_id);
    copy_text(block.fingerprint, sizeof(block.fingerprint), fingerprint);
    copy_text(block.title_cn, sizeof(block.title_cn), title_cn);
    copy_text(block.reason_cn, sizeof(block.reason_cn), reason_cn);
    copy_text(block.next_cn, sizeof(block.next_cn), next_cn);
    block.crc32 = block_crc32_like(&block);
    stream = fopen(actual_path, "wb");
    if (!stream) {
        return 0;
    }
    if (fwrite(&block, 1U, sizeof(block), stream) != sizeof(block)) {
        fclose(stream);
        return 0;
    }
    return fclose(stream) == 0;
}

static void set_alias(
    V5NativeOperatorErrorStatus *status,
    const char *title_cn,
    const char *reason_cn,
    const char *next_cn)
{
    v5_native_operator_error_status_init(status);
    copy_text(status->source_id, sizeof(status->source_id), "OWNER_ALIAS");
    copy_text(status->title_cn, sizeof(status->title_cn), title_cn);
    copy_text(status->reason_cn, sizeof(status->reason_cn), reason_cn);
    copy_text(status->next_cn, sizeof(status->next_cn), next_cn);
}

int v5_native_operator_error_status_from_alias(
    const char *alias_code,
    V5NativeOperatorErrorStatus *status)
{
    if (!alias_code || !status) {
        return 0;
    }
    if (strcmp(alias_code, "POWER_ON_HOME_REQUIRED") == 0 ||
        strcmp(alias_code, "NOT_HOMED") == 0 || strcmp(alias_code, "UNHOMED") == 0) {
        set_alias(
            status,
            "需要回零",
            "当前动作要求先完成本次开机机械全轴回零",
            "关闭提示，选择机械全轴并完成回零，再重新执行当前动作");
        return 1;
    }
    if (strcmp(alias_code, "POWER_ON_HOME_STATUS_UNAVAILABLE") == 0 ||
        strcmp(alias_code, "HOME_STATUS_UNAVAILABLE") == 0) {
        set_alias(
            status,
            "回零状态不可用",
            "当前无法读取本次开机回零状态",
            "关闭提示，等待状态恢复并完成机械全轴回零后再操作");
        return 1;
    }
    if (strcmp(alias_code, "ESTOP") == 0 ||
        strcmp(alias_code, "HOME_PRECONDITION_ESTOP") == 0) {
        set_alias(
            status,
            "机器状态不允许动作",
            "机器当前处于急停状态",
            "先排除现场风险，取消急停并确认机器已使能，再重新操作");
        return 1;
    }
    if (strcmp(alias_code, "DISABLED") == 0 ||
        strcmp(alias_code, "HOME_PRECONDITION_DISABLED") == 0) {
        set_alias(
            status,
            "机器状态不允许动作",
            "机器当前未上使能",
            "先排除现场风险，取消急停并确认机器已使能，再重新操作");
        return 1;
    }
    return 0;
}
