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
    const char *owner;
    const char *result_path;
    const char *message;
} V5SettingsActionResult;

int v5_settings_action_start(V5MainPageActionKind action, V5SettingsActionResult *result);

#ifdef __cplusplus
}
#endif

#endif
