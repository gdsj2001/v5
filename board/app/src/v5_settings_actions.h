#ifndef V5_SETTINGS_ACTIONS_H
#define V5_SETTINGS_ACTIONS_H

#include "v5_main_page_actions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct V5SettingsActionResult {
    int started;
    int supported;
    const char *name;
    const char *daemon_action;
    const char *owner;
    const char *result_path;
    const char *message;
} V5SettingsActionResult;

typedef struct V5SettingsActionStatus {
    int available;
    int busy;
    int ok;
    char action[64];
    char run_id[64];
    char code[72];
    char message[768];
    char vps_distribution_id[16];
    char result_path[192];
    char axis[16];
    char state[24];
    int cancel_allowed;
    int restart_required;
    int restart_deferred;
    int backend_restart_required;
} V5SettingsActionStatus;

int v5_settings_action_start(V5MainPageActionKind action, V5SettingsActionResult *result);
int v5_settings_axis_zero_start(const char *axis,
                                const char *driver_mode,
                                const char *target_scope,
                                const char *apply_mode,
                                const char *slave_index,
                                const char *home_offset,
                                V5SettingsActionResult *result);
int v5_settings_action_poll_status(V5SettingsActionStatus *status);
int v5_settings_action_cancel(const char *run_id);
int v5_settings_action_restart_commit(const char *run_id);

#ifdef __cplusplus
}
#endif

#endif
