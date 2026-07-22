#include "v5_native_operator_error_status.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define V5_OPERATOR_ERROR_MAGIC 0x564F4552u
#define V5_OPERATOR_ERROR_VERSION 2u

typedef struct V5NativeOperatorErrorStatusBlock {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t valid;
    uint32_t kind;
    uint32_t display_mode;
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
#ifdef _WIN32
    return (uint64_t)GetTickCount64() * 1000000ULL;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
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
        block->generation == 0ULL ||
        block->display_mode < V5_NATIVE_OPERATOR_ERROR_DISPLAY_LOG_ONLY ||
        block->display_mode > V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ||
        block->crc32 != block_crc32_like(block)) {
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
    status->display_mode = block.display_mode;
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
    uint32_t display_mode,
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
    block.valid = valid &&
        display_mode >= V5_NATIVE_OPERATOR_ERROR_DISPLAY_LOG_ONLY &&
        display_mode <= V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP ? 1U : 0U;
    block.kind = block.valid ? kind : 0U;
    block.display_mode = block.valid ? display_mode : 0U;
    block.generation = block.valid ? generation : 0ULL;
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
    status->display_mode = V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP;
    copy_text(status->source_id, sizeof(status->source_id), "OWNER_ALIAS");
    copy_text(status->title_cn, sizeof(status->title_cn), title_cn);
    copy_text(status->reason_cn, sizeof(status->reason_cn), reason_cn);
    copy_text(status->next_cn, sizeof(status->next_cn), next_cn);
}

typedef struct V5NativeOperatorAliasPresentation {
    const char *code;
    const char *title_cn;
    const char *reason_cn;
    const char *next_cn;
} V5NativeOperatorAliasPresentation;

