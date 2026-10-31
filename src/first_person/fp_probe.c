/* A probe build's (FPV_PROBE) pose-setter census: which engine Unit setters touch the camera unit each frame, and from
 * where. Logged once a second for ten seconds after the camera unit is first known. Not in normal builds. */
#ifdef FPV_PROBE
#include <stdio.h>
#include <windows.h>
#include "fp.h"

#define PROBE_SLOTS 7
static const uint32_t kSlots[PROBE_SLOTS] = {0x1FC650, 0x1FC720, 0x1FC7A0, 0x1FC880, 0x1FC990, 0x1FCAD0, 0x1FCA10};
typedef void (*SetFn)(uint32_t unit, int32_t node, const void* value);
static SetFn g_orig[PROBE_SLOTS];
static volatile LONG g_count[PROBE_SLOTS];
static uintptr_t g_caller[PROBE_SLOTS];
static int32_t g_node[PROBE_SLOTS];
static ULONGLONG g_since;
static int g_lines;

static void Note(int slot, uint32_t unit, int32_t node, void* caller)
{
    FpvCameraStatus status;
    fpv_camera_status(&status);
    if (status.unit == 0 || unit != status.unit) return;
    InterlockedIncrement(&g_count[slot]);
    g_caller[slot] = (uintptr_t)caller;
    g_node[slot] = node;
}

#define DETOUR(i) static void Detour##i(uint32_t unit, int32_t node, const void* value) \
    { Note(i, unit, node, __builtin_return_address(0)); g_orig[i](unit, node, value); }
DETOUR(0) DETOUR(1) DETOUR(2) DETOUR(3) DETOUR(4) DETOUR(5) DETOUR(6)
static void* const kDetours[PROBE_SLOTS] = {Detour0, Detour1, Detour2, Detour3, Detour4, Detour5, Detour6};

static const char* Module(uintptr_t at, uintptr_t* rva)
{
    const FpvHost* h = fpv_host();
    if (at >= h->exe_base && at < h->exe_base + h->exe_size) { *rva = at - h->exe_base; return "exe"; }
    if (at >= h->game_base && at < h->game_base + h->game_size) { *rva = at - h->game_base; return "game.dll"; }
    *rva = at;
    return "?";
}

void fp_probe_install(void)
{
    for (int i = 0; i < PROBE_SLOTS; i++)
        if (!fpv_exe_hook(kSlots[i], "48 89 5C 24 08", kDetours[i], (void**)&g_orig[i], 30))
            fpv_log("probe", "could not hook exe+0x%X", kSlots[i]);
}

void fp_probe_tick(void)
{
    ULONGLONG now = GetTickCount64();
    if (g_since == 0) { g_since = now; return; }
    if (now - g_since < 1000 || g_lines >= 30) return;
    g_since = now;
    g_lines++;
    char text[600];
    size_t used = 0;
    for (int i = 0; i < PROBE_SLOTS; i++)
    {
        LONG n = InterlockedExchange(&g_count[i], 0);
        if (n == 0) continue;
        uintptr_t rva;
        const char* module = Module(g_caller[i], &rva);
        used += (size_t)snprintf(text + used, sizeof text - used, " exe+0x%X: %ld (node %d, from %s+0x%llX);", kSlots[i], n,
            g_node[i], module, (unsigned long long)rva);
    }
    FpvCameraStatus status;
    fpv_camera_status(&status);
    fpv_log("probe", "camera %u setters:%s", status.unit, used ? text : " none");
}
#endif
