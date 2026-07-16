#ifndef V5_SETTINGS_APPLY_H
#define V5_SETTINGS_APPLY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum V5SettingsApplyStatus {
    V5_SETTINGS_APPLY_REJECTED = 0,
    V5_SETTINGS_APPLY_ACCEPTED = 1,
} V5SettingsApplyStatus;

typedef struct V5SettingsApplyRequest {
    const char *field_name;
    const char *value_text;
    unsigned int owner_generation;
    unsigned int readback_token;
} V5SettingsApplyRequest;

typedef struct V5SettingsApplyResult {
    V5SettingsApplyStatus status;
    const char *write_owner;
    const char *readback_owner;
    int restart_required;
    int drive_only_allowed;
    int scale_chain_transaction_required;
    int raw_limits_recompute_required;
} V5SettingsApplyResult;

typedef struct V5SettingsApplyScaleChainResult {
    int attempted;
    int zero_model_present;
    int skipped;
    int scale_recomputed;
    int raw_limits_recomputed;
    char code[64];
    double effective_scale;
    double raw_zero_position;
    double raw_min_limit;
    double raw_max_limit;
} V5SettingsApplyScaleChainResult;

typedef struct V5SettingsApplyAxisCommitRequest {
    const char *project_root;
    const char *axis;
    unsigned int axis_index;
    const char *field_key;
    const char *field_name;
    const char *value_text;
    unsigned int owner_generation;
    unsigned int readback_token;
} V5SettingsApplyAxisCommitRequest;

typedef struct V5SettingsApplyAxisCommitResult {
    V5SettingsApplyResult apply;
    V5SettingsApplyScaleChainResult scale_chain;
    char readback_value[64];
    int owner_written;
    int source_readback_confirmed;
    int restart_pending;
} V5SettingsApplyAxisCommitResult;

int v5_settings_apply_prepare(
    const V5SettingsApplyRequest *request,
    V5SettingsApplyResult *result);

int v5_settings_apply_commit_axis_value(
    const V5SettingsApplyAxisCommitRequest *request,
    V5SettingsApplyAxisCommitResult *result);

int v5_settings_apply_scale_chain_commit(
    const char *project_root,
    const char *settings_runtime_json_path,
    const char *axis,
    int joint_active,
    unsigned int status_slot,
    const char *field_name,
    V5SettingsApplyScaleChainResult *result);

#ifdef __cplusplus
}
#endif

#endif
