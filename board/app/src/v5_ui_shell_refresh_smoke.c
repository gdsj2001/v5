#define V5_UI_SHELL_REFRESH_CLASSIFIER_ONLY
#include "v5_ui_shell_internal.h"

#define V5_MAIN_PAGE_REFRESH_DYNAMIC (1U << 0)
#define V5_MAIN_PAGE_REFRESH_POSE (1U << 4)

#include <stdio.h>

int main(void)
{
    unsigned int flags;

    flags = shell_refresh_classify_changes(1, 0, 0, 1);
    if (flags != V5_MAIN_PAGE_REFRESH_DYNAMIC) {
        return 1;
    }
    flags = shell_refresh_classify_changes(1, 1, 0, 1);
    if (flags != (V5_MAIN_PAGE_REFRESH_DYNAMIC | V5_MAIN_PAGE_REFRESH_POSE)) {
        return 2;
    }
    flags = shell_refresh_classify_changes(0, 0, 1, 1);
    if (flags != V5_MAIN_PAGE_REFRESH_POSE) {
        return 3;
    }
    flags = shell_refresh_classify_changes(1, 1, 1, 0);
    if (flags != V5_MAIN_PAGE_REFRESH_DYNAMIC) {
        return 4;
    }
    printf("V5_UI_SHELL_REFRESH_SMOKE_OK\n");
    return 0;
}
