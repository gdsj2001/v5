#include "v5_linuxcncrsh_client.h"
#include "v5_linuxcncrsh_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static int v5_linuxcncrsh_axis_index(char axis, unsigned int *index_out)
{
    const char *axes = "XYZABC";
    const char *match = strchr(axes, toupper((unsigned char)axis));
    if (!match || !index_out) {
        return 0;
    }
    *index_out = (unsigned int)(match - axes);
    return 1;
}

int v5_linuxcncrsh_format_axis_position_query(
    char axis,
    int relative,
    char *out,
    size_t out_size)
{
    unsigned int axis_index;
    int rc;
    if (!out || out_size == 0U || !v5_linuxcncrsh_axis_index(axis, &axis_index)) {
        return 0;
    }
    rc = snprintf(
        out,
        out_size,
        "Get %s %u",
        relative ? "REL_ACT_POS" : "ABS_ACT_POS",
        axis_index);
    return v5_linuxcncrsh_format_ok(rc, out_size);
}

#ifndef _WIN32
static int v5_linuxcncrsh_parse_axis_position(
    const char *response,
    const char *key,
    unsigned int expected_axis_index,
    double *position_out)
{
    const char *match = 0;
    const char *scan;
    unsigned int response_axis_index;
    double value;
    if (!response || !key || !position_out) {
        return 0;
    }
    scan = response;
    while ((scan = strstr(scan, key)) != 0) {
        match = scan;
        scan += strlen(key);
    }
    if (!match) {
        return 0;
    }
    scan = match + strlen(key);
    while (*scan && isspace((unsigned char)*scan)) {
        ++scan;
    }
    if (sscanf(scan, "%u %lf", &response_axis_index, &value) != 2 ||
        response_axis_index != expected_axis_index || !isfinite(value)) {
        return 0;
    }
    *position_out = value;
    return 1;
}
#endif

int v5_linuxcncrsh_get_axis_position(
    const V5LinuxcncrshConfig *config,
    char axis,
    int relative,
    double *position_out)
{
#ifdef _WIN32
    (void)config;
    (void)axis;
    (void)relative;
    (void)position_out;
    return 0;
#else
    int fd;
    int rc;
    char command[64];
    char response[512];
    const char *key = relative ? "REL_ACT_POS" : "ABS_ACT_POS";
    unsigned int axis_index;
    axis = (char)toupper((unsigned char)axis);
    if (!position_out || !v5_linuxcncrsh_axis_index(axis, &axis_index)) {
        return 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }
    rc = v5_linuxcncrsh_format_axis_position_query(axis, relative, command, sizeof(command));
    if (!rc ||
        !v5_linuxcncrsh_send_request_text(fd, command, response, sizeof(response))) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    return v5_linuxcncrsh_parse_axis_position(response, key, axis_index, position_out);
#endif
}

int v5_linuxcncrsh_get_joint_position(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    double *position_out)
{
#ifdef _WIN32
    (void)config;
    (void)joint;
    (void)position_out;
    return 0;
#else
    int fd;
    int rc;
    char command[64];
    char response[512];
    const char *scan;
    unsigned int response_joint = 0U;
    double position = 0.0;
    if (!position_out) {
        return 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }
    rc = snprintf(command, sizeof(command), "Get Joint_Pos %u", joint);
    if (!v5_linuxcncrsh_format_ok(rc, sizeof(command)) ||
        !v5_linuxcncrsh_send_request_text(fd, command, response, sizeof(response))) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    scan = response;
    while ((scan = strstr(scan, "JOINT_POS")) != 0) {
        if (sscanf(scan, "JOINT_POS %u %lf", &response_joint, &position) == 2 &&
            response_joint == joint && isfinite(position)) {
            *position_out = position;
            return 1;
        }
        scan += strlen("JOINT_POS");
    }
    return 0;
#endif
}

int v5_linuxcncrsh_parse_joint_state_response(
    const char *response,
    unsigned int expected_joint,
    V5LinuxcncrshJointState *state_out)
{
    const char *scan;
    unsigned int response_joint = 0U;
    unsigned int heartbeat = 0U;
    int echo_serial = 0;
    double actual = 0.0;
    char state[16];
    if (state_out) {
        memset(state_out, 0, sizeof(*state_out));
    }
    if (!response || !state_out) {
        return 0;
    }
    scan = response;
    while ((scan = strstr(scan, "JOINT_STATE")) != 0) {
        if (sscanf(scan, "JOINT_STATE %u %lf %15s %u %d",
                   &response_joint, &actual, state, &heartbeat, &echo_serial) == 5 &&
            response_joint == expected_joint &&
            isfinite(actual) &&
            (strcasecmp(state, "YES") == 0 || strcasecmp(state, "NO") == 0)) {
            state_out->actual = actual;
            state_out->in_position = strcasecmp(state, "YES") == 0;
            state_out->heartbeat = heartbeat;
            state_out->echo_serial = echo_serial;
            return 1;
        }
        scan += strlen("JOINT_STATE");
    }
    return 0;
}