static const V5NativeOperatorAliasPresentation g_alias_presentations[] = {
    {"POWER_ON_HOME_REQUIRED", "需要回零", "当前动作要求先完成本次开机机械全轴回零", "关闭提示，选择机械全轴并完成回零，再重新执行当前动作"},
    {"WORK_ZERO_NOT_HOMED", "需要回零", "加工坐标归零前必须先完成本次开机机械全轴回零", "选择机械全轴并完成回零，再重新执行加工坐标归零"},
    {"NOT_HOMED", "需要回零", "当前动作要求相关轴已经完成回零", "完成机械全轴回零后重新执行当前动作"},
    {"UNHOMED", "需要回零", "当前动作要求相关轴已经完成回零", "完成机械全轴回零后重新执行当前动作"},
    {"POWER_ON_HOME_STATUS_UNAVAILABLE", "回零状态不可用", "当前无法读取本次开机回零状态", "关闭提示，等待状态恢复并完成机械全轴回零后再操作"},
    {"HOME_STATUS_UNAVAILABLE", "回零状态不可用", "当前无法读取回零状态", "等待状态恢复后重新执行回零"},
    {"ESTOP", "机器状态不允许动作", "机器当前处于急停状态", "先排除现场风险，取消急停并确认机器已使能，再重新操作"},
    {"HOME_PRECONDITION_ESTOP", "请先取消急停", "急停正在生效，控制系统未启动任何轴回零运动", "确认现场安全，点击右下角“取消急停”并确认机器使能后重新回零"},
    {"DISABLED", "机器状态不允许动作", "机器当前未上使能", "先排除现场风险，取消急停并确认机器已使能，再重新操作"},
    {"HOME_PRECONDITION_DISABLED", "机器状态不允许回零", "机器尚未使能，控制系统未启动任何轴回零运动", "确认已取消急停且机器使能成功，再重新执行机械全轴回零"},
    {"HOME_PRECONDITION_MOVING", "机器正在运动", "提交回零时仍有轴在运动，新的回零事务未开始", "先停止当前运动并等待所有轴静止，再重新执行机械全轴回零"},
    {"HOME_PRECONDITION_SAFETY_UNAVAILABLE", "安全状态不可用", "未取得有效的急停与机器使能状态，不能安全开始回零", "保持机器停止，恢复安全状态回读后确认急停和使能状态，再重新回零"},
    {"HOME_PRECONDITION_RTCP_STATUS_UNAVAILABLE", "RTCP状态不可用", "未取得本次回零所需的有效RTCP实际状态，不能安全开始运动", "保持机器停止，恢复RTCP实际状态回读并确认其已关闭后重新回零"},
    {"HOME_PRECONDITION_FAILED", "回零前置条件不满足", "回零前的安全或运动状态检查未通过，未开始轴运动", "确认已取消急停、机器已使能且所有轴静止后重新回零"},
    {"ALL_HOME_NATIVE_CONFIG_INVALID", "回零配置错误", "本次全轴回零的轴配置缺失或无效，未开始运动", "保持机器停止，检查轴参数、回零参数和启用轴后保存并重启，再重新回零"},
    {"ALL_HOME_AXIS_X_CONFIG_INVALID", "X轴回零配置无效", "X轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查X轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_AXIS_Y_CONFIG_INVALID", "Y轴回零配置无效", "Y轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查Y轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_AXIS_Z_CONFIG_INVALID", "Z轴回零配置无效", "Z轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查Z轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_AXIS_A_CONFIG_INVALID", "A轴回零配置无效", "A轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查A轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_AXIS_B_CONFIG_INVALID", "B轴回零配置无效", "B轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查B轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_AXIS_C_CONFIG_INVALID", "C轴回零配置无效", "C轴回零配置缺失或无效，本次全轴回零未开始运动", "保持机器停止，检查C轴从站、比例和零位参数，保存并彻底重启后重新回零"},
    {"ALL_HOME_COUNT_READBACK_INVALID", "回零反馈不可用", "未取得本次回零所需的有效驱动计数回读，不能计算或确认机械零位", "检查驱动在线状态、编码器反馈和计数映射，读取驱动正常后重新回零"},
    {"ALL_HOME_LIMIT_ACTIVE", "轴限位阻止回零", "目标轴的限位输入正在生效，回零运动已被阻止", "检查该轴软限位、硬限位开关和实际位置，排除限位后重新回零"},
    {"HOME_CANCELLED", "回零已取消", "本次回零已被操作员急停取消", "确认现场安全后取消急停，再重新执行机械全轴回零"},
    {"HOME_CANCELLED_BY_ESTOP", "回零已取消", "操作员按下急停，本次单轴定位已立即取消", "确认现场安全后取消急停，再重新执行所需回零动作"},
    {"ALL_HOME_DRIVE_FAULT", "驱动故障阻止回零", "目标轴驱动器报告故障，本次回零已停止", "查看具体故障轴，排除并清除驱动报警，读取驱动正常后重新回零"},
    {"ALL_HOME_RTCP_FORCE_OFF_NOT_CONFIRMED", "RTCP状态阻止回零", "回零前RTCP状态未能切换为关闭，未启动任何轴运动", "保持机器停止，确认RTCP实际状态为关闭后重新回零"},
    {"HOME_RTCP_FORCE_OFF_NOT_CONFIRMED", "RTCP状态阻止回零", "回零前RTCP状态未能切换为关闭，未启动任何轴运动", "保持机器停止，确认RTCP实际状态为关闭后重新回零"},
    {"ALL_HOME_AXIS_SLAVE_MAPPING_INVALID", "运动模型与从站映射不一致", "当前运动模型的生效逻辑轴与轴参数中的从站映射缺失、重复或不一致，运动保持禁用", "进入设置页，在轴参数的从站下拉中逐轴选择与当前运动模型一致的从站，保存并重启后再使能、回零或启动"},
    {"ALL_HOME_MOTION_NOT_STARTED", "回零运动未启动", "完整回零位移已提交，但目标轴实际计数没有变化，未证明运动已启动", "检查机器使能、驱动状态、限位和轴映射，确认轴可运动后重新回零"},
    {"ALL_HOME_MOTION_NOT_COMPLETE", "回零运动未完成", "已经观察到轴运动，但未取得本次运动完成并稳定静止的有效证明", "保持机器停止，检查驱动反馈和运动状态；确认轴已静止后重新回零"},
    {"ALL_HOME_SEQUENCE_NOT_STARTED", "回零次序未启动", "控制系统未进入任何已登记的回零次序，未启动目标轴回零", "保持机器停止，由维护人员检查启用轴和回零次序配置后重新回零"},
    {"ALL_HOME_DIRECT_SINGLE_JOINT_UNSUPPORTED", "回零入口不正确", "当前请求不是允许的机械全轴回零方式，控制系统已拒绝执行", "关闭提示并从主页面选择机械全轴后重新回零；若再次出现请联系维护人员"},
    {"ALL_HOME_ZERO_DELTA_MOVEMENT_UNPROVEN", "回零运动未证明", "目标轴位移为零且没有本次真实运动证明，不能把旧零位当作回零成功", "先将该轴安全点动离开机械零位，再重新执行机械全轴回零"},
    {"ALL_HOME_PLAN_STALE", "回零执行计划已失效", "RTCP关闭后登记的轴位置或坐标窗口在运动前发生变化，控制系统已拒绝使用旧计划", "保持机器停止，确认轴静止且坐标窗口回读稳定后重新回零；若再次出现请联系维护人员"},
    {"ALL_HOME_NATIVE_FAILED", "回零失败", "控制系统报告本次全轴回零失败，但失败类别不在当前登记表中", "保持机器停止并联系维护人员核对本次回零结果后再重试"},
    {"HOME_FAILED", "回零失败", "本次回零返回失败，但没有取得更具体的失败原因", "保持机器停止并联系维护人员核对本次回零结果后再重试"},
    {"AXIS_ZERO_REPORT_INVALID", "单轴回零请求无效", "本次单轴回机械零或加工零的请求内容无效，未开始运动", "重新选择目标轴和坐标域后只执行一次回零；仍失败时联系维护人员"},
    {"AXIS_ZERO_NOT_ATTEMPTED", "单轴回零未执行", "本次单轴回机械零或加工零尚未提交到控制系统", "重新选择目标轴和坐标域后只执行一次回零"},
    {"AXIS_ZERO_FAILED", "单轴回零失败", "本次单轴回机械零或加工零返回失败，但没有取得更具体原因", "保持机器停止并联系维护人员核对本次单轴回零结果后再重试"},
    {"AXIS_ZERO_REQUEST_INVALID", "单轴回零请求无效", "目标轴或坐标域请求无效，控制系统未开始单轴运动", "重新选择一个有效轴及机械坐标或加工坐标后再回零"},
    {"AXIS_ZERO_POSITION_INVALID", "单轴回零目标无效", "单轴回零请求没有提供有效目标轴", "重新选择X、Y、Z、A、B或C轴后再回零"},
    {"AXIS_ZERO_POSITION_MODE_INVALID", "单轴回零坐标域无效", "单轴回零请求没有提供有效的机械坐标或加工坐标域", "重新选择机械坐标或加工坐标后再回零"},
    {"AXIS_ZERO_PARAMETERS_UNAVAILABLE", "单轴参数不可用", "未取得目标轴的有效运行参数，不能计算或执行单轴回零", "保持机器停止，检查该轴参数并保存重启后重新回零"},
    {"AXIS_ZERO_START_POSITION_UNAVAILABLE", "单轴位置不可用", "未取得目标轴本次运动前的有效实际位置", "检查轴反馈和控制状态，位置恢复正常后重新回零"},
    {"AXIS_ZERO_WCS_READBACK_UNAVAILABLE", "加工坐标状态不可用", "未取得当前加工坐标偏移回读，不能安全执行回加工零", "保持机器停止，恢复加工坐标偏移回读后重新执行回加工零"},
    {"AXIS_ZERO_MDI_MODE_REJECTED", "单轴运动模式被拒绝", "控制系统未能进入本次单轴定位所需的运动模式", "停止当前程序或任务，确认机器可手动运动后重新回零"},
    {"AXIS_ZERO_PROOF_MOVE_NOT_CONFIRMED", "单轴证明运动失败", "目标轴的本次证明位移没有取得真实运动与到位确认", "检查驱动、限位、轴映射和反馈，确认轴可安全运动后重新回零"},
    {"AXIS_ZERO_ARRIVAL_NOT_CONFIRMED", "单轴到位未确认", "目标轴没有取得本次运动完成、静止并到达登记零位的有效证明", "保持机器停止，检查实际位置和反馈后重新执行该轴回零"},
    {"AXIS_ZERO_REAL_MOVE_NOT_CONFIRMED", "单轴运动未确认", "本次单轴回零没有取得实际位置变化证明，不能把原位置当作成功", "先确认目标轴可运动并检查反馈，再重新执行该轴回零"},
    {"AXIS_ZERO_WCS_OFFSET_CHANGED", "加工坐标偏移异常", "回加工零过程中当前加工坐标偏移发生变化，本次结果已拒绝", "保持机器停止，检查加工坐标偏移来源并恢复稳定后重新回加工零"},
    {"HOME_NATIVE_OWNER_FAILED", "回零控制状态异常", "回零控制状态初始化失败，不能确认本次动作", "保持机器停止并联系维护人员恢复控制状态后再回零"},
    {"HOME_WCHECKPOINT_AXIS_INVALID", "旋转轴选择无效", "当前目标不是已登记的旋转轴，无法读取坐标窗口", "重新选择有效旋转轴后执行回机械零"},
    {"HOME_WCHECKPOINT_READBACK_INVALID", "旋转轴窗口回读无效", "未取得旋转轴坐标窗口的有效代次与平移基准", "保持机器停止，恢复旋转轴窗口回读后重新回零"},
    {"HOME_WCHECKPOINT_RUNTIME_ACTUAL_INVALID", "旋转轴实际位置无效", "未取得可与坐标窗口绑定的旋转轴运行实际位置", "检查旋转轴反馈和坐标窗口状态，恢复有效回读后重新回零"},
    {"HOME_SAFE_ZERO_INPUT_INVALID", "旋转轴零位参数无效", "旋转轴等效机械零计算缺少有效轴参数或实际计数", "保持机器停止，检查旋转轴参数与反馈后重新回零"},
    {"HOME_SAFE_ZERO_RANGE_INVALID", "旋转轴零位范围无效", "旋转轴每圈计数或整数范围无效，不能计算等效机械零", "检查每单位计数和每圈整数范围，保存重启后重新回零"},
    {"HOME_SAFE_ZERO_TIE_AMBIGUOUS", "旋转轴目标不唯一", "旋转轴到两个等效机械零位的距离相同，无法唯一选择最近目标", "先将该旋转轴安全点动离开半圈等距位置，再重新回零"},
    {"HOME_SAFE_ZERO_GENERATION_MISMATCH", "旋转轴窗口已变化", "到位检查使用的坐标窗口代次与本次固定目标不一致", "保持机器停止，等待坐标窗口稳定后重新回零"},
    {"HOME_SAFE_ZERO_LOGICAL_TARGET_COUNT_MISMATCH", "旋转轴逻辑到位超差", "旋转轴最终逻辑实际计数没有到达本次固定机械零目标", "检查旋转轴反馈、零点和计数参数，排除偏差后重新回零"},
    {"HOME_SAFE_ZERO_PHASE_COUNT_MISMATCH", "旋转轴等效零位超差", "旋转轴最终等效角计数没有落入机械零允许范围", "检查旋转轴每圈计数和零点模型，排除偏差后重新回零"},
    {"HOME_SAFE_ZERO_REMAP_READBACK_INVALID", "旋转轴重映射回读无效", "坐标窗口变化后未取得有效的新平移基准，不能继续本次回零", "保持机器停止，恢复坐标窗口回读后重新回零"},
    {"HOME_SAFE_ZERO_REMAP_FAILED", "旋转轴目标重映射失败", "坐标窗口变化后无法按固定逻辑零位重发剩余运动", "保持机器停止，检查旋转轴窗口与反馈后重新回零"}
};

int v5_native_operator_error_status_from_alias(
    const char *alias_code,
    V5NativeOperatorErrorStatus *status)
{
    size_t index;
    if (!alias_code || !status) {
        return 0;
    }
    for (index = 0U; index < sizeof(g_alias_presentations) / sizeof(g_alias_presentations[0]); ++index) {
        if (strcmp(alias_code, g_alias_presentations[index].code) == 0) {
            set_alias(
                status,
                g_alias_presentations[index].title_cn,
                g_alias_presentations[index].reason_cn,
                g_alias_presentations[index].next_cn);
            return 1;
        }
    }
    set_alias(
        status,
        "回零失败",
        "后台未返回具体回零失败原因",
        "本次结果缺少已登记的具体失败字段；请保持机器停止并联系维护人员核对后再重试");
    copy_text(status->source_id, sizeof(status->source_id), "OWNER_UNKNOWN");
    return 0;
}
