/* First Person's camera filter: the camera at the local Helldiver's eyes, facing where the game's camera faces.
 *
 * The Helldiver: the main world's unit of resource 0x4D1C334D294DFA97 nearest the game's camera (the camera follows the
 * local player's Helldiver; fpv_units.c has the layout). Kept by its place in the world's unit array and checked every
 * frame there (same Unit*, reference and resource), so a destroyed Helldiver is noticed without calling the engine with
 * a stale reference; looked for again every 30 frames while there is none.
 *
 * The situation: on foot while the game's camera is within FP_ON_FOOT_RANGE of the head (its over-the-shoulder camera);
 * anything else (vehicles, the hellpod, cutscenes, the map) is not known yet and stays third person.
 *
 * The view keeps the game camera's rotation, so the direction the game aims along is the one on screen; only the
 * position moves, to the head node plus FP_EYE_FORWARD along the view and FP_EYE_UP.
 *
 * Cost per frame: about ten reads; the search (every unit's resource) only while no Helldiver is known.
 */
#include <math.h>
#include <string.h>
#include "fp.h"

#define FP_AVATAR 0x4D1C334D294DFA97ull
#define FP_SEARCH_FRAMES 30
#define FP_SEARCH_RANGE 12.0f   /* a Helldiver farther from the camera is someone else's */
#define FP_ON_FOOT_RANGE 6.0f
#define FP_EYE_FORWARD 0.12f
#define FP_EYE_UP 0.06f

static struct
{
    uintptr_t world, unit;
    uint32_t index, ref;
    int32_t head;
    int wait;
    int active;          /* last frame's view was First Person's */
    int loggedHead;
    int searches;        /* searches that found no Helldiver, logged for the first FP_SEARCHES_LOGGED */
} g;

#define FP_SEARCHES_LOGGED 5

void fp_camera_reset(void)
{
    memset(&g, 0, sizeof g);
}

/* Still the Helldiver we found: the world's array holds the same Unit* at the same place, with the same reference and
 * resource. */
static int StillThere(uintptr_t world)
{
    uintptr_t units;
    uint32_t count = fpv_world_units(world, &units);
    return g.unit != 0 && world == g.world && g.index < count && fpv_world_unit(units, g.index) == g.unit &&
        fpv_unit_ref(g.unit) == g.ref && fpv_unit_resource(g.unit) == FP_AVATAR;
}

/* 0: not a Helldiver; 1: a Helldiver whose root does not read; 2: one at *sq (squared metres) from the camera. */
static int AvatarDistance(uintptr_t unit, FpvVec3 camera, float* sq)
{
    float root[16];
    if (unit == 0 || fpv_unit_resource(unit) != FP_AVATAR) return 0;
    if (!fpv_unit_world_pose(unit, 0, root)) return 1;
    FpvVec3 d = fpv_sub(fpv_v3(root[12], root[13], root[14]), camera);
    *sq = fpv_dot(d, d);
    return 2;
}

/* The Helldiver found: its reference and head node, or no Helldiver after all when it has no head. */
static void Take(float distanceSq)
{
    g.ref = fpv_unit_ref(g.unit);
    g.head = fpv_unit_node(g.unit, fpv_id32("head"));
    if (g.head < 0) { fpv_log_once(&g.loggedHead, "camera", "the Helldiver has no head node"); g.unit = 0; return; }
    fpv_log("camera", "the Helldiver: unit %u, head node %d, %.2f m from the camera", g.ref, g.head, sqrtf(distanceSq));
}

static void Search(uintptr_t world, FpvVec3 camera)
{
    g.unit = 0;
    g.world = world;
    uintptr_t units;
    uint32_t count = fpv_world_units(world, &units), avatars = 0;
    float bestSq = FP_SEARCH_RANGE * FP_SEARCH_RANGE, nearestSq = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t unit = fpv_world_unit(units, i);
        float sq = 0;
        int kind = AvatarDistance(unit, camera, &sq);
        avatars += kind != 0;
        if (kind != 2) continue;
        if (nearestSq < 0 || sq < nearestSq) nearestSq = sq;
        if (sq >= bestSq) continue;
        bestSq = sq;
        g.unit = unit;
        g.index = i;
    }
    if (g.unit != 0) { Take(bestSq); return; }
    if (g.searches++ < FP_SEARCHES_LOGGED)
        fpv_log("camera", "no Helldiver near the camera: %u units, %u Helldivers, the nearest %.1f m away (camera at %.1f %.1f %.1f)",
            count, avatars, nearestSq < 0 ? -1.0f : sqrtf(nearestSq), camera.x, camera.y, camera.z);
}

/* The head's world position, or 0 when it cannot be read. */
static int Head(FpvVec3* out)
{
    float m[16];
    if (!fpv_unit_world_pose(g.unit, g.head, m) || !isfinite(m[12]) || !isfinite(m[13]) || !isfinite(m[14])) return 0;
    *out = fpv_v3(m[12], m[13], m[14]);
    return 1;
}

static void Say(int active)
{
    if (active == g.active) return;
    g.active = active;
    fpv_log("camera", active ? "first person" : "third person");
}

int fp_camera_filter(float* pose, void* context)
{
    uintptr_t world = fpv_camera_world();
    FpvVec3 camera = fpv_pose_position(pose), head;
    if (!StillThere(world))
    {
        g.unit = 0;
        if (g.wait > 0) g.wait--;
        else Search(world, camera);
        if (g.unit == 0) g.wait = FP_SEARCH_FRAMES;
    }
    if (g.unit == 0 || !Head(&head))
    {
        fp_view_set_situation(0);
        Say(0);
        return 0;
    }
    FpvVec3 d = fpv_sub(head, camera);
    fp_view_set_situation(fpv_dot(d, d) < FP_ON_FOOT_RANGE * FP_ON_FOOT_RANGE ? FP_SITUATION_ON_FOOT : 0);
    int active = fp_view_active();
    Say(active);
    if (!active) return 0;
    FpvVec3 forward = fpv_pose_forward(pose), eye = fpv_add(head, fpv_scale(forward, FP_EYE_FORWARD));
    eye.z += FP_EYE_UP;
    pose[9] = eye.x;
    pose[10] = eye.y;
    pose[11] = eye.z;
    return 1;
}
