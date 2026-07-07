#include "v5_remote_metrics.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

typedef struct V5CpuSample {
    unsigned long long total;
    unsigned long long idle;
    int valid;
} V5CpuSample;

static V5CpuSample g_cpu_prev[2];

static unsigned long long monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)ts.tv_sec * 1000ULL) + ((unsigned long long)ts.tv_nsec / 1000000ULL);
}

static double percent_round1(double value)
{
    if (value < 0.0) {
        value = 0.0;
    }
    if (value > 100.0) {
        value = 100.0;
    }
    return ((double)((unsigned long long)(value * 10.0 + 0.5))) / 10.0;
}

static int read_cpu_sample(const char *name, V5CpuSample *sample)
{
    FILE *fp = fopen("/proc/stat", "r");
    char line[256];
    char cpu_name[16];
    unsigned long long values[10];
    unsigned int i;
    if (!fp) {
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        memset(values, 0, sizeof(values));
        if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   cpu_name,
                   &values[0], &values[1], &values[2], &values[3], &values[4],
                   &values[5], &values[6], &values[7], &values[8], &values[9]) < 5) {
            continue;
        }
        if (strcmp(cpu_name, name) != 0) {
            continue;
        }
        fclose(fp);
        sample->total = 0ULL;
        for (i = 0U; i < 10U; ++i) {
            sample->total += values[i];
        }
        sample->idle = values[3] + values[4];
        sample->valid = 1;
        return sample->total > 0ULL;
    }
    fclose(fp);
    return 0;
}

static int cpu_percent(unsigned int index, const char *name, double *out)
{
    V5CpuSample current;
    V5CpuSample previous;
    unsigned long long total_delta;
    unsigned long long idle_delta;
    memset(&current, 0, sizeof(current));
    if (index >= 2U || !read_cpu_sample(name, &current)) {
        return 0;
    }
    previous = g_cpu_prev[index];
    g_cpu_prev[index] = current;
    if (previous.valid && current.total > previous.total && current.idle >= previous.idle) {
        total_delta = current.total - previous.total;
        idle_delta = current.idle - previous.idle;
        if (total_delta > 0ULL) {
            *out = percent_round1((1.0 - ((double)idle_delta / (double)total_delta)) * 100.0);
            return 1;
        }
    }
    *out = percent_round1((1.0 - ((double)current.idle / (double)current.total)) * 100.0);
    return 1;
}

static int read_memory_metrics(unsigned long long *used, unsigned long long *total, double *percent)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    char line[128];
    unsigned long long mem_total = 0ULL;
    unsigned long long mem_available = 0ULL;
    if (!fp) {
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long value = 0ULL;
        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            mem_total = value * 1024ULL;
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            mem_available = value * 1024ULL;
        }
    }
    fclose(fp);
    if (mem_total == 0ULL) {
        return 0;
    }
    *total = mem_total;
    *used = mem_total > mem_available ? mem_total - mem_available : 0ULL;
    *percent = percent_round1(((double)*used / (double)*total) * 100.0);
    return 1;
}

static int read_disk_metrics(unsigned long long *used, unsigned long long *total, double *percent)
{
    struct statvfs st;
    unsigned long long blocks;
    unsigned long long free_blocks;
    if (statvfs("/", &st) != 0 || st.f_frsize == 0UL) {
        return 0;
    }
    blocks = (unsigned long long)st.f_blocks * (unsigned long long)st.f_frsize;
    free_blocks = (unsigned long long)st.f_bfree * (unsigned long long)st.f_frsize;
    if (blocks == 0ULL) {
        return 0;
    }
    *total = blocks;
    *used = blocks > free_blocks ? blocks - free_blocks : 0ULL;
    *percent = percent_round1(((double)*used / (double)*total) * 100.0);
    return 1;
}

static void number_or_null(char *buffer, size_t size, int ok, double value)
{
    if (ok) {
        snprintf(buffer, size, "%.1f", value);
    } else {
        snprintf(buffer, size, "null");
    }
}

