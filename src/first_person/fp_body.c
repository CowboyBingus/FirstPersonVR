/* The parts of the local Helldiver First Person hides while the view is at its eyes: its capes and helmets. A cape hangs
 * where the eye goes looking down while running back or sideways, and flips over the view in a dive; the helmet's brim
 * shows across the top of the view at First Person's wider field of view (live 2026-10-31). No eye placement clears
 * either, so they are hidden, the way Vanilla Plus Megapack's Hide Cape hides one (Match Your Colors,
 * myc_cloak.c).
 *
 * The capes: the local player's Helldiver (the player manager game.dll+0x3326468: player count +132, entity records
 * +232 + 8 * i with bit 0 of +20 set for this machine's player and its id +8, the map +208 from that id to its index,
 * the Helldiver +936 + 32 * index: a script API unit handle, not the engine's unit reference First Person follows, live
 * 0x137 against 0x400054) -> its entity (game.dll+0x346BF98's map +0xF22EC8, records +0xF32F18, 24
 * bytes: the avatar resource, the avatar id +8) -> its avatar record (game.dll+0x3326D20's map +248, records +5532272 +
 * 440 * index) -> three arrays of 10 unit references at +220, a helmet in slot 0 and a cape in slot 1 of each. Maps are open addressing u32
 * tables {data +0, capacity +8, empty key +12, multiplier +16}, probed as the game probes them.
 *
 * Hidden with the engine's set_unit_visibility (the script C API's Unit table, [game.dll+0x3326308]+24, slot +1624 =
 * exe+0x205680: one queued command the unit's world runs in its update); each of the cape's meshes then reads 0 in its
 * visibility mask (Mesh +0xDC). Every engine function is compared with its expected address once, before any call.
 *
 * A cape already hidden (Megapack's Hide Cape) is left alone and not shown again. While hidden, one cape's mask is read
 * a frame (round robin): the game shows every unit of a Helldiver again on the hellpod's exit, so one shown is hidden
 * again. Shown again when the view leaves first person, or the Helldiver changes (a corpse keeps its cape).
 *
 * Cost: one 1-byte read a frame while hidden; the chain (about ten reads) once per Helldiver.
 */
#include <string.h>
#include "fp.h"

#define BODY_SCRIPT_API 0x3326308u
#define BODY_UNIT_TABLE 24u
#define BODY_PLAYERS 0x3326468u
#define BODY_P_COUNT 132u
#define BODY_P_ENTITIES 232u
#define BODY_P_MAP 208u
#define BODY_P_UNITS 936u
#define BODY_MAX_PLAYERS 8u
#define BODY_ENTITIES 0x346BF98u
#define BODY_E_MAP 0xF22EC8u
#define BODY_E_RECORDS 0xF32F18u
#define BODY_AVATARS 0x3326D20u
#define BODY_A_MAP 248u
#define BODY_A_COUNT 0x6Cu
#define BODY_A_RECORDS 5532272u
#define BODY_A_RECORD_SIZE 440u
#define BODY_A_UNITS 220u
#define BODY_AVATAR_LOW 0x294DFA97u
#define BODY_AVATAR_HIGH 0x4D1C334Du
#define BODY_MESH_VISIBILITY 0xDCu
#define BODY_NO_UNIT 0xFFFFFFFFu
#define BODY_MAX_PROBES 64u
#define BODY_RETRY_FRAMES 30

static const struct
{
    const char* name;
    uint32_t slot, expected;
} kFunctions[] = {{"alive", 1824, 0x1FEEB0}, {"meshes", 424, 0x200CD0}, {"mesh", 440, 0x200D20},
    {"visibility", 1624, 0x205680}};

static struct
{
    int state;              /* 0 not tried, 1 ready, -1 off */
    FpBodyFns fns;
    uint32_t ref;           /* the Helldiver whose capes are below */
    int resolved;
    int wait;               /* frames before the chain is tried again */
    int hidden;
    int turn;
    int count;
    uint32_t units[FP_BODY_CAPES];
    uintptr_t meshes[FP_BODY_CAPES];
    int mine[FP_BODY_CAPES]; /* hidden by First Person (shown again on the way out) */
    int loggedChain;
} g;

void fp_body_reset(void)
{
    memset(&g, 0, sizeof g);
}

static uint32_t Le32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uintptr_t Pointer(uintptr_t address)
{
    uint64_t v = 0;
    if (!fpv_read(address, &v, sizeof v) || v < 0x10000 || v >= 0x800000000000ull) return 0;
    return (uintptr_t)v;
}

