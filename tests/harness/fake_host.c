#include <stdio.h>
#include <string.h>
#include "harness/fake_host.h"

#define FAKE_RANGES 256

static struct { uintptr_t from, to; } g_ranges[FAKE_RANGES];
static int g_rangeCount;
static char g_log[16384];
static size_t g_logLength;

static void Log(const char* feature, const char* text)
{
    int n = snprintf(g_log + g_logLength, sizeof g_log - g_logLength, "[%s] %s\n", feature, text);
    if (n > 0) g_logLength += (size_t)n < sizeof g_log - g_logLength ? (size_t)n : sizeof g_log - g_logLength - 1;
}

static int Read(uintptr_t address, void* out, size_t size)
{
    for (int i = 0; i < g_rangeCount; i++)
        if (address >= g_ranges[i].from && address + size <= g_ranges[i].to)
        {
            memcpy(out, (const void*)address, size);
            return 1;
        }
    return 0;
}

void fake_setup(void)
{
    g_rangeCount = 0;
    g_logLength = 0;
    g_log[0] = 0;
    FpvHost* host = fpv_host_for_test();
    memset(host, 0, sizeof *host);
    host->log = Log;
    host->read = Read;
    fpv_camera_reset_for_test();
}

void fake_readable(const void* address, size_t size)
{
    if (g_rangeCount == FAKE_RANGES) return;
    g_ranges[g_rangeCount].from = (uintptr_t)address;
    g_ranges[g_rangeCount].to = (uintptr_t)address + size;
    g_rangeCount++;
}

int fake_log_has(const char* text) { return strstr(g_log, text) != NULL; }
const char* fake_log_text(void) { return g_log; }
