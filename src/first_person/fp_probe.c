/* A probe build's (FPV_PROBE) camera-getter census: which engine Camera C API getters (exe+0x30DE10 table) game code
 * calls on the player's camera, and from where. Logged once a second, 12 lines, while First Person is active. Not in
 * normal builds. */
#ifdef FPV_PROBE
#include <stdio.h>
#include <windows.h>
#include "fp.h"

#define PROBE_SLOTS 7
/* local_position-ish, +0x10, local_pose, world_position, world_pose, near_range, vertical_fov */
static const uint32_t kSlots[PROBE_SLOTS] = {0x30DE10, 0x30DE70, 0x30DE90, 0x30E190, 0x30E1F0, 0x30D940, 0x30D980};
static const char* const kPins[PROBE_SLOTS] = {"48 8B 41 18 48 63 51 20", "48 8B 41 18 48 63 51 20", "48 63 41 20 48 8B 49 18",
    "48 8B 41 18 48 63 51 20", "48 63 41 20 48 8B 49 18", "F3 0F 10 41 28 C3", "F3 0F 10 41 34 C3"};
typedef uintptr_t (*PtrFn)(uintptr_t camera);
typedef float (*FloatFn)(uintptr_t camera);
static void* g_orig[PROBE_SLOTS];
static volatile LONG g_count[PROBE_SLOTS], g_all[PROBE_SLOTS];
static uintptr_t g_caller[PROBE_SLOTS];
static volatile uintptr_t g_graph; /* the player camera unit's scene graph, set each frame */
static ULONGLONG g_since;
static int g_lines;

static void Note(int slot, uintptr_t camera, void* caller)
{
    InterlockedIncrement(&g_all[slot]);
    uintptr_t graph = 0;
    if (g_graph == 0 || !fpv_read(camera + 0x18, &graph, sizeof graph) || graph != g_graph) return;
    InterlockedIncrement(&g_count[slot]);
    g_caller[slot] = (uintptr_t)caller;
}

#define PTR(i) static uintptr_t Detour##i(uintptr_t c) { Note(i, c, __builtin_return_address(0)); return ((PtrFn)g_orig[i])(c); }
#define FLT(i) static float Detour##i(uintptr_t c) { Note(i, c, __builtin_return_address(0)); return ((FloatFn)g_orig[i])(c); }
PTR(0) PTR(1) PTR(2) PTR(3) PTR(4) FLT(5) FLT(6)
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
        if (!fpv_exe_hook(kSlots[i], kPins[i], kDetours[i], &g_orig[i], 30))
            fpv_log("probe", "could not hook exe+0x%X", kSlots[i]);
}

void fp_probe_tick(void)
{
    FpvCameraStatus status;
    fpv_camera_status(&status);
    g_graph = status.unit ? fpv_unit_graph(fpv_engine_unit(status.unit)) : 0;
    if (!fp_view_active()) return;
    ULONGLONG now = GetTickCount64();
    if (g_since == 0) { g_since = now; return; }
    if (now - g_since < 1000 || g_lines >= 12) return;
    g_since = now;
    g_lines++;
    char text[900];
    size_t used = 0;
    for (int i = 0; i < PROBE_SLOTS; i++)
    {
        LONG n = InterlockedExchange(&g_count[i], 0), all = InterlockedExchange(&g_all[i], 0);
        uintptr_t rva;
        const char* module = Module(g_caller[i], &rva);
        used += (size_t)snprintf(text + used, sizeof text - used, " exe+0x%X: %ld of %ld%s%s+0x%llX;", kSlots[i], n, all,
            n ? " from " : "", n ? module : "", n ? (unsigned long long)rva : 0ull);
    }
    fpv_log("probe", "camera getters:%s", text);
}
#endif