/* The value of key in the game's map at address. */
static int Lookup(uintptr_t address, uint32_t key, uint32_t* value)
{
    uint8_t b[20];
    if (!fpv_read(address, b, sizeof b)) return 0;
    uint64_t data = Le32(b) | (uint64_t)Le32(b + 4) << 32;
    uint32_t capacity = Le32(b + 8), empty = Le32(b + 12), start = key * Le32(b + 16);
    if (capacity == 0 || capacity > 4194304 || data < 0x10000) return 0;
    for (uint32_t i = 0; i < (capacity < BODY_MAX_PROBES ? capacity : BODY_MAX_PROBES); i++)
    {
        uint8_t slot[8];
        if (!fpv_read((uintptr_t)(data + 8 * (((uint64_t)start + i) % capacity)), slot, sizeof slot)) return 0;
        if (Le32(slot) == key) { *value = Le32(slot + 4); return 1; }
        if (Le32(slot) == empty) return 0;
    }
    return 0;
}

/* The local player's Helldiver (a script API unit handle), or why not. */
static const char* LocalHelldiver(uint32_t* unit)
{
    uintptr_t players = Pointer(fpv_host()->game_base + BODY_PLAYERS);
    uint32_t count = 0, index;
    if (players == 0 || !fpv_read(players + BODY_P_COUNT, &count, sizeof count) || count == 0 || count > BODY_MAX_PLAYERS)
        return "no players";
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t entity = Pointer(players + BODY_P_ENTITIES + 8 * i);
        uint8_t b[24];
        if (entity == 0 || !fpv_read(entity, b, sizeof b) || b[20] % 2 != 1) continue;
        if (!Lookup(players + BODY_P_MAP, Le32(b + 8), &index) || index >= BODY_MAX_PLAYERS) return "the player is not indexed";
        if (!fpv_read(players + BODY_P_UNITS + 32 * (uintptr_t)index, unit, sizeof *unit) || *unit == 0) return "no Helldiver";
        return NULL;
    }
    return "no local player";
}

/* The local Helldiver's 30 unit references (its avatar record's three arrays of 10), or why not. */
static const char* AvatarUnits(uint32_t* refs)
{
    uintptr_t game = fpv_host()->game_base;
    uint32_t ref;
    const char* why = game != 0 ? LocalHelldiver(&ref) : "game.dll was not found";
    if (why != NULL) return why;
    uintptr_t entities = game != 0 ? Pointer(game + BODY_ENTITIES) : 0, avatars = game != 0 ? Pointer(game + BODY_AVATARS) : 0;
    uint32_t entity, index, count;
    uint8_t e[24];
    if (entities == 0 || avatars == 0) return "no entity or avatar manager";
    if (!Lookup(entities + BODY_E_MAP, ref, &entity)) return "the Helldiver's entity is not found";
    if (!fpv_read(entities + BODY_E_RECORDS + 24 * (uintptr_t)entity, e, sizeof e) || Le32(e) != BODY_AVATAR_LOW ||
        Le32(e + 4) != BODY_AVATAR_HIGH)
        return "the Helldiver's entity is not an avatar";
    if (!Lookup(avatars + BODY_A_MAP, Le32(e + 8), &index) || !fpv_read(avatars + BODY_A_COUNT, &count, sizeof count) ||
        index >= count)
        return "the avatar is not indexed";
    uintptr_t at = avatars + BODY_A_RECORDS + (uintptr_t)BODY_A_RECORD_SIZE * index + BODY_A_UNITS;
    return fpv_read(at, refs, 30 * sizeof refs[0]) ? NULL : "the avatar's units do not read";
}

/* The local Helldiver's cape and helmet units (slots 0 and 1 of each array) and their first meshes; how many, or -1 with *why. (ref, the Helldiver First Person
 * follows, only tells when to look again.) */
static int Capes(uint32_t ref, uint32_t* units, uintptr_t* meshes, int max, const char** why)
{
    uint32_t refs[30];
    (void)ref;
    *why = AvatarUnits(refs);
    if (*why != NULL) return -1;
    int n = 0;
    for (int i = 0; i < 6 && n < max; i++)
    {
        uint32_t unit = refs[10 * (i / 2) + i % 2];
        if (unit == 0 || unit == BODY_NO_UNIT || g.fns.alive(unit) == 0) continue;
        units[n] = unit;
        meshes[n++] = g.fns.meshes(unit) > 0 ? g.fns.mesh(unit, 0) : 0;
    }
    return n;
}

static int MaskRead(uintptr_t mesh, uint8_t* mask)
{
    return mesh != 0 && fpv_read(mesh + BODY_MESH_VISIBILITY, mask, 1);
}