static void ull_or_null(char *buffer, size_t size, int ok, unsigned long long value)
{
    if (ok) {
        snprintf(buffer, size, "%llu", value);
    } else {
        snprintf(buffer, size, "null");
    }
}

void v5_remote_metrics_json(char *buffer, size_t size)
{
    double cpu0 = 0.0;
    double cpu1 = 0.0;
    double memory_percent = 0.0;
    double disk_percent = 0.0;
    unsigned long long memory_used = 0ULL;
    unsigned long long memory_total = 0ULL;
    unsigned long long disk_used = 0ULL;
    unsigned long long disk_total = 0ULL;
    int cpu0_ok = cpu_percent(0U, "cpu0", &cpu0);
    int cpu1_ok = cpu_percent(1U, "cpu1", &cpu1);
    int memory_ok = read_memory_metrics(&memory_used, &memory_total, &memory_percent);
    int disk_ok = read_disk_metrics(&disk_used, &disk_total, &disk_percent);
    char cpu0_text[32];
    char cpu1_text[32];
    char mem_percent_text[32];
    char disk_percent_text[32];
    char mem_used_text[32];
    char mem_total_text[32];
    char disk_used_text[32];
    char disk_total_text[32];
    number_or_null(cpu0_text, sizeof(cpu0_text), cpu0_ok, cpu0);
    number_or_null(cpu1_text, sizeof(cpu1_text), cpu1_ok, cpu1);
    number_or_null(mem_percent_text, sizeof(mem_percent_text), memory_ok, memory_percent);
    number_or_null(disk_percent_text, sizeof(disk_percent_text), disk_ok, disk_percent);
    ull_or_null(mem_used_text, sizeof(mem_used_text), memory_ok, memory_used);
    ull_or_null(mem_total_text, sizeof(mem_total_text), memory_ok, memory_total);
    ull_or_null(disk_used_text, sizeof(disk_used_text), disk_ok, disk_used);
    ull_or_null(disk_total_text, sizeof(disk_total_text), disk_ok, disk_total);
    snprintf(buffer, size,
             "{\"cpu0_percent\":%s,\"cpu1_percent\":%s,\"memory_percent\":%s,\"disk_percent\":%s,\"memory_used_bytes\":%s,\"memory_total_bytes\":%s,\"disk_used_bytes\":%s,\"disk_total_bytes\":%s}",
             cpu0_text, cpu1_text, mem_percent_text, disk_percent_text,
             mem_used_text, mem_total_text, disk_used_text, disk_total_text);
}

void v5_remote_metrics_display_text(char *cpu0, size_t cpu0_size, char *cpu1, size_t cpu1_size)
{
    static char cached_cpu0[24] = "cpu0  --%";
    static char cached_cpu1[24] = "cpu1  --%";
    static unsigned long long last_update_ms;
    unsigned long long now = monotonic_ms();
    double cpu0_percent = 0.0;
    double cpu1_percent = 0.0;
    int cpu0_ok;
    int cpu1_ok;
    if (last_update_ms != 0ULL && now >= last_update_ms && now - last_update_ms < 1000ULL) {
        snprintf(cpu0, cpu0_size, "%s", cached_cpu0);
        snprintf(cpu1, cpu1_size, "%s", cached_cpu1);
        return;
    }
    cpu0_ok = cpu_percent(0U, "cpu0", &cpu0_percent);
    cpu1_ok = cpu_percent(1U, "cpu1", &cpu1_percent);
    if (cpu0_ok) {
        snprintf(cached_cpu0, sizeof(cached_cpu0), "cpu0  %.0f%%", cpu0_percent);
    }
    if (cpu1_ok) {
        snprintf(cached_cpu1, sizeof(cached_cpu1), "cpu1  %.0f%%", cpu1_percent);
    }
    last_update_ms = now;
    snprintf(cpu0, cpu0_size, "%s", cached_cpu0);
    snprintf(cpu1, cpu1_size, "%s", cached_cpu1);
}
