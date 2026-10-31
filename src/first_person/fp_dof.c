/* Depth of field off while the view is at the Helldiver's eyes. The game focuses it for its third-person camera, a few
 * metres out, so the weapon in first person (0.3-0.5 m from the eye) is always in its near blur (live 2026-10-31: with
 * dof_enabled off the weapon is sharp, standing and walking; with motion blur off instead it stays blurred). Arma, which
 * First Person's view follows, has none on the weapon either.
 *
 * The engine's render settings: Application.set_render_setting (the Lua binding exe+0x3F9CC0) passes a string value
 * to exe+0x10D880 (the renderer [[exe+0x23621A8]+0x338], unused, name, value), one queued command the render thread
 * parses (live: "false" turns depth of field off, "true" on). Application.render_setting reads the settings dictionary
 * [exe+0x1A102B8] (exe+0x4BB100: count +0, 24-byte entries +8 sorted by the name's IdString32 at +0x10, each a variant:
 * type +0, 4 a bool with its value at +8); read here the same way, without calling.
 *
 * Turned off on the way into first person when it is on, and back on on the way out (or when the mod unloads): only
 * when First Person turned it off. Checked once a second while in first person, so the game's options turning it on
 * again (Apply in the menu) are undone. Cost: one dictionary search (about ten reads) a second in first person.
 */
#include <string.h>
#include "fp.h"

#define DOF_RENDERER 0x23621A8u
#define DOF_RENDERER_FIELD 0x338u
#define DOF_SET 0x10D880u
#define DOF_SETTINGS 0x1A102B8u
#define DOF_ENTRY_SIZE 24u
#define DOF_TYPE_BOOL 4u
#define DOF_MAX_ENTRIES 65536u
#define DOF_CHECK_FRAMES 60

static const char kSet[] = "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 57 48 81 EC 90 00 00 00 48 8D B9 B0 10 00 00 0F 29";

static struct
{
    int state;          /* 0 not tried, 1 ready, -1 off */
    FpDofFns fns;
    int off;            /* First Person turned it off */
    int wait;
} g;

void fp_dof_reset(void)
{
    memset(&g, 0, sizeof g);
}

/* The setting's bool: 1 on, 0 off, -1 not found. */
static int Get(const char* name)
{
    uint64_t dict = 0, entries = 0;
    uint32_t count = 0, id = fpv_id32(name);
    if (!fpv_read(fpv_host()->exe_base + DOF_SETTINGS, &dict, sizeof dict) || dict < 0x10000 ||
        !fpv_read((uintptr_t)dict, &count, sizeof count) || count == 0 || count > DOF_MAX_ENTRIES ||
        !fpv_read((uintptr_t)dict + 8, &entries, sizeof entries) || entries < 0x10000) return -1;
    uint32_t lo = 0, hi = count;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2, key = 0, type = 0;
        uint8_t value = 0;
        uintptr_t at = (uintptr_t)entries + (uintptr_t)DOF_ENTRY_SIZE * mid;
        if (!fpv_read(at + 16, &key, sizeof key)) return -1;
        if (key < id) { lo = mid + 1; continue; }
        if (key > id) { hi = mid; continue; }
        if (!fpv_read(at, &type, sizeof type) || type != DOF_TYPE_BOOL || !fpv_read(at + 8, &value, 1)) return -1;
        return value != 0;
    }
    return -1;
}

static void Set(const char* name, const char* value)
{
    typedef void (*SetFn)(uintptr_t renderer, uintptr_t unused, const char* name, const char* value);
    uint64_t app = 0, renderer = 0;
    uintptr_t exe = fpv_host()->exe_base;
    if (!fpv_read(exe + DOF_RENDERER, &app, sizeof app) || app < 0x10000 ||
        !fpv_read((uintptr_t)app + DOF_RENDERER_FIELD, &renderer, sizeof renderer) || renderer < 0x10000) return;
    ((SetFn)(exe + DOF_SET))((uintptr_t)renderer, 0, name, value);
}

static int Ready(void)
{
    if (g.state != 0) return g.state == 1;
    if (fpv_host()->exe_base == 0) return 0;
    g.state = fpv_exe_matches(DOF_SET, kSet) ? 1 : -1;
    if (g.state == 1) { g.fns.get = Get; g.fns.set = Set; }
    else fpv_log("dof", "off (the engine's render setting differs, build %s): depth of field stays as the game has it",
        FPV_GAME_BUILD);
    return g.state == 1;
}

#ifdef FPV_TEST
void fp_dof_set_for_test(const FpDofFns* fns)
{
    fp_dof_reset();
    if (fns != NULL) g.fns = *fns;
    g.state = fns != NULL ? 1 : -1;
}
#endif

void fp_dof_tick(int firstPerson)
{
    if (!firstPerson)
    {
        if (g.off && Ready())
        {
            g.fns.set("dof_enabled", "true");
            fpv_log("dof", "depth of field on again");
        }
        g.off = 0;
        g.wait = 0;
        return;
    }
    if (g.wait-- > 0 || !Ready()) return;
    g.wait = DOF_CHECK_FRAMES;
    if (g.fns.get("dof_enabled") != 1) return;
    g.fns.set("dof_enabled", "false");
    if (!g.off) fpv_log("dof", "depth of field off in first person");
    g.off = 1;
}
