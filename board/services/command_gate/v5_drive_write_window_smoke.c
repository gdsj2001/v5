#include "v5_drive_write_window.h"

#include <stdio.h>
#include <string.h>

typedef struct FakeDriveWindow {
    int read_ok;
    int estop_known;
    int estop_active;
    int machine_known;
    int machine_enabled;
    int off_ok;
    int on_ok;
    int off_calls;
    int on_calls;
} FakeDriveWindow;

static int fake_read(void *context, V5DriveWriteSafetyActual *actual)
{
    FakeDriveWindow *fake = (FakeDriveWindow *)context;
    if (!fake || !actual || !fake->read_ok) {
        return 0;
    }
    actual->safety_estop_known = fake->estop_known;
    actual->safety_estop_active = fake->estop_active;
    actual->machine_enable_known = fake->machine_known;
    actual->machine_enabled = fake->machine_enabled;
    return 1;
}

static int fake_off(void *context)
{
    FakeDriveWindow *fake = (FakeDriveWindow *)context;
    ++fake->off_calls;
    if (fake->off_ok) {
        fake->machine_enabled = 0;
    }
    return fake->off_ok;
}

static int fake_on(void *context)
{
    FakeDriveWindow *fake = (FakeDriveWindow *)context;
    ++fake->on_calls;
    if (fake->on_ok && !fake->estop_active) {
        fake->machine_enabled = 1;
    }
    return fake->on_ok;
}

static void init_fake(FakeDriveWindow *fake, int machine_enabled)
{
    memset(fake, 0, sizeof(*fake));
    fake->read_ok = 1;
    fake->estop_known = 1;
    fake->machine_known = 1;
    fake->machine_enabled = machine_enabled;
    fake->off_ok = 1;
    fake->on_ok = 1;
}

int main(void)
{
    FakeDriveWindow fake;
    V5DriveWriteWindowOps ops;
    V5DriveWriteWindowResult result;
    char owner_code[64];

    init_fake(&fake, 1);
    ops.context = &fake;
    ops.read_safety = fake_read;
    ops.set_machine_off = fake_off;
    ops.set_machine_on = fake_on;
    v5_drive_write_window_reset_for_test();

    if (!v5_drive_write_window_begin("settings:run-1", &ops, &result) ||
        !result.ok || !result.initial_machine_enabled ||
        result.final_machine_enabled || fake.off_calls != 1 ||
        !v5_drive_write_window_is_active() ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_START) ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_HOME) ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_JOG_CONTINUOUS) ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_ESTOP_RESET) ||
        v5_drive_write_window_blocks_kind(V5_COMMAND_JOG_STOP) ||
        v5_drive_write_window_blocks_kind(V5_COMMAND_ESTOP_FORCE)) {
        return 1;
    }
    if (!v5_drive_write_window_check_owner(
            "settings:run-1", owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "DRIVE_WRITE_WINDOW_OWNER_OK") != 0 ||
        v5_drive_write_window_check_owner(
            "settings:run-2", owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "DRIVE_WRITE_WINDOW_OWNER_MISMATCH") != 0) {
        return 14;
    }
    if (v5_drive_write_window_begin("settings:run-2", &ops, &result) ||
        strcmp(result.code, "DRIVE_WRITE_WINDOW_BUSY") != 0) {
        return 2;
    }
    if (!v5_drive_write_window_finish("settings:run-1", 0, &ops, &result) ||
        !result.ok || result.final_machine_enabled || fake.on_calls != 0 ||
        v5_drive_write_window_blocks_kind(V5_COMMAND_START) ||
        v5_drive_write_window_is_active()) {
        return 3;
    }
    if (v5_drive_write_window_check_owner(
            "settings:run-1", owner_code, sizeof(owner_code)) ||
        strcmp(owner_code, "DRIVE_WRITE_WINDOW_NOT_ACTIVE") != 0) {
        return 15;
    }

    init_fake(&fake, 0);
    if (!v5_drive_write_window_begin("run-3", &ops, &result) ||
        result.initial_machine_enabled || fake.off_calls != 0 ||
        !v5_drive_write_window_finish("run-3", 1, &ops, &result) ||
        result.final_machine_enabled || fake.on_calls != 0) {
        return 4;
    }

    init_fake(&fake, 1);
    if (!v5_drive_write_window_begin("run-4", &ops, &result) ||
        !v5_drive_write_window_finish("run-4", 1, &ops, &result) ||
        !result.final_machine_enabled || fake.off_calls != 1 || fake.on_calls != 1) {
        return 5;
    }

    init_fake(&fake, 1);
    if (!v5_drive_write_window_begin("run-5", &ops, &result) ||
        !v5_drive_write_window_abort("run-5", &ops, &result) ||
        result.final_machine_enabled || fake.on_calls != 0 ||
        strcmp(result.code, "DRIVE_WRITE_WINDOW_ABORT_OK") != 0) {
        return 6;
    }

    init_fake(&fake, 1);
    if (!v5_drive_write_window_abort("run-5", &ops, &result) ||
        !result.ok || result.final_machine_enabled || fake.off_calls != 1 ||
        strcmp(result.code, "DRIVE_WRITE_WINDOW_ABORT_NOT_ACTIVE") != 0) {
        return 7;
    }

    init_fake(&fake, 1);
    if (!v5_drive_write_window_begin("run-6", &ops, &result)) {
        return 8;
    }
    fake.on_ok = 0;
    if (v5_drive_write_window_finish("run-6", 1, &ops, &result) ||
        result.final_machine_enabled ||
        strcmp(result.code, "DRIVE_WRITE_MACHINE_ON_NOT_CONFIRMED") != 0) {
        return 9;
    }

    init_fake(&fake, 1);
    if (!v5_drive_write_window_begin("run-7", &ops, &result)) {
        return 10;
    }
    if (v5_drive_write_window_abort("other-run", &ops, &result) ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_START)) {
        return 11;
    }
    fake.read_ok = 0;
    if (v5_drive_write_window_finish("run-7", 0, &ops, &result) ||
        !v5_drive_write_window_blocks_kind(V5_COMMAND_HOME)) {
        return 12;
    }
    fake.read_ok = 1;
    fake.machine_enabled = 0;
    if (!v5_drive_write_window_abort("run-7", &ops, &result) ||
        v5_drive_write_window_blocks_kind(V5_COMMAND_HOME)) {
        return 13;
    }

    printf("v5 drive write window smoke passed\n");
    return 0;
}
