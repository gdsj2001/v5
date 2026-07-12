#ifndef V5_UI_SHELL_PROGRAM_DELETE_H
#define V5_UI_SHELL_PROGRAM_DELETE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void shell_create_program_delete_popup(lv_obj_t *screen);
void shell_program_delete_cb(lv_event_t *event);
int shell_program_delete_request_selected(void);
int shell_program_delete_popup_visible(void);
const char *shell_program_delete_popup_text(void);
lv_obj_t *shell_program_delete_popup_confirm_button(void);
lv_obj_t *shell_program_delete_popup_close_button(void);

#ifdef __cplusplus
}
#endif

#endif
