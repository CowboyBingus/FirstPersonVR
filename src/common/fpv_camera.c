/* The camera override: build 25480438's script render step, hooked, so a filter can move the camera for the frame.
 *
 * Found for HellForge's terminal camera (HellForge native/mod-loader/terminal_camera.c, which hooks the same step; the
 * hooks chain, the mods' first): the camera is a unit's, the main world's unit with a camera whose pose is the
 * world's last render pose (world + 0x230920, a row-major 4x4). The game's camera code sets that unit's pose every
 * frame before the engine's script render step (exe+0x689BC0), and the render copies the camera unit's world pose to the
 * world's render pose. A pose set before the step is the one the frame renders with. So before the step the camera
 * unit's local pose (its scene graph's node 0, at +0x20) is read, filtered, and set with the engine's own
 * set_local_pose (exe+0x1FCAD0) and update_unit (exe+0x1B2DC0).
 *
 * The pose is the frame's only: the game sets its own again before the next frame, so the game's aim, audio listener
 * and logic still see the game's camera. (Making aim agree with a moved camera is the first-person feature's job.)
 *
 * Cost per frame with no filter: one branch. With one: a world call, two reads and the filter; the search over the
 * world's units runs only while the camera unit is not known (once per world, at most 30 frames).
 */
#include <math.h>
#include <string.h>
#include "fpv.h"

#define CAM_RENDER_STEP 0x689BC0u
#define CAM_SET_LOCAL_POSE 0x1FCAD0u
#define CAM_UPDATE_UNIT 0x1B2DC0u
#define CAM_UNIT 0x9D8C0u
#define CAM_MAIN_WORLD 0x317DE0u
#define CAM_WORLD_UNITS 0x2305B8u /* uint32 count, then (at +8) the Unit* array */
#define CAM_WORLD_RENDER_POSE 0x230920u
#define CAM_UNIT_CAMERAS 0x160u   /* unit vtable: int num_cameras(unit) */
#define CAM_UNIT_GRAPH 0xE8u      /* unit vtable: scene graph* (unit) */
#define CAM_SEARCH_FRAMES 30

/* The functions' first 32 bytes on build 25480438 (as HellForge's terminal camera pins them). */
static const char kStep[] = "48 83 EC 58 33 C0 4C 8D 05 EB 71 00 01 48 89 44 24 30 48 8D 54 24 40 89 44 24 38 48 81 C1 90 01";
static const char kSetPose[] = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B D8 48 63 FA E8 D6 0D EA FF 48 8B C8 48 8B F0";
static const char kUpdate[] = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 41 55 41 56 41 57 48 8D AC 24 30 F8 FF FF";
static const char kUnit[] = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 35 1A 28 97 01 8B D9 48 8D 8E D0 00 00 00 FF";
static const char kWorld[] = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 48 8B 05 12 84 6F 01 33 C9 0F 57 C0 8B D1 8B D9 44";

typedef void (*StepFn)(void* app);
typedef void (*SetPoseFn)(uint32_t unit, int32_t node, const float* pose);
typedef void (*UpdateFn)(uintptr_t world, uintptr_t unit);
typedef uintptr_t (*UnitFn)(uint32_t unit);
typedef uintptr_t (*WorldFn)(void);
typedef int (*CamerasFn)(uintptr_t unit);
typedef uintptr_t (*GraphFn)(uintptr_t unit);

static StepFn g_step;
static SetPoseFn g_setPose;
static UpdateFn g_update;
static UnitFn g_unit;
static WorldFn g_world;
static int g_installed;
static FpvCameraFilter g_filter;
static void* g_context;
static uint32_t g_ref;
static uintptr_t g_refWorld;  /* the world g_ref was found in */
static int g_misses;
static float g_nearest = -1;
static uint32_t g_frames;
static int g_loggedFound, g_loggedMissing;

/* The unit's scene graph poses (node 0 first); 0 when they cannot be read. */
static uintptr_t Poses(uintptr_t unit)
{
    uintptr_t table = 0, graph = 0, poses = 0;
    if (unit == 0 || !fpv_read(unit, &table, sizeof table) || !fpv_read(table + CAM_UNIT_GRAPH, &graph, sizeof graph) ||
        graph == 0) return 0;
    uintptr_t sg = ((GraphFn)graph)(unit);
    return sg != 0 && fpv_read(sg + 0x20, &poses, sizeof poses) ? poses : 0;
}

/* The squared distance from a unit with a camera to the render pose's position; 0 when the unit has no camera or its
 * pose cannot be read. */
static int CameraDistanceSq(uintptr_t unit, const float* render, float* sq)
{
    uintptr_t table = 0, cameras = 0, poses;
    if (unit == 0 || !fpv_read(unit, &table, sizeof table) ||
        !fpv_read(table + CAM_UNIT_CAMERAS, &cameras, sizeof cameras) || cameras == 0) return 0;
    if (((CamerasFn)cameras)(unit) <= 0 || (poses = Poses(unit)) == 0) return 0;
    float pose[12];
    if (!fpv_read(poses, pose, sizeof pose)) return 0;
    float dx = pose[9] - render[12], dy = pose[10] - render[13], dz = pose[11] - render[14];
    *sq = dx * dx + dy * dy + dz * dz;
    return 1;
}