int v5_linuxcncrsh_get_joint_state(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    V5LinuxcncrshJointState *state_out)
{
#ifdef _WIN32
    (void)config;
    (void)joint;
    if (state_out) {
        memset(state_out, 0, sizeof(*state_out));
    }
    return 0;
#else
    int fd;
    int rc;
    char command[64];
    char response[512];
    if (state_out) {
        memset(state_out, 0, sizeof(*state_out));
    }
    if (!state_out) {
        return 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }
    rc = snprintf(command, sizeof(command), "Get Joint_State %u", joint);
    if (!v5_linuxcncrsh_format_ok(rc, sizeof(command)) ||
        !v5_linuxcncrsh_send_request_text(fd, command, response, sizeof(response))) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    return v5_linuxcncrsh_parse_joint_state_response(response, joint, state_out);
#endif
}

#ifndef _WIN32
static int v5_linuxcncrsh_parse_joint_homed(
    const char *response,
    unsigned int expected_joint,
    int *homed_out)
{
    const char *scan;
    int joint = -1;
    char state[16];
    if (homed_out) {
        *homed_out = 0;
    }
    if (!response) {
        return 0;
    }
    scan = response;
    while ((scan = strstr(scan, "JOINT_HOMED")) != 0) {
        if (sscanf(scan, "JOINT_HOMED %d %15s", &joint, state) == 2 &&
            joint == (int)expected_joint &&
            (strcasecmp(state, "YES") == 0 || strcasecmp(state, "NO") == 0)) {
            if (homed_out) {
                *homed_out = strcasecmp(state, "YES") == 0;
            }
            return 1;
        }
        scan += strlen("JOINT_HOMED");
    }
    return 0;
}
#endif

int v5_linuxcncrsh_get_joint_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int joint,
    int *homed_out)
{
#ifdef _WIN32
    (void)config;
    (void)joint;
    if (homed_out) {
        *homed_out = 0;
    }
    return 0;
#else
    int fd;
    int rc;
    char command[64];
    char response[512];
    if (homed_out) {
        *homed_out = 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }
    rc = snprintf(command, sizeof(command), "Get Joint_Homed %u", joint);
    if (!v5_linuxcncrsh_format_ok(rc, sizeof(command)) ||
        !v5_linuxcncrsh_send_request_text(fd, command, response, sizeof(response))) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    return v5_linuxcncrsh_parse_joint_homed(response, joint, homed_out);
#endif
}

int v5_linuxcncrsh_get_teleop_enabled(
    const V5LinuxcncrshConfig *config,
    int *enabled_out)
{
#ifdef _WIN32
    (void)config;
    if (enabled_out) {
        *enabled_out = 0;
    }
    return 0;
#else
    int fd;
    char response[512];
    const char *scan;
    char state[16];
    if (enabled_out) {
        *enabled_out = 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0 ||
        !v5_linuxcncrsh_send_request_text(fd, "Get Teleop_Enable", response, sizeof(response))) {
        v5_linuxcncrsh_gate_close();
        return 0;
    }
    scan = response;
    while ((scan = strstr(scan, "TELEOP_ENABLE")) != 0) {
        if (sscanf(scan, "TELEOP_ENABLE %15s", state) == 1 &&
            (strcasecmp(state, "YES") == 0 || strcasecmp(state, "NO") == 0)) {
            if (enabled_out) {
                *enabled_out = strcasecmp(state, "YES") == 0;
            }
            return 1;
        }
        scan += strlen("TELEOP_ENABLE");
    }
    return 0;
#endif
}

int v5_linuxcncrsh_get_all_homed(
    const V5LinuxcncrshConfig *config,
    unsigned int expected_joint_count,
    int *all_homed_out)
{
#ifdef _WIN32
    (void)config;
    (void)expected_joint_count;
    if (all_homed_out) {
        *all_homed_out = 0;
    }
    return 0;
#else
    int fd;
    char command[64];
    char response[512];
    unsigned int joint;
    int all_homed = 1;
    if (all_homed_out) {
        *all_homed_out = 0;
    }
    if (expected_joint_count == 0U) {
        return 0;
    }
    fd = v5_linuxcncrsh_gate_connect(config);
    if (fd < 0) {
        return 0;
    }
    for (joint = 0U; joint < expected_joint_count; ++joint) {
        int homed = 0;
        int rc = snprintf(command, sizeof(command), "Get Joint_Homed %u", joint);
        if (!v5_linuxcncrsh_format_ok(rc, sizeof(command)) ||
            !v5_linuxcncrsh_send_request_text(fd, command, response, sizeof(response)) ||
            !v5_linuxcncrsh_parse_joint_homed(response, joint, &homed)) {
            v5_linuxcncrsh_gate_close();
            return 0;
        }
        if (!homed) {
            all_homed = 0;
        }
    }
    if (all_homed_out) {
        *all_homed_out = all_homed;
    }
    return 1;
#endif
}
