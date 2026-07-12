#ifndef V5_NATIVE_OPERATOR_ERROR_STATUS_H
#define V5_NATIVE_OPERATOR_ERROR_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_PATH "/dev/shm/v5_native_operator_error_status.bin"
#define V5_NATIVE_OPERATOR_ERROR_STATUS_DEFAULT_MAX_AGE_MS 10000U
#define V5_NATIVE_OPERATOR_ERROR_SOURCE_ID_CAP 64U
#define V5_NATIVE_OPERATOR_ERROR_FINGERPRINT_CAP 24U
#define V5_NATIVE_OPERATOR_ERROR_TITLE_CAP 96U
#define V5_NATIVE_OPERATOR_ERROR_REASON_CAP 384U
#define V5_NATIVE_OPERATOR_ERROR_NEXT_CAP 256U

typedef enum V5NativeOperatorErrorDisplayMode {
    V5_NATIVE_OPERATOR_ERROR_DISPLAY_LOG_ONLY = 1,
    V5_NATIVE_OPERATOR_ERROR_DISPLAY_TOP_STATUS = 2,
    V5_NATIVE_OPERATOR_ERROR_DISPLAY_POPUP = 3
} V5NativeOperatorErrorDisplayMode;

typedef struct V5NativeOperatorErrorStatus {
    uint64_t generation;
    uint32_t kind;
    uint32_t display_mode;
    char source_id[V5_NATIVE_OPERATOR_ERROR_SOURCE_ID_CAP];
    char fingerprint[V5_NATIVE_OPERATOR_ERROR_FINGERPRINT_CAP];
    char title_cn[V5_NATIVE_OPERATOR_ERROR_TITLE_CAP];
    char reason_cn[V5_NATIVE_OPERATOR_ERROR_REASON_CAP];
    char next_cn[V5_NATIVE_OPERATOR_ERROR_NEXT_CAP];
} V5NativeOperatorErrorStatus;

void v5_native_operator_error_status_init(V5NativeOperatorErrorStatus *status);
int v5_native_operator_error_status_read(
    const char *path,
    unsigned int max_age_ms,
    V5NativeOperatorErrorStatus *status);
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
    const char *next_cn);
int v5_native_operator_error_status_from_alias(
    const char *alias_code,
    V5NativeOperatorErrorStatus *status);

#ifdef __cplusplus
}
#endif

#endif
