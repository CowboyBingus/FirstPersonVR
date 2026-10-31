/* The mod's entry point and its one HfApi seam: hellforge_mod_init checks the SDK and the game build, fills FpvHost and
 * installs the camera hook. This is the only file in the mod that includes hellforge_mod.h; every feature goes through
 * fpv.h.
 *
 * G0 (the scaffold) sets no camera filter, so nothing here changes the game: the render step's hook is one branch a
 * frame until a feature sets a filter.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fpv.h"
#include "hellforge_mod.h"

static const HfApi* g_api;
static HfMod* g_mod;
static FpvHost g_host;

const FpvHost* fpv_host(void) { return &g_host; }
#ifdef FPV_TEST
FpvHost* fpv_host_for_test(void) { return &g_host; }
#endif

/* ---- logging ----------------------------------------------------------------------------------------------------- */

static void HostLog(const char* feature, const char* text)
{
    if (g_api == NULL || g_mod == NULL) return;
    g_api->log(g_mod, "[%s] %s", feature, text); /* lint-ok: C4 callers log only at start-up or once per world */
}

static void LogV(const char* feature, const char* format, va_list args)
{
    char text[1024];
    vsnprintf(text, sizeof text, format, args);
    if (g_host.log != NULL) g_host.log(feature, text);
}

void fpv_log(const char* feature, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(feature, format, args);
    va_end(args);
}

void fpv_log_once(int* logged, const char* feature, const char* format, ...)
{
    if (logged == NULL || *logged) return;
    *logged = 1;
    va_list args;
    va_start(args, format);
    LogV(feature, format, args);
    va_end(args);
}

/* ---- host wrappers ----------------------------------------------------------------------------------------------- */

static int HostRead(uintptr_t address, void* out, size_t size)
{
    return g_api != NULL && g_api->read(address, out, size) == 0;
}

static int HostCodeMatches(uintptr_t address, const char* pattern)
{
    return g_api != NULL && g_api->code_matches(address, pattern) != 0;
}

static int HostHook(uintptr_t target, const char* shipped, void* detour, void** original)
{
    if (g_api == NULL || original == NULL) return 0;
    if (g_api->code_matches(target, shipped) == 0) return 0;
    return g_api->hook_ex(g_mod, target, detour, original, 0) == 0;
}

int fpv_read(uintptr_t address, void* out, size_t size)
{
    return g_host.read != NULL && address != 0 && g_host.read(address, out, size);
}

int fpv_exe_matches(uint32_t rva, const char* pattern)
{
    if (g_host.code_matches == NULL || g_host.exe_base == 0 || rva >= g_host.exe_size) return 0;
    return g_host.code_matches(g_host.exe_base + rva, pattern);
}

int fpv_exe_hook(uint32_t rva, const char* shipped, void* detour, void** original)
{
    if (g_host.hook == NULL || g_host.exe_base == 0 || rva >= g_host.exe_size || original == NULL) return 0;
    return g_host.hook(g_host.exe_base + rva, shipped, detour, original);
}

/* ---- init -------------------------------------------------------------------------------------------------------- */

static void ClearHost(void)
{
    memset(&g_host, 0, sizeof g_host);
    g_api = NULL;
    g_mod = NULL;
}

/* The first HfApi member this mod needs that the loader's table is too short for, or NULL. */
static const char* MissingMember(const HfApi* api)
{
#define FPV_NEED(member) \
    if (api->size < offsetof(HfApi, member) + sizeof(api->member) || api->member == NULL) return #member
    FPV_NEED(build_id);
    FPV_NEED(module_base);
    FPV_NEED(read);
    FPV_NEED(code_matches);
    FPV_NEED(hook_ex);
#undef FPV_NEED
    return NULL;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    ClearHost();
    if (api == NULL || mod == NULL) return 1;
    if (api->version < 13u) return 1;
    if (api->size < offsetof(HfApi, log) + sizeof(api->log)) return 1;

    g_api = api;
    g_mod = mod;
    g_host.log = HostLog;

    const char* missing = MissingMember(api);
    if (missing != NULL)
    {
        fpv_log("mod", "HfApi has no %s: this mod needs mod SDK 13", missing);
        ClearHost();
        return 1;
    }
    const char* build = api->build_id();
    if (build == NULL || strcmp(build, FPV_GAME_BUILD) != 0)
    {
        fpv_log("mod", "needs game build %s; this is %s", FPV_GAME_BUILD, build != NULL ? build : "unknown");
        ClearHost();
        return 1;
    }
    g_host.read = HostRead;
    g_host.code_matches = HostCodeMatches;
    g_host.hook = HostHook;
    g_host.exe_base = api->module_base("helldivers2.exe", &g_host.exe_size);

    fpv_camera_install();
    fpv_log("mod", "%s %s loaded for game build %s (mod SDK %u)", FPV_MOD_NAME, FPV_MOD_VERSION, FPV_GAME_BUILD,
        api->version);
    return 0;
}