/* Why the engine's functions cannot be used (state -1), NULL when they can, or "" to try again later. */
static const char* Check(void)
{
    const FpvHost* host = fpv_host();
    if (host->game_base == 0 || host->exe_base == 0) return "game.dll was not found";
    uintptr_t script = Pointer(host->game_base + BODY_SCRIPT_API), table = script != 0 ? Pointer(script + BODY_UNIT_TABLE) : 0;
    if (table == 0) return "";
    void* fns[4];
    for (int i = 0; i < 4; i++)
    {
        uintptr_t fn = Pointer(table + kFunctions[i].slot);
        if (fn != host->exe_base + kFunctions[i].expected) return kFunctions[i].name;
        fns[i] = (void*)fn;
    }
    g.fns.alive = (uint8_t (*)(uint32_t))fns[0];
    g.fns.meshes = (uint32_t (*)(uint32_t))fns[1];
    g.fns.mesh = (uintptr_t (*)(uint32_t, uint32_t))fns[2];
    g.fns.visibility = (void (*)(uint32_t, uint32_t, uint8_t))fns[3];
    g.fns.capes = Capes;
    g.fns.mask = MaskRead;
    return NULL;
}

static int Ready(void)
{
    if (g.state != 0) return g.state == 1;
    const char* why = Check();
    if (why != NULL && *why == 0) return 0;
    g.state = why == NULL ? 1 : -1;
    if (why != NULL)
        fpv_log("body", "off (the engine's %s differs, build %s): the cape and helmet stay shown", why, FPV_GAME_BUILD);
    return g.state == 1;
}

#ifdef FPV_TEST
void fp_body_set_for_test(const FpBodyFns* fns)
{
    fp_body_reset();
    if (fns != NULL) g.fns = *fns;
    g.state = fns != NULL ? 1 : -1;
}
#endif

/* The capes First Person hid shown again (those still alive). */
static void Show(void)
{
    for (int i = 0; i < g.count; i++)
        if (g.mine[i] && g.fns.alive(g.units[i]) != 0) g.fns.visibility(g.units[i], 1, 1);
    if (g.hidden && g.count > 0) fpv_log("body", "the cape and helmet shown");
    g.hidden = 0;
    memset(g.mine, 0, sizeof g.mine);
}

/* The Helldiver's capes found (once per Helldiver; again after BODY_RETRY_FRAMES when the chain does not read). */
static int Resolve(void)
{
    if (g.resolved) return 1;
    if (g.wait > 0) { g.wait--; return 0; }
    const char* why = NULL;
    g.count = g.fns.capes(g.ref, g.units, g.meshes, FP_BODY_CAPES, &why);
    if (g.count <= 0)
    {
        if (g.count == 0) why = "it wears none yet";
        g.count = 0;
        g.wait = BODY_RETRY_FRAMES;
        fpv_log_once(&g.loggedChain, "body", "the cape and helmet are not found (%s): they stay shown", why);
        return 0;
    }
    g.resolved = 1;
    fpv_log("body", "the Helldiver (unit %u) wears %d cape and helmet unit%s", g.ref, g.count, g.count == 1 ? "" : "s");
    return 1;
}

static void Hide(void)
{
    int n = 0;
    for (int i = 0; i < g.count; i++)
    {
        uint8_t mask = 0;
        if (!g.fns.mask(g.meshes[i], &mask) || mask == 0) continue;  /* hidden already: not ours to show */
        g.fns.visibility(g.units[i], 0, 1);
        g.mine[i] = 1;
        n++;
    }
    g.hidden = 1;
    if (g.count > 0) fpv_log("body", "the cape and helmet hidden (%d of %d)", n, g.count);
}

/* One cape a frame: shown by something else (the hellpod's exit) while it should be hidden, hidden again. */
static void Check1(void)
{
    if (g.count == 0) return;
    g.turn = (g.turn + 1) % g.count;
    uint8_t mask = 0;
    int i = g.turn;
    if (!g.fns.mask(g.meshes[i], &mask) || mask == 0 || g.fns.alive(g.units[i]) == 0) return;
    g.fns.visibility(g.units[i], 0, 1);
    g.mine[i] = 1;
}

void fp_body_tick(uint32_t ref, int hide)
{
    if (ref != g.ref)
    {
        if (g.hidden) Show();
        g.ref = ref;
        g.resolved = 0;
        g.wait = 0;
        g.count = 0;
    }
    if (!hide || ref == 0)
    {
        if (g.hidden) Show();
        return;
    }
    if (!Ready() || !Resolve()) return;
    if (!g.hidden) Hide();
    else Check1();
}

int fp_body_hidden(void) { return g.hidden; }
