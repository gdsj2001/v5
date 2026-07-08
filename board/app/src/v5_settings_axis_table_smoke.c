#include "v5_settings_axis_table.h"
#include "v5_command_gate_ipc.h"
#include "v5_settings_apply.h"
#include "v5_settings_parameter_store.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int same_text(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static int dropdown_has_line(const char *options, const char *line)
{
    const char *start;
    size_t line_len;
    if (!options || !line) return 0;
    line_len = strlen(line);
    start = options;
    while (*start) {
        const char *end = strchr(start, '\n');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (len == line_len && strncmp(start, line, len) == 0) {
            return 1;
        }
        if (!end) break;
        start = end + 1;
    }
    return 0;
}

static unsigned int g_axis_zero_callback_count;
static char g_axis_zero_axis[16];
static char g_axis_zero_mode[24];
static char g_axis_zero_scope[64];
static char g_axis_zero_apply[64];
static char g_axis_zero_slave[32];

static void axis_zero_smoke_cb(const char *axis, const char *driver_mode, const char *target_scope, const char *apply_mode, const char *slave_index, const char *home_offset, void *user_data)
{
    (void)home_offset;
    (void)user_data;
    ++g_axis_zero_callback_count;
    snprintf(g_axis_zero_axis, sizeof(g_axis_zero_axis), "%s", axis ? axis : "");
    snprintf(g_axis_zero_mode, sizeof(g_axis_zero_mode), "%s", driver_mode ? driver_mode : "");
    snprintf(g_axis_zero_scope, sizeof(g_axis_zero_scope), "%s", target_scope ? target_scope : "");
    snprintf(g_axis_zero_apply, sizeof(g_axis_zero_apply), "%s", apply_mode ? apply_mode : "");
    snprintf(g_axis_zero_slave, sizeof(g_axis_zero_slave), "%s", slave_index ? slave_index : "");
}

static pid_t g_settings_owner_pid = -1;

static void copy_cstr_smoke(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0U) return;
    dst[0] = '\0';
    if (src && src[0]) {
        snprintf(dst, cap, "%s", src);
    }
}

static int read_all_smoke(int fd, void *data, size_t size)
{
    char *p = (char *)data;
    while (size > 0U) {
        ssize_t n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static int write_all_smoke(int fd, const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size > 0U) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += (size_t)n;
        size -= (size_t)n;
    }
    return 1;
}

static void settings_owner_response_init(V5CommandGateIpcResponseFrame *response)
{
    memset(response, 0, sizeof(*response));
    response->magic = V5_COMMAND_GATE_IPC_MAGIC;
    response->version = V5_COMMAND_GATE_IPC_VERSION;
    response->size = (uint32_t)sizeof(*response);
    response->send_status = V5_COMMAND_GATE_SEND_UNAVAILABLE;
}

static void settings_owner_copy_result(V5CommandGateIpcResponseFrame *response, const V5SettingsApplyAxisCommitResult *result)
{
    response->settings_owner_written = result->owner_written ? 1 : 0;
    response->settings_source_readback_confirmed = result->source_readback_confirmed ? 1 : 0;
    response->settings_restart_pending = result->restart_pending ? 1 : 0;
    response->settings_scale_chain_attempted = result->scale_chain.attempted ? 1 : 0;
    response->settings_scale_recomputed = result->scale_chain.scale_recomputed ? 1 : 0;
    response->settings_raw_limits_recomputed = result->scale_chain.raw_limits_recomputed ? 1 : 0;
    response->settings_effective_scale = result->scale_chain.effective_scale;
    response->settings_raw_zero_position = result->scale_chain.raw_zero_position;
    response->settings_raw_min_limit = result->scale_chain.raw_min_limit;
    response->settings_raw_max_limit = result->scale_chain.raw_max_limit;
    copy_cstr_smoke(response->settings_readback_value, sizeof(response->settings_readback_value), result->readback_value);
    copy_cstr_smoke(response->settings_scale_chain_code, sizeof(response->settings_scale_chain_code), result->scale_chain.code);
}

