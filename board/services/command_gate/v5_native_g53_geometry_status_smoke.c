#include "v5_native_g53_geometry_status.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define V5_G53_STATUS_WIRE_SIZE 152U
#define V5_G53_STATUS_ACTIVE_MASK_OFFSET 28U
#define V5_G53_STATUS_CRC_OFFSET 144U

static uint32_t smoke_crc32_like(const unsigned char *wire)
{
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0U; i < V5_G53_STATUS_CRC_OFFSET; ++i) {
        hash ^= (uint32_t)wire[i];
        hash *= 16777619u;
    }
    return hash;
}

static int smoke_read_wire(const char *path, unsigned char *wire)
{
    FILE *fp = fopen(path, "rb");
    int ok;
    if (!fp || !wire) {
        if (fp) fclose(fp);
        return 0;
    }
    ok = fread(wire, 1U, V5_G53_STATUS_WIRE_SIZE, fp) == V5_G53_STATUS_WIRE_SIZE;
    if (fgetc(fp) != EOF) {
        ok = 0;
    }
    fclose(fp);
    return ok;
}

static int smoke_write_wire(const char *path, const unsigned char *wire)
{
    FILE *fp = fopen(path, "wb");
    int ok;
    if (!fp || !wire) {
        if (fp) fclose(fp);
        return 0;
    }
    ok = fwrite(wire, 1U, V5_G53_STATUS_WIRE_SIZE, fp) == V5_G53_STATUS_WIRE_SIZE;
    return fclose(fp) == 0 && ok;
}

static uint32_t smoke_wire_u32(const unsigned char *wire, size_t offset)
{
    uint32_t value = 0U;
    memcpy(&value, wire + offset, sizeof(value));
    return value;
}

static int smoke_set_wire_u32(unsigned char *wire, size_t offset, uint32_t value)
{
    if (!wire || offset + sizeof(value) > V5_G53_STATUS_WIRE_SIZE) {
        return 0;
    }
    memcpy(wire + offset, &value, sizeof(value));
    return 1;
}

int main(int argc, char **argv)
{
    const char *path = "v5_native_g53_geometry_status_smoke.bin";
    V5NativeReadback readback;
    unsigned char wire[V5_G53_STATUS_WIRE_SIZE];
    const uint32_t ac_mask = V5_NATIVE_G53_GEOMETRY_FIELD_A_Y |
        V5_NATIVE_G53_GEOMETRY_FIELD_A_Z |
        V5_NATIVE_G53_GEOMETRY_FIELD_C_X |
        V5_NATIVE_G53_GEOMETRY_FIELD_C_Y;
    const uint32_t bc_mask = V5_NATIVE_G53_GEOMETRY_FIELD_B_X |
        V5_NATIVE_G53_GEOMETRY_FIELD_B_Z |
        V5_NATIVE_G53_GEOMETRY_FIELD_C_X |
        V5_NATIVE_G53_GEOMETRY_FIELD_C_Y;
    double centers[V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT][V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT] = {
        {1.0, 20.0, -50.0},
        {0.0, 2.0, -25.0},
        {50.0, 20.0, 3.0},
    };

    if (argc == 2) {
        v5_native_readback_init(&readback);
        if (!v5_native_g53_geometry_status_read(argv[1], 1000U, &readback) ||
            !v5_native_readback_g53_geometry_known(&readback) ||
            !v5_native_readback_motion_model_known(&readback)) {
            return 7;
        }
        printf(
            "v5 native g53 geometry status: path=%s model=%s epoch=%u\n",
            argv[1],
            readback.motion_model,
            readback.g53_geometry_epoch);
        return 0;
    }

    unlink(path);
    v5_native_readback_init(&readback);
    if (v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        v5_native_readback_g53_geometry_known(&readback)) {
        return 1;
    }
    if (!strstr(readback.unavailable_reason, "missing")) {
        return 2;
    }
    if (!v5_native_g53_geometry_status_write(
            path,
            1,
            &centers[0][0],
            V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT,
            V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT,
            19U,
            "XYZAC_TRT")) {
        return 3;
    }
    if (!smoke_read_wire(path, wire) ||
        smoke_wire_u32(wire, V5_G53_STATUS_ACTIVE_MASK_OFFSET) != ac_mask) {
        unlink(path);
        return 8;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        !v5_native_readback_g53_geometry_known(&readback) ||
        readback.g53_geometry_epoch != 19U ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][0] != 1.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][1] != 20.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_A][2] != -50.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][0] != 50.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][1] != 20.0 ||
        readback.g53_centers[V5_NATIVE_READBACK_G53_CENTER_C][2] != 3.0 ||
        !v5_native_readback_motion_model_known(&readback) ||
        strcmp(readback.motion_model, "XYZAC_TRT") != 0 ||
        v5_native_readback_g53_center(&readback, V5_NATIVE_READBACK_G53_CENTER_B)[2] != -25.0) {
        unlink(path);
        return 4;
    }
    if (!smoke_set_wire_u32(wire, V5_G53_STATUS_ACTIVE_MASK_OFFSET, bc_mask) ||
        !smoke_set_wire_u32(wire, V5_G53_STATUS_CRC_OFFSET, smoke_crc32_like(wire)) ||
        !smoke_write_wire(path, wire)) {
        unlink(path);
        return 9;
    }
    v5_native_readback_init(&readback);
    if (v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        v5_native_readback_g53_geometry_known(&readback) ||
        !strstr(readback.unavailable_reason, "invalid")) {
        unlink(path);
        return 10;
    }
    if (!v5_native_g53_geometry_status_write(
            path,
            1,
            &centers[0][0],
            V5_NATIVE_G53_GEOMETRY_STATUS_CENTER_COUNT,
            V5_NATIVE_G53_GEOMETRY_STATUS_AXIS_COUNT,
            20U,
            "XYZBC_TRT") ||
        !smoke_read_wire(path, wire) ||
        smoke_wire_u32(wire, V5_G53_STATUS_ACTIVE_MASK_OFFSET) != bc_mask) {
        unlink(path);
        return 11;
    }
    v5_native_readback_init(&readback);
    if (!v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        strcmp(readback.motion_model, "XYZBC_TRT") != 0 ||
        readback.g53_geometry_epoch != 20U) {
        unlink(path);
        return 12;
    }
    if (!v5_native_g53_geometry_status_write(path, 0, 0, 0, 0, 0U, 0)) {
        unlink(path);
        return 5;
    }
    v5_native_readback_init(&readback);
    if (v5_native_g53_geometry_status_read(path, 1000U, &readback) ||
        v5_native_readback_g53_geometry_known(&readback)) {
        unlink(path);
        return 6;
    }
    unlink(path);
    printf("v5 native g53 geometry status: AC/BC active_field_mask=registry-derived mismatch=fail_closed ABI=152/28/144\n");
    return 0;
}
