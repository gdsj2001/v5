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
} V5SettingsApplyResult;

int v5_settings_apply_prepare(
    const V5SettingsApplyRequest *request,
    V5SettingsApplyResult *result);

#ifdef __cplusplus
}
#endif

#endif