static void settings_owner_handle_frame(const V5CommandGateIpcRequestFrame *frame, V5CommandGateIpcResponseFrame *response)
{
    V5SettingsApplyAxisCommitRequest request;
    V5SettingsApplyAxisCommitResult result;
    int ok;
    settings_owner_response_init(response);
    if (!frame || frame->magic != V5_COMMAND_GATE_IPC_MAGIC ||
        frame->version != V5_COMMAND_GATE_IPC_VERSION ||
        frame->size != (uint32_t)sizeof(*frame) ||
        frame->op != (uint32_t)V5_COMMAND_GATE_IPC_OP_SETTINGS_AXIS_COMMIT) {
        response->send_status = V5_COMMAND_GATE_SEND_INVALID;
        copy_cstr_smoke(response->readback_code, sizeof(response->readback_code), "BAD_SETTINGS_SMOKE_FRAME");
        return;
    }
    memset(&request, 0, sizeof(request));
    request.project_root = frame->settings_project_root;
    request.axis = frame->settings_axis;
    request.axis_index = frame->settings_axis_index;
    request.field_key = frame->settings_field_key;
    request.field_name = frame->settings_field_name;
    request.value_text = frame->settings_value_text;
    request.owner_generation = frame->settings_owner_generation;
    request.readback_token = frame->settings_readback_token;
    ok = v5_settings_apply_commit_axis_value(&request, &result);
    settings_owner_copy_result(response, &result);
    response->send_status = ok ? V5_COMMAND_GATE_SEND_SENT : V5_COMMAND_GATE_SEND_INVALID;
    response->executed = ok ? 1 : 0;
    copy_cstr_smoke(response->readback_code, sizeof(response->readback_code), ok ? "SETTINGS_AXIS_COMMIT_OK" : "SETTINGS_AXIS_COMMIT_REJECTED");
}

static void settings_owner_loop(int listen_fd)
{
    while (1) {
        int client = accept(listen_fd, 0, 0);
        if (client < 0) {
            if (errno == EINTR) continue;
            _exit(2);
        }
        while (1) {
            V5CommandGateIpcRequestFrame request;
            V5CommandGateIpcResponseFrame response;
            if (!read_all_smoke(client, &request, sizeof(request))) break;
            settings_owner_handle_frame(&request, &response);
            if (!write_all_smoke(client, &response, sizeof(response))) break;
        }
        close(client);
    }
}

static int start_settings_owner_smoke(void)
{
    struct sockaddr_un addr;
    int fd;
    mkdir("/run/8ax_v5_product_ui", 0755);
    unlink(V5_COMMAND_GATE_SOCKET_PATH);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", V5_COMMAND_GATE_SOCKET_PATH);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 4) != 0) {
        close(fd);
        return 0;
    }
    g_settings_owner_pid = fork();
    if (g_settings_owner_pid < 0) {
        close(fd);
        return 0;
    }
    if (g_settings_owner_pid == 0) {
        settings_owner_loop(fd);
        _exit(0);
    }
    close(fd);
    return 1;
}

