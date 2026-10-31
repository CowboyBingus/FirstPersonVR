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
 * world's units runs only while the camera unit is not known (then every 30 frames until found).
 */
#include <math.h>
#include <string.h>
#include "fpv.h"

#define CAM_RENDER_STEP 0x689BC0u
#define CAM_SET_LOCAL_POSE 0x1FCAD0u
#define CAM_UPDATE_UNIT 0x1B2DC0u
#define CAM_UNIT 0x9D8C0u
#define CAM_MAIN_WORLD 0x317DE0u
#define CAM_WORLD_RENDER_POSE 0x230920u
#define CAM_RETRY_FRAMES 30       /* a search that found nothing waits this long before the next */

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
static int g_wait;    /* frames until the next search, after one found nothing */
static float g_nearest = -1;
static uint32_t g_frames;
static int g_loggedFound, g_loggedMissing, g_loggedLost;
static uintptr_t g_frameWorld;
/* A status line every CAM_STATUS_FRAMES frames, the first CAM_STATUS_LINES times: what the hook did since the last. */
#define CAM_STATUS_FRAMES 900
#define CAM_STATUS_LINES 12
static struct { uint32_t frames, noWorld, finds, found, noUnit, noGraph, noPoses, filtered, lines; } g_diag;

static void Status(uintptr_t world)
{
    if (++g_diag.frames < CAM_STATUS_FRAMES || g_diag.lines >= CAM_STATUS_LINES) return;
    g_diag.lines++;
    fpv_log("camera", "status: %u frames, world %p, %u without a world, %u searches (%u found), camera %u: %u unit misses, "
        "%u graph misses, %u pose misses, %u filtered", g_diag.frames, (void*)world, g_diag.noWorld, g_diag.finds, g_diag.found,
        g_ref, g_diag.noUnit, g_diag.noGraph, g_diag.noPoses, g_diag.filtered);
    uint32_t lines = g_diag.lines;
    memset(&g_diag, 0, sizeof g_diag);
    g_diag.lines = lines;
}

/* The unit's scene graph local poses (node 0 first); 0 when they cannot be read. */
static uintptr_t Poses(uintptr_t unit)
{
    uintptr_t sg = fpv_unit_graph(unit), poses = 0;
    return sg != 0 && fpv_read(sg + 0x20, &poses, sizeof poses) ? poses : 0;
}

/* The squared distance from a unit with a camera to the render pose's position; 0 when the unit has no camera or its
 * pose cannot be read. */
static int CameraDistanceSq(uintptr_t unit, const float* render, float* sq)
{
    uintptr_t poses;
    if (fpv_unit_cameras(unit) <= 0 || (poses = Poses(unit)) == 0) return 0;
    float pose[12];
    if (!fpv_read(poses, pose, sizeof pose)) return 0;
    float dx = pose[9] - render[12], dy = pose[10] - render[13], dz = pose[11] - render[14];
    *sq = dx * dx + dy * dy + dz * dz;
    return 1;
}

/* The main world's camera unit: its one unit with a camera (seen live on the ship and in missions), or with several the
 * one nearest the world's last render pose. 0 when none is; g_nearest is that one's distance to the render pose. */
static uint32_t Find(uintptr_t world)
{
    g_nearest = -1;
    uintptr_t units;
    uint32_t count = fpv_world_units(world, &units);
    float render[16];
    if (count == 0 || !fpv_read(world + CAM_WORLD_RENDER_POSE, render, sizeof render)) return 0;
    uint32_t best = 0;
    float bestSq = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t unit = fpv_world_unit(units, i);
        float sq;
        uint32_t ref;
        if (!CameraDistanceSq(unit, render, &sq) || (ref = fpv_unit_ref(unit)) == 0) continue;
        if (bestSq < 0 || sq < bestSq) { best = ref; bestSq = sq; }
    }
    if (bestSq >= 0) g_nearest = sqrtf(bestSq);
    return best;
}

static void Apply(void)
{
    if (g_filter == NULL) return;
    uintptr_t world = g_world();
    g_frameWorld = world;
    Status(world);
    if (world == 0) { g_diag.noWorld++; return; }
    if (world != g_refWorld) { g_ref = 0; g_wait = 0; g_refWorld = world; }
    if (g_ref == 0)
    {
        if (g_wait > 0) { g_wait--; return; }
        g_diag.finds++;
        if ((g_ref = Find(world)) == 0)
        {
            g_wait = CAM_RETRY_FRAMES;
            fpv_log_once(&g_loggedMissing, "camera", "no camera unit in the main world yet (said once)");
            return;
        }
        g_diag.found++;
        fpv_log_once(&g_loggedFound, "camera", "found the camera unit (%u, %.2f m from the render pose)", g_ref, g_nearest);
    }
    uintptr_t unit = g_unit(g_ref), poses = Poses(unit);
    float game[16], pose[16];
    if (unit == 0) g_diag.noUnit++;
    else if (fpv_unit_graph(unit) == 0) g_diag.noGraph++;
    if (poses == 0 || !fpv_read(poses, game, sizeof game))
    {
        g_diag.noPoses++;
        fpv_log_once(&g_loggedLost, "camera", "the camera unit %u has no readable pose (said once)", g_ref);
        g_ref = 0; /* looked for again next frame */
        return;
    }
    memcpy(pose, game, sizeof pose);
    g_diag.filtered++;
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

uintptr_t fpv_camera_world(void) { return g_frameWorld; }

void fpv_camera_forget(void)
{
    g_ref = 0;
    g_refWorld = 0;
    g_wait = 0;
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
    g_loggedFound = g_loggedMissing = g_loggedLost = 0;
    g_frameWorld = 0;
    memset(&g_diag, 0, sizeof g_diag);
    fpv_camera_forget();
}
#endif