/* The main world's unit with a camera nearest the world's last render pose, within a metre (the pose is a frame old).
 * 0 when none is; g_nearest is the closest distance seen. */
static uint32_t Find(uintptr_t world)
{
    g_nearest = -1;
    uint32_t count = 0;
    uintptr_t units = 0;
    float render[16];
    if (!fpv_read(world + CAM_WORLD_UNITS, &count, sizeof count) ||
        !fpv_read(world + CAM_WORLD_UNITS + 8, &units, sizeof units) ||
        !fpv_read(world + CAM_WORLD_RENDER_POSE, render, sizeof render) || count > 100000 || units == 0) return 0;
    uint32_t best = 0;
    float bestSq = 1.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t unit = 0;
        float sq;
        if (!fpv_read(units + 8 * (uintptr_t)i, &unit, sizeof unit) || !CameraDistanceSq(unit, render, &sq)) continue;
        if (g_nearest < 0 || sq < g_nearest * g_nearest) g_nearest = sqrtf(sq);
        uint32_t ref = 0;
        if (sq < bestSq && fpv_read(unit + 8, &ref, sizeof ref) && ref != 0) { best = ref; bestSq = sq; }
    }
    return best;
}

static void Apply(void)
{
    if (g_filter == NULL) return;
    uintptr_t world = g_world();
    if (world == 0) return;
    if (world != g_refWorld) { g_ref = 0; g_misses = 0; g_refWorld = world; }
    if (g_ref == 0)
    {
        if (g_misses >= CAM_SEARCH_FRAMES) return; /* given up for this world */
        if ((g_ref = Find(world)) == 0)
        {
            if (++g_misses == CAM_SEARCH_FRAMES)
                fpv_log_once(&g_loggedMissing, "camera", "no camera unit at the render pose (the nearest %.2f m away)",
                    g_nearest);
            return;
        }
        fpv_log_once(&g_loggedFound, "camera", "found the camera unit (%u)", g_ref);
    }
    uintptr_t unit = g_unit(g_ref), poses = Poses(unit);
    float game[16], pose[16];
    if (poses == 0 || !fpv_read(poses, game, sizeof game)) { g_ref = 0; return; } /* looked for again next frame */
    memcpy(pose, game, sizeof pose);
    if (!g_filter(pose, g_context)) return;
    g_setPose(g_ref, 0, pose);
    g_update(world, unit);
    g_frames++;
}

/* Before the step: the boot script's render() renders the world, which takes the camera unit's pose then. */
static void StepDetour(void* app)
{
    Apply();
    g_step(app);
}

int fpv_camera_install(int32_t priority)
{
    if (g_installed) return g_installed == 1;
    g_installed = -1;
    const FpvHost* host = fpv_host();
    if (host->exe_base == 0)
    {
        fpv_log("camera", "off: helldivers2.exe was not found");
        return 0;
    }
    if (!fpv_exe_matches(CAM_SET_LOCAL_POSE, kSetPose) || !fpv_exe_matches(CAM_UPDATE_UNIT, kUpdate) ||
        !fpv_exe_matches(CAM_UNIT, kUnit) || !fpv_exe_matches(CAM_MAIN_WORLD, kWorld))
    {
        fpv_log("camera", "off: the engine does not match build %s", FPV_GAME_BUILD);
        return 0;
    }
    g_setPose = (SetPoseFn)(host->exe_base + CAM_SET_LOCAL_POSE);
    g_update = (UpdateFn)(host->exe_base + CAM_UPDATE_UNIT);
    g_unit = (UnitFn)(host->exe_base + CAM_UNIT);
    g_world = (WorldFn)(host->exe_base + CAM_MAIN_WORLD);
    if (!fpv_exe_hook(CAM_RENDER_STEP, kStep, (void*)StepDetour, (void**)&g_step, priority))
    {
        fpv_log("camera", "off: the render step could not be hooked");
        return 0;
    }
    g_installed = 1;
    fpv_log("camera", "the render step is hooked");
    return 1;
}

void fpv_camera_set_filter(FpvCameraFilter filter, void* context)
{
    g_context = context;
    g_filter = filter;
}

uintptr_t fpv_engine_unit(uint32_t unit) { return g_unit != NULL && unit != 0 ? g_unit(unit) : 0; }

void fpv_camera_forget(void)
{
    g_ref = 0;
    g_refWorld = 0;
    g_misses = 0;
}

void fpv_camera_status(FpvCameraStatus* out)
{
    if (out == NULL) return;
    out->installed = g_installed;
    out->unit = g_ref;
    out->nearest = g_nearest;
    out->frames = g_frames;
}

#ifdef FPV_TEST
void (*fpv_camera_install_for_test(const FpvEngineFns* fns))(void* app)
{
    fpv_camera_reset_for_test();
    g_step = fns->step;
    g_setPose = fns->set_local_pose;
    g_update = fns->update_unit;
    g_unit = fns->unit;
    g_world = fns->main_world;
    g_installed = 1;
    return StepDetour;
}

void fpv_camera_reset_for_test(void)
{
    g_step = NULL;
    g_setPose = NULL;
    g_update = NULL;
    g_unit = NULL;
    g_world = NULL;
    g_installed = 0;
    g_filter = NULL;
    g_context = NULL;
    g_frames = 0;
    g_nearest = -1;
    g_loggedFound = g_loggedMissing = 0;
    fpv_camera_forget();
}
#endif