static void stop_settings_owner_smoke(void)
{
    if (g_settings_owner_pid > 0) {
        kill(g_settings_owner_pid, SIGTERM);
        waitpid(g_settings_owner_pid, 0, 0);
        g_settings_owner_pid = -1;
    }
    unlink(V5_COMMAND_GATE_SOCKET_PATH);
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t n;
    if (!in) return 0;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1U, sizeof(buf), in)) > 0U) {
        if (fwrite(buf, 1U, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

static void trim_smoke(char *text)
{
    char *start = text;
    char *end;
    if (!text) return;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1U);
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) --end;
    *end = '\0';
}


static int read_section_ini_key(const char *path, const char *section_name, const char *key, char *out, size_t out_cap)
{
    FILE *fp = fopen(path, "rb");
    char raw[512];
    int in_section = 0;
    if (!fp || !section_name || !key || !out || out_cap == 0U) return 0;
    out[0] = '\0';
    while (fgets(raw, sizeof(raw), fp)) {
        char probe[512];
        char *eq;
        snprintf(probe, sizeof(probe), "%s", raw);
        trim_smoke(probe);
        if (!probe[0] || probe[0] == '#' || probe[0] == ';') continue;
        if (probe[0] == '[') {
            char section[32];
            section[0] = '\0';
            in_section = sscanf(probe, "[%31[^]]]", section) == 1 && strcmp(section, section_name) == 0;
            continue;
        }
        eq = strchr(probe, '=');
        if (in_section && eq) {
            *eq = '\0';
            trim_smoke(probe);
            trim_smoke(eq + 1);
            if (strcmp(probe, key) == 0) {
                snprintf(out, out_cap, "%s", eq + 1);
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int read_rtcp_ini_key(const char *path, const char *key, char *out, size_t out_cap)
{
    FILE *fp = fopen(path, "rb");
    char raw[512];
    int in_rtcp = 0;
    if (!fp || !key || !out || out_cap == 0U) return 0;
    out[0] = '\0';
    while (fgets(raw, sizeof(raw), fp)) {
        char probe[512];
        char *eq;
        snprintf(probe, sizeof(probe), "%s", raw);
        trim_smoke(probe);
        if (!probe[0] || probe[0] == '#' || probe[0] == ';') continue;
        if (probe[0] == '[') {
            char section[32];
            section[0] = '\0';
            in_rtcp = sscanf(probe, "[%31[^]]]", section) == 1 && strcmp(section, "RTCP") == 0;
            continue;
        }
        eq = strchr(probe, '=');
        if (in_rtcp && eq) {
            *eq = '\0';
            trim_smoke(probe);
            trim_smoke(eq + 1);
            if (strcmp(probe, key) == 0) {
                snprintf(out, out_cap, "%s", eq + 1);
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

int main(void)
{

    char source_root[512];
    char ini_src[768];
    char tmp_root[] = "/tmp/v5_axis_table_smoke_XXXXXX";
    FILE *fp;
    if (!getcwd(source_root, sizeof(source_root)) || !mkdtemp(tmp_root) || chdir(tmp_root) != 0) {
        return 7;
    }
    mkdir("linuxcnc", 0755);
    mkdir("linuxcnc/ini", 0755);
    snprintf(ini_src, sizeof(ini_src), "%s/linuxcnc/ini/v5_bus.ini", source_root);
    if (!copy_file(ini_src, "linuxcnc/ini/v5_bus.ini")) {
        return 7;
    }
    fp = fopen("settings_runtime.json", "wb");
    if (!fp) {
        return 7;
    }
    fputs("{\n"
          "  \"schema\": \"re.v3.settings_runtime.drive_only.v1\",\n"
          "  \"axes\": [\n"
          "    {\n"
          "      \"axis\": \"X\",\n"
          "      \"actual_counts_per_motor_rev\": 12000,\n"
          "      \"zero_model\": {\n"
          "        \"zero_anchor_counts\": 100000,\n"
          "        \"raw_zero_position\": 10,\n"
          "        \"drive_position\": {\"actual_position_counts\": 100000, \"write_status\": \"write_verified_readback\"}\n"
          "      }\n"
          "    }\n"
          "  ]\n"
          "}\n", fp);
    fclose(fp);
    setenv("V5_SETTINGS_RUNTIME_JSON", "settings_runtime.json", 1);
    if (!start_settings_owner_smoke()) {
        return 7;
    }
    atexit(stop_settings_owner_smoke);
    mkdir("config", 0755);
    mkdir("config/settings", 0755);
    fp = fopen("config/settings/self_parameter_table.tsv", "wb");
    if (!fp) {
        return 7;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "SETTINGS\tbus_pulse_setting\t总线\n"
          "SETTINGS\tslave_options\t0,1,2,3,4,5,6,7\n"
          "X\tslave\t0\n"
          "Y\tslave\t1\n"
          "Z\tslave\t2\n"
          "A\tslave\t3\n"
          "B\tslave\t4\n"
          "C\tslave\t5\n"
          "GANTRY\tslave\t6\n"
          "TOOLMAG\tslave\t7\n"
          "G53\ttool_setter_x\t0\n"
          "G53\ttool_setter_y\t0\n"
          "G53\ttool_setter_z\t0\n"
          "G53\tfive_direction_detector_x\t0\n"
          "G53\tfive_direction_detector_y\t0\n"
          "G53\tfive_direction_detector_z\t0\n", fp);
    fclose(fp);
    fp = fopen("config/settings/drive_parameter_table.tsv", "wb");
    if (!fp) {
        return 8;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "X\tencoder_bits\t18\n"
          "X\tegear_numerator\t16384\n"
          "X\tegear_denominator\t3125\n"
          "X\twrite_status\t已写入\n"
          "Y\tencoder_bits\t18\n"
          "Y\tegear_numerator\t16384\n"
          "Y\tegear_denominator\t3125\n"
          "Y\twrite_status\t已写入\n"
          "Z\tencoder_bits\t18\n"
          "Z\tegear_numerator\t16384\n"
          "Z\tegear_denominator\t3125\n"
          "Z\twrite_status\t已写入\n"
          "A\tencoder_bits\t18\n"
          "A\tegear_numerator\t4096\n"
          "A\tegear_denominator\t5625\n"
          "A\twrite_status\t已写入\n"
          "B\tencoder_bits\t18\n"
          "B\tegear_numerator\t4096\n"
          "B\tegear_denominator\t5625\n"
          "B\twrite_status\t已写入\n"
          "C\tencoder_bits\t18\n"
          "C\tegear_numerator\t4096\n"
          "C\tegear_denominator\t5625\n"
          "C\twrite_status\t已写入\n"
          "GANTRY\tencoder_bits\t18\n"
          "GANTRY\tegear_numerator\t16384\n"
          "GANTRY\tegear_denominator\t3125\n"
          "GANTRY\twrite_status\t已写入\n"
          "TOOLMAG\tencoder_bits\t18\n"
          "TOOLMAG\tegear_numerator\t4096\n"
          "TOOLMAG\tegear_denominator\t5625\n"
          "TOOLMAG\twrite_status\t已写入\n", fp);
    fputs("SLAVE_0\tegear_numerator\t777\n"
          "SLAVE_0\tegear_denominator\t888\n"
          "SLAVE_0\twrite_status\tslave-ok\n", fp);
    fclose(fp);
    unsigned int rows = v5_settings_axis_table_row_count();
    unsigned int cols = v5_settings_axis_table_column_count();
    const V5SettingsAxisRowSpec *row = v5_settings_axis_table_rows();
    const V5SettingsAxisColumnSpec *col = v5_settings_axis_table_columns();
    unsigned int native_or_runtime = 0U;
    unsigned int drive_only = 0U;
    unsigned int self_table = 0U;
    unsigned int select_cols = 0U;

    v5_settings_axis_table_load_readback(".");
    if (strcmp(v5_settings_axis_table_value(0U, 16U), "777") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 17U), "888") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 18U), "slave-ok") != 0) {
        fprintf(stderr, "drive display columns must prefer selected slave evidence: num=%s den=%s status=%s\n",
                v5_settings_axis_table_value(0U, 16U),
                v5_settings_axis_table_value(0U, 17U),
                v5_settings_axis_table_value(0U, 18U));
        return 47;
    }
    if (rows != 8U || cols != 19U) {
        return 1;
    }
    if (!same_text(row[0].axis, "X") || !same_text(row[6].axis, "GANTRY") || !same_text(row[7].axis, "TOOLMAG")) {
        return 2;
    }
    for (unsigned int c = 0; c < cols; ++c) {
        if (col[c].kind == 1) {
            ++select_cols;
        }
    }
    if (select_cols != 6U ||
        col[10].kind != 2 ||
        !same_text(col[10].field_key, "zero") ||
        !same_text(col[0].field_key, "axis_mode") ||
        !same_text(col[1].field_key, "direction_mode") ||
        !same_text(col[2].field_key, "slave") ||
        !same_text(col[7].field_key, "home_order") ||
        !same_text(col[8].field_key, "home_direction") ||
        !same_text(col[15].field_key, "encoder_bits")) {
        return 12;
    }
    if (col[0].x != 0 || col[0].width != 94 ||
        col[1].x != 100 || col[1].width != 62 ||
        col[2].x != 168 || col[2].width != 98 ||
        col[3].x != 272 || col[3].width != 112 ||
        col[4].x != 390 || col[4].width != 76 ||
        col[7].x != 632 || col[8].x != 728 ||
        col[12].x != 1122 || col[15].x != 1440 ||
        col[16].x != 1516 || col[18].x != 1712) {
        fprintf(stderr, "axis v3 column anchors changed: mode=%d direction=%d slave=%d precision=%d pitch=%d bit=%d\n",
                col[0].x, col[1].x, col[2].x, col[3].x, col[4].x, col[15].x);
        return 22;
    }
    for (unsigned int r = 0; r < rows; ++r) {
        for (unsigned int c = 0; c < cols; ++c) {
            if (!v5_settings_axis_table_field_matches_owner(&row[r], &col[c])) {
                char id[96];
                v5_settings_axis_table_field_id(&row[r], &col[c], id, sizeof(id));
                fprintf(stderr, "axis owner mismatch: %s\n", id);
                return 3;
            }
            {
                char id[96];
                const V5ParameterField *field;
                v5_settings_axis_table_field_id(&row[r], &col[c], id, sizeof(id));
                field = v5_parameter_table_find(id);
                if (field && field->owner == V5_PARAMETER_OWNER_SELF_PARAMETER_TABLE) {
                    ++self_table;
                } else if (col[c].drive_only_allowed) {
                    ++drive_only;
                } else {
                    ++native_or_runtime;
                }
                if (!v5_settings_axis_table_value_is_real(r, c) ||
                    same_text(v5_settings_axis_table_value(r, c), "NAT") ||
                    same_text(v5_settings_axis_table_value(r, c), "--")) {
                    fprintf(stderr, "axis value missing: %s=%s\n", id, v5_settings_axis_table_value(r, c));
                    return 6;
                }
            }
        }
    }
    if (native_or_runtime == 0U || drive_only == 0U || self_table == 0U) {
        return 4;
    }
    if (strcmp(v5_settings_axis_table_value(0U, 0U), "直线") != 0 ||
        strcmp(v5_settings_axis_table_value(3U, 0U), "旋转") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 1U), "cw") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 2U), "0") != 0 ||
        v5_settings_axis_table_slave_option_count() != 8U ||
        strcmp(v5_settings_axis_table_slave_option(0U), "0") != 0 ||
        strcmp(v5_settings_axis_table_slave_option(7U), "7") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 4U), "5") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 5U), "1") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 6U), "1") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 9U), "-500") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 11U), "500") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 12U), "10000") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 15U), "18") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 16U), "777") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 17U), "888") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 18U), "slave-ok") != 0 ||
        strcmp(v5_settings_axis_table_value(3U, 12U), "50000") != 0 ||
        strcmp(v5_settings_axis_table_value(6U, 0U), "直线") != 0 ||
        strcmp(v5_settings_axis_table_value(6U, 9U), "-500") != 0 ||
        strcmp(v5_settings_axis_table_value(6U, 11U), "500") != 0 ||
        strcmp(v5_settings_axis_table_value(6U, 12U), "10000") != 0 ||
        strcmp(v5_settings_axis_table_value(7U, 0U), "旋转") != 0 ||
        strcmp(v5_settings_axis_table_value(7U, 15U), "18") != 0) {
        fprintf(stderr, "axis readback values missing: X mode=%s A mode=%s X direction=%s X slave=%s X pitch=%s X motor=%s X load=%s X min=%s X max=%s X velocity=%s X bit=%s A velocity=%s GANTRY mode=%s GANTRY min=%s GANTRY max=%s GANTRY velocity=%s TOOLMAG mode=%s TOOLMAG bit=%s\n",
                v5_settings_axis_table_value(0U, 0U),
                v5_settings_axis_table_value(3U, 0U),
                v5_settings_axis_table_value(0U, 1U),
                v5_settings_axis_table_value(0U, 2U),
                v5_settings_axis_table_value(0U, 4U),
                v5_settings_axis_table_value(0U, 5U),
                v5_settings_axis_table_value(0U, 6U),
                v5_settings_axis_table_value(0U, 9U),
                v5_settings_axis_table_value(0U, 11U),
                v5_settings_axis_table_value(0U, 12U),
                v5_settings_axis_table_value(0U, 15U),
                v5_settings_axis_table_value(3U, 12U),
                v5_settings_axis_table_value(6U, 0U),
                v5_settings_axis_table_value(6U, 9U),
                v5_settings_axis_table_value(6U, 11U),
                v5_settings_axis_table_value(6U, 12U),
                v5_settings_axis_table_value(7U, 0U),
                v5_settings_axis_table_value(7U, 15U));
        return 5;
    }

    {
        char options[256];
        if (!v5_settings_axis_table_dropdown_options(0U, 2U, options, sizeof(options)) ||
            !dropdown_has_line(options, "NAT") ||
            !dropdown_has_line(options, "0") ||
            dropdown_has_line(options, "1") ||
            dropdown_has_line(options, "7")) {
            fprintf(stderr, "X slave dropdown must keep NAT and own slave only when all scanned slaves are assigned: %s\n", options);
            return 34;
        }
        if (!v5_settings_axis_table_dropdown_options(1U, 2U, options, sizeof(options)) ||
            !dropdown_has_line(options, "NAT") ||
            !dropdown_has_line(options, "1") ||
            dropdown_has_line(options, "0")) {
            fprintf(stderr, "Y slave dropdown must hide X selected slave: %s\n", options);
            return 35;
        }
    }
    if (!v5_settings_axis_table_commit_value(0U, 2U, "NAT") ||
        strcmp(v5_settings_axis_table_value(0U, 2U), "NAT") != 0) {
        fprintf(stderr, "slave NAT commit failed: X slave=%s\n", v5_settings_axis_table_value(0U, 2U));
        return 36;
    }
    if (strcmp(v5_settings_axis_table_value(0U, 16U), "--") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 17U), "--") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 18U), "--") != 0) {
        fprintf(stderr, "NAT slave must clear drive display columns: num=%s den=%s status=%s\n",
                v5_settings_axis_table_value(0U, 16U),
                v5_settings_axis_table_value(0U, 17U),
                v5_settings_axis_table_value(0U, 18U));
        return 48;
    }
    {
        char self_slave[64];
        if (!v5_settings_parameter_store_read_axis(".", V5_SETTINGS_PARAMETER_DISK_SELF, "X", "slave", self_slave, sizeof(self_slave)) ||
            strcmp(self_slave, "NAT") != 0) {
            fprintf(stderr, "slave NAT self owner readback failed: %s\n", self_slave);
            return 37;
        }
    }
    if (!v5_settings_axis_table_cell_is_disabled(0U, 0U) ||
        v5_settings_axis_table_cell_is_disabled(0U, 2U) ||
        !v5_settings_axis_table_cell_is_disabled(0U, 10U) ||
        !v5_settings_axis_table_cell_is_disabled(0U, 15U)) {
        fprintf(stderr, "NAT row must disable every axis parameter cell except slave\n");
        return 43;
    }
    if (v5_settings_axis_table_commit_value(0U, 12U, "12000")) {
        fprintf(stderr, "NAT row non-slave parameter commit must be rejected\n");
        return 44;
    }
    if (v5_settings_axis_table_start_axis_zero(0U, 0)) {
        fprintf(stderr, "BUS axis zero must reject NAT slave binding\n");
        return 38;
    }
    {
        char options[256];
        if (!v5_settings_axis_table_dropdown_options(0U, 2U, options, sizeof(options)) ||
            !dropdown_has_line(options, "NAT") ||
            !dropdown_has_line(options, "0") ||
            dropdown_has_line(options, "1")) {
            fprintf(stderr, "NAT row slave dropdown must expose unassigned scanned slave 0 and hide Y slave 1: %s\n", options);
            return 39;
        }
    }
    if (!v5_settings_axis_table_commit_value(0U, 2U, "0") ||
        strcmp(v5_settings_axis_table_value(0U, 2U), "0") != 0) {
        fprintf(stderr, "slave restore commit failed: X slave=%s\n", v5_settings_axis_table_value(0U, 2U));
        return 40;
    }
    if (strcmp(v5_settings_axis_table_value(0U, 16U), "777") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 17U), "888") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 18U), "slave-ok") != 0) {
        fprintf(stderr, "restored slave must restore slave drive display columns: num=%s den=%s status=%s\n",
                v5_settings_axis_table_value(0U, 16U),
                v5_settings_axis_table_value(0U, 17U),
                v5_settings_axis_table_value(0U, 18U));
        return 49;
    }
    if (v5_settings_axis_table_cell_is_disabled(0U, 0U) ||
        v5_settings_axis_table_cell_is_disabled(0U, 2U) ||
        v5_settings_axis_table_cell_is_disabled(0U, 10U) ||
        v5_settings_axis_table_cell_is_disabled(0U, 15U)) {
        fprintf(stderr, "real slave row must re-enable editable/action cells after restore\n");
        return 45;
    }

    if (v5_settings_axis_table_commit_value(0U, 12U, "12000.5") ||
        v5_settings_axis_table_commit_value(0U, 4U, "6.5")) {
        fprintf(stderr, "axis numeric fields except precision must reject decimal input\n");
        return 46;
    }

    if (!v5_settings_axis_table_commit_value(0U, 12U, "12000")) {
        fprintf(stderr, "axis max velocity per-minute commit failed\n");
        return 32;
    }
    {
        char axis_vel[64];
        char joint_vel[64];
        if (!read_section_ini_key("linuxcnc/ini/v5_bus.ini", "AXIS_X", "MAX_VELOCITY", axis_vel, sizeof(axis_vel)) ||
            !read_section_ini_key("linuxcnc/ini/v5_bus.ini", "JOINT_0", "MAX_VELOCITY", joint_vel, sizeof(joint_vel)) ||
            strcmp(axis_vel, "200") != 0 ||
            strcmp(joint_vel, "200") != 0 ||
            strcmp(v5_settings_axis_table_value(0U, 12U), "12000") != 0) {
            fprintf(stderr, "axis max velocity unit conversion failed: axis=%s joint=%s ui=%s\n",
                    axis_vel, joint_vel, v5_settings_axis_table_value(0U, 12U));
            return 33;
        }
    }

    if (v5_settings_axis_table_commit_value(0U, 10U, "0")) {
        fprintf(stderr, "axis zero must not use generic parameter commit\n");
        return 25;
    }
    v5_settings_axis_table_set_axis_zero_callback(axis_zero_smoke_cb, 0);
    if (!v5_settings_axis_table_start_axis_zero(0U, 0)) {
        fprintf(stderr, "BUS axis zero action request failed\n");
        return 26;
    }
    if (g_axis_zero_callback_count != 1U ||
        strcmp(g_axis_zero_axis, "X") != 0 ||
        strcmp(g_axis_zero_mode, "bus") != 0 ||
        strcmp(g_axis_zero_scope, "bus_count_domain_zero") != 0 ||
        strcmp(g_axis_zero_apply, "count_domain_zero") != 0 ||
        strcmp(g_axis_zero_slave, "0") != 0) {
        fprintf(stderr, "BUS axis zero callback mismatch: count=%u axis=%s mode=%s scope=%s apply=%s slave=%s\n",
                g_axis_zero_callback_count, g_axis_zero_axis, g_axis_zero_mode, g_axis_zero_scope, g_axis_zero_apply, g_axis_zero_slave);
        return 27;
    }
    if (!v5_settings_axis_table_commit_value(0U, 4U, "6")) {
        fprintf(stderr, "axis pitch runtime INI commit failed\n");
        return 13;
    }
    {
        char axis_pitch[64];
        char joint_scale[64];
        char axis_min[64];
        char joint_min[64];
        char axis_max[64];
        if (!read_section_ini_key("linuxcnc/ini/v5_bus.ini", "AXIS_X", "PITCH", axis_pitch, sizeof(axis_pitch)) ||
            strcmp(axis_pitch, "6") != 0 || strcmp(v5_settings_axis_table_value(0U, 4U), "6") != 0) {
            fprintf(stderr, "axis pitch runtime INI readback failed: ini=%s ui=%s\n", axis_pitch, v5_settings_axis_table_value(0U, 4U));
            return 13;
        }
        if (!read_section_ini_key("linuxcnc/ini/v5_bus.ini", "JOINT_0", "SCALE", joint_scale, sizeof(joint_scale)) ||
            strcmp(joint_scale, "2000") != 0 ||
            strcmp(v5_settings_axis_table_value(0U, 3U), "0.0005") != 0) {
            fprintf(stderr, "axis pitch scale-chain recompute failed: scale=%s precision=%s\n",
                    joint_scale, v5_settings_axis_table_value(0U, 3U));
            return 30;
        }
        if (!read_section_ini_key("linuxcnc/ini/v5_bus.ini", "AXIS_X", "MIN_LIMIT", axis_min, sizeof(axis_min)) ||
            !read_section_ini_key("linuxcnc/ini/v5_bus.ini", "JOINT_0", "MIN_LIMIT", joint_min, sizeof(joint_min)) ||
            !read_section_ini_key("linuxcnc/ini/v5_bus.ini", "AXIS_X", "MAX_LIMIT", axis_max, sizeof(axis_max)) ||
            strcmp(axis_min, "-460") != 0 || strcmp(joint_min, "-460") != 0 ||
            strcmp(axis_max, "540") != 0 ||
            strcmp(v5_settings_axis_table_value(0U, 9U), "-460") != 0 ||
            strcmp(v5_settings_axis_table_value(0U, 11U), "540") != 0) {
            fprintf(stderr, "axis pitch raw-limit recompute failed: axis_min=%s joint_min=%s axis_max=%s ui_min=%s ui_max=%s\n",
                    axis_min, joint_min, axis_max,
                    v5_settings_axis_table_value(0U, 9U),
                    v5_settings_axis_table_value(0U, 11U));
            return 31;
        }
        if (strcmp(v5_settings_axis_table_value(0U, 18U), "slave-ok") != 0) {
            fprintf(stderr, "axis pitch commit must not overwrite drive write_status: status=%s\n", v5_settings_axis_table_value(0U, 18U));
            return 28;
        }
    }
    if (!v5_settings_axis_table_commit_value(0U, 3U, "0.00025") ||
        strcmp(v5_settings_axis_table_value(0U, 3U), "0.00025") != 0) {
        fprintf(stderr, "axis precision must still accept decimal input: precision=%s\n",
                v5_settings_axis_table_value(0U, 3U));
        return 47;
    }
    if (v5_settings_axis_table_commit_value(0U, 15U, "NAT") ||
        v5_settings_axis_table_commit_value(0U, 15U, "--")) {
        fprintf(stderr, "axis bit placeholder must not be committed\n");
        return 23;
    }
    if (!v5_settings_axis_table_commit_value(0U, 15U, "19")) {
        fprintf(stderr, "axis bit commit failed\n");
        return 15;
    }
    {
        char drive[64];
        if (!v5_settings_parameter_store_read_axis(".", V5_SETTINGS_PARAMETER_DISK_DRIVE, "X", "encoder_bits", drive, sizeof(drive)) ||
            strcmp(drive, "19") != 0 || strcmp(v5_settings_axis_table_value(0U, 15U), "19") != 0) {
            fprintf(stderr, "axis bit owner readback failed: drive=%s ui=%s\n", drive, v5_settings_axis_table_value(0U, 15U));
            return 16;
        }
        if (strcmp(v5_settings_axis_table_value(0U, 18U), "slave-ok") != 0) {
            fprintf(stderr, "axis bit commit must not overwrite drive write_status: status=%s\n", v5_settings_axis_table_value(0U, 18U));
            return 29;
        }
    }
    fp = fopen("config/settings/drive_parameter_table.tsv", "wb");
    if (!fp) {
        return 30;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "Y\tencoder_bits\t18\n"
          "Z\tencoder_bits\t18\n", fp);
    fclose(fp);
    v5_settings_axis_table_load_readback(".");
    if (strcmp(v5_settings_axis_table_value(0U, 15U), "--") != 0 ||
        v5_settings_axis_table_value_is_real(0U, 15U) ||
        v5_settings_axis_table_commit_value(0U, 15U, "--")) {
        fprintf(stderr, "missing axis bit must display unavailable and stay uncommittable: bit=%s real=%d\n",
                v5_settings_axis_table_value(0U, 15U),
                v5_settings_axis_table_value_is_real(0U, 15U));
        return 30;
    }
    if (v5_settings_axis_table_g53_value_is_editable(0U, 0U) ||
        v5_settings_axis_table_g53_value_is_editable(1U, 1U) ||
        v5_settings_axis_table_g53_value_is_editable(2U, 2U) ||
        v5_settings_axis_table_commit_g53_value(0U, 0U, "123")) {
        fprintf(stderr, "g53 modal owner cells must stay read-only\n");
        return 17;
    }
    if (!v5_settings_axis_table_g53_value_is_editable(1U, 0U) ||
        !v5_settings_axis_table_commit_g53_value(1U, 0U, "12.5")) {
        fprintf(stderr, "g53 native geometry runtime INI commit failed\n");
        return 18;
    }
    {
        char rtcp_g53[64];
        if (!read_rtcp_ini_key("linuxcnc/ini/v5_bus.ini", "G53_B_X", rtcp_g53, sizeof(rtcp_g53)) ||
            strcmp(rtcp_g53, "12.5") != 0 || strcmp(v5_settings_axis_table_g53_value(1U, 0U), "12.5") != 0) {
            fprintf(stderr, "g53 native geometry runtime INI readback failed: ini=%s ui=%s\n", rtcp_g53, v5_settings_axis_table_g53_value(1U, 0U));
            return 18;
        }
    }
    if (!v5_settings_axis_table_g53_value_is_editable(3U, 2U) ||
        !v5_settings_axis_table_commit_g53_value(3U, 2U, "88")) {
        fprintf(stderr, "g53 tool setter Z commit failed\n");
        return 20;
    }
    {
        char self_g53[64];
        if (!v5_settings_parameter_store_read_axis(".", V5_SETTINGS_PARAMETER_DISK_SELF, "G53", "tool_setter_z", self_g53, sizeof(self_g53)) ||
            strcmp(self_g53, "88") != 0 || strcmp(v5_settings_axis_table_g53_value(3U, 2U), "88") != 0) {
            fprintf(stderr, "g53 tool setter Z readback failed: self=%s ui=%s\n", self_g53, v5_settings_axis_table_g53_value(3U, 2U));
            return 21;
        }
    }
    fp = fopen("config/settings/self_parameter_table.tsv", "wb");
    if (!fp) {
        return 24;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "SETTINGS\tbus_pulse_setting\t总线\n"
          "SETTINGS\tslave_options\t0:SV630_1Axis_03713,1:SV630_1Axis_03715\n"
          "X\tslave\t0\n"
          "Y\tslave\t1\n", fp);
    fclose(fp);
    v5_settings_axis_table_load_readback(".");
    if (v5_settings_axis_table_slave_option_count() != 2U ||
        strcmp(v5_settings_axis_table_slave_option(0U), "0 SV630_1Axis_03713") != 0 ||
        strcmp(v5_settings_axis_table_slave_option(1U), "1 SV630_1Axis_03715") != 0 ||
        strcmp(v5_settings_axis_table_value(0U, 2U), "0") != 0) {
        fprintf(stderr, "slave options must reload from SETTINGS/slave_options: count=%u opt0=%s opt1=%s x_slave=%s\n",
                v5_settings_axis_table_slave_option_count(),
                v5_settings_axis_table_slave_option(0U),
                v5_settings_axis_table_slave_option(1U),
                v5_settings_axis_table_value(0U, 2U));
        return 24;
    }
    {
        char options[256];
        if (!v5_settings_axis_table_dropdown_options(0U, 2U, options, sizeof(options)) ||
            !dropdown_has_line(options, "NAT") ||
            !dropdown_has_line(options, "0 SV630_1Axis_03713") ||
            dropdown_has_line(options, "1 SV630_1Axis_03715")) {
            fprintf(stderr, "labeled slave dropdown must keep own label and hide Y label: %s\n", options);
            return 41;
        }
    }
    fp = fopen("config/settings/self_parameter_table.tsv", "wb");
    if (!fp) {
        return 31;
    }
    fputs("# schema=v5.settings.parameter_table.tsv.v1\n"
          "SETTINGS\tbus_pulse_setting\t总线\n"
          "X\tslave\t0\n"
          "Y\tslave\t1\n", fp);
    fclose(fp);
    v5_settings_axis_table_load_readback(".");
    if (v5_settings_axis_table_slave_option_count() != 0U ||
        strcmp(v5_settings_axis_table_value(0U, 2U), "0") != 0) {
        fprintf(stderr, "slave options must not fall back to current axis assignments: count=%u x_slave=%s\n",
                v5_settings_axis_table_slave_option_count(),
                v5_settings_axis_table_value(0U, 2U));
        return 31;
    }
    {
        char options[256];
        if (!v5_settings_axis_table_dropdown_options(0U, 2U, options, sizeof(options)) ||
            strcmp(options, "NAT") != 0) {
            fprintf(stderr, "slave dropdown without scan candidates must only show NAT: %s\n", options);
            return 42;
        }
    }

    unlink("linuxcnc/ini/v5_bus.ini");
    rmdir("linuxcnc/ini");
    rmdir("linuxcnc");
    unlink("config/settings/self_parameter_table.tsv");
    unlink("config/settings/drive_parameter_table.tsv");
    rmdir("config/settings");
    rmdir("config");
    chdir("/");
    rmdir(tmp_root);

    printf("v5 settings axis table: rows=%u cols=%u native_runtime_cells=%u self_table_cells=%u drive_only_cells=%u x_pitch=%s x_velocity=%s x_bit=%s gantry_velocity=%s toolmag_bit=%s x_egear=%s/%s\n",
           rows,
           cols,
           native_or_runtime,
           self_table,
           drive_only,
           v5_settings_axis_table_value(0U, 4U),
           v5_settings_axis_table_value(0U, 12U),
           v5_settings_axis_table_value(0U, 15U),
           v5_settings_axis_table_value(6U, 12U),
           v5_settings_axis_table_value(7U, 15U),
           v5_settings_axis_table_value(0U, 16U),
           v5_settings_axis_table_value(0U, 17U));
    return 0;
}
