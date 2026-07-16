#include "v5_native_g53_geometry_status.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("usage: v5_linuxcncrsh_golden_run --program /path/to/cc-ac.ngc\n");
    printf("       v5_linuxcncrsh_golden_run --print-active-model\n");
    printf("read-only diagnostic: validates that cc-ac.ngc or cc-bc.ngc matches fresh native active-model readback; it never opens or starts a program\n");
}

static const char *program_filename(const char *path)
{
    const char *slash;
    const char *backslash;
    if (!path) {
        return "";
    }
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash && backslash) {
        return (slash > backslash ? slash : backslash) + 1;
    }
    if (slash) {
        return slash + 1;
    }
    return backslash ? backslash + 1 : path;
}

static const char *golden_program_expected_model(const char *program_path)
{
    const char *name = program_filename(program_path);
    if (strcmp(name, "cc-ac.ngc") == 0) {
        return "XYZAC_TRT";
    }
    if (strcmp(name, "cc-bc.ngc") == 0) {
        return "XYZBC_TRT";
    }
    return 0;
}

static int read_active_motion_model(char *model, size_t model_cap)
{
    V5NativeReadback readback;
    if (!model || model_cap == 0U) {
        return 0;
    }
    model[0] = '\0';
    v5_native_readback_init(&readback);
    if (!v5_native_g53_geometry_status_read(
            V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_PATH,
            V5_NATIVE_G53_GEOMETRY_STATUS_DEFAULT_MAX_AGE_MS,
            &readback) ||
        !v5_native_readback_motion_model_known(&readback)) {
        return 0;
    }
    snprintf(model, model_cap, "%s", readback.motion_model);
    return 1;
}

static int golden_program_matches_active_model(const char *program_path, char *active_model, size_t active_model_cap)
{
    const char *expected_model = golden_program_expected_model(program_path);
    if (!expected_model) {
        fprintf(stderr, "unsupported golden program filename: %s\n", program_filename(program_path));
        return 0;
    }
    if (!read_active_motion_model(active_model, active_model_cap)) {
        fprintf(stderr, "fresh native active model readback unavailable\n");
        return 0;
    }
    if (strcmp(active_model, expected_model) != 0) {
        fprintf(stderr, "golden program model mismatch: program=%s expected=%s active=%s\n",
                program_filename(program_path), expected_model, active_model);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *program_path = 0;
    char active_model[V5_NATIVE_READBACK_MOTION_MODEL_CAP];
    int print_active_model = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
            program_path = argv[++i];
        } else if (strcmp(argv[i], "--print-active-model") == 0) {
            print_active_model = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (print_active_model) {
        if (program_path) {
            fprintf(stderr, "--print-active-model cannot be combined with --program\n");
            return 2;
        }
        if (!read_active_motion_model(active_model, sizeof(active_model))) {
            fprintf(stderr, "fresh native active model readback unavailable\n");
            return 11;
        }
        printf("%s\n", active_model);
        return 0;
    }
    if (!program_path || !program_path[0]) {
        fprintf(stderr, "--program is required\n");
        return 3;
    }
    if (!golden_program_matches_active_model(program_path, active_model, sizeof(active_model))) {
        return 12;
    }
    printf("native active model diagnostic passed: %s program=%s\n",
           active_model, program_filename(program_path));
    return 0;
}
