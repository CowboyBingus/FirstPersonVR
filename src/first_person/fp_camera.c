/* First Person's camera filter: the camera at the local Helldiver's eyes, facing where the game's camera faces.
 *
 * The Helldiver: the main world's unit of resource 0x4D1C334D294DFA97 nearest the game's camera and in front of it (the
 * camera follows the local player's Helldiver and looks over its shoulder, so a teammate standing behind the camera,
 * nearer than ours, is not taken; one behind counts only when none is in front; fpv_units.c has the layout). Kept by its place in the world's unit array and checked every
 * frame there (same Unit*, reference and resource), so a destroyed Helldiver is noticed without calling the engine with
 * a stale reference; looked for again every 30 frames while there is none, and while the one kept is farther from the
 * camera than on foot: a dead Helldiver's body stays in the world as the same unit, and the camera follows the
 * reinforced one (seen live: the view stayed third person after a reinforcement).
 *
 * The situation: on foot in a mission while the game's camera is within FP_ON_FOOT_RANGE of the head (its
 * over-the-shoulder camera); anything else (vehicles, the hellpod, cutscenes, the map) is not known yet and stays third
 * person. Outside a mission (the ship, the screen after a mission, whose camera can sit near the head) it is never on
 * foot.
 *
 * The eye is the head node plus the options' eye_forward along the view's heading (level: along the full view, looking
 * down sank the eye into the neck and shoulder armour) and eye_up (0.12 m and 0.06 m). Not along the body's facing:
 * the diver faces where it runs, so the view looked back across its own shoulder (all seen live). The eye goes further
 * out looking up (the helmet's brim) and looking down with the body turned away (its back and backpack). The
 * eye is steadied: the head's place relative to the Helldiver's root (node 0) is smoothed, so the run's bob and sway
 * are softened while the root carries the eye along at once. Measured live: jogging bobs the head 13-24 cm at about
 * 2 Hz; the height follows at FP_EYE_RATE_UP (a third of that bob is left), the rest at FP_EYE_RATE. Crouching or going
 * prone still lowers the eye within a fifth of a second. The view turns from there to where the game's own line of fire
 * hits (fp_aim.c), so the screen's centre is where shots land; without that, it keeps the game camera's direction.
 *
 * The game's own first-person aim (middle mouse while aiming: iron sights, scopes) puts its camera at the head (0.16 m
 * from it, live); while the game's camera is within FP_GAME_SIGHTS of the head, First Person steps aside and blends to
 * it, and comes back when the aim ends.
 *
 * Switching between the views (on foot or not, the player's toggle, a mod's request) blends over FP_BLEND_SECONDS: the
 * frame's pose is mixed between the game's and First Person's.
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
#define FP_EYE_RATE 10.0f       /* per second: how fast the steadied eye follows the head, sideways and fore and aft */
#define FP_EYE_RATE_UP 5.0f     /* and up and down */
#define FP_EYE_UP_OUT 0.15f     /* metres further out along the heading, looking straight up */
#define FP_EYE_AWAY_OUT 0.35f   /* and looking straight down with the body facing the other way */
#define FP_BLEND_SECONDS 0.25f
#define FP_GAME_SIGHTS 0.5f     /* the game's camera this near the head is its own first-person aim */

static struct
{
    uintptr_t world, unit;
    uint32_t index, ref;
    int32_t head;
    int wait;
    int away;            /* frames the Helldiver kept has been too far from the camera to be on foot */
    int active;          /* last frame's view was First Person's */
    float blend;         /* 0 the game's view .. 1 First Person's */
    FpvVec3 offset;      /* the steadied head, relative to the root */
    int loggedHead;
    int searches;        /* searches that found no Helldiver, logged for the first FP_SEARCHES_LOGGED */
    double last;         /* the last filtered frame's time */
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

/* 0: not a Helldiver; 1: a Helldiver whose root does not read; 2: one at *sq (squared metres) from the camera, *behind
 * set when it is behind the camera's view. */
static int AvatarDistance(uintptr_t unit, const float* camera, float* sq, int* behind)
{
    float root[16];
    if (unit == 0 || fpv_unit_resource(unit) != FP_AVATAR) return 0;
    if (!fpv_unit_world_pose(unit, 0, root)) return 1;
    FpvVec3 d = fpv_sub(fpv_v3(root[12], root[13], root[14]), fpv_pose_position(camera));
    *sq = fpv_dot(d, d);
    *behind = fpv_dot(d, fpv_pose_forward(camera)) < 0;
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

/* The search's best so far. */
typedef struct Best
{
    float score, sq, nearestSq;
    uint32_t avatars;
} Best;

/* Weighs the unit at index i against the best so far (g.unit, g.index). */
static void Weigh(uintptr_t unit, uint32_t i, const float* pose, Best* best)
{
    float sq = 0, range = FP_SEARCH_RANGE * FP_SEARCH_RANGE;
    int behind = 0, kind = AvatarDistance(unit, pose, &sq, &behind);
    best->avatars += kind != 0;
    if (kind != 2) return;
    if (best->nearestSq < 0 || sq < best->nearestSq) best->nearestSq = sq;
    float score = sq + (behind ? 2 * range : 0);  /* any in front before any behind */
    if (sq >= range || (g.unit != 0 && score >= best->score)) return;
    best->score = score;
    best->sq = sq;
    g.unit = unit;
    g.index = i;
}

static void Search(uintptr_t world, const float* pose)
{
    g.unit = 0;
    g.world = world;
    uintptr_t units;
    uint32_t count = fpv_world_units(world, &units);
    Best best = {0, 0, -1, 0};
    for (uint32_t i = 0; i < count; i++) Weigh(fpv_world_unit(units, i), i, pose, &best);
    uint32_t avatars = best.avatars;
    float bestSq = best.sq, nearestSq = best.nearestSq;
    FpvVec3 camera = fpv_pose_position(pose);
    if (g.unit != 0) { Take(bestSq); return; }
    if (g.searches++ < FP_SEARCHES_LOGGED)
        fpv_log("camera", "no Helldiver near the camera: %u units, %u Helldivers, the nearest %.1f m away (camera at %.1f %.1f %.1f)",
            count, avatars, nearestSq < 0 ? -1.0f : sqrtf(nearestSq), camera.x, camera.y, camera.z);
}

/* The head's and the root's world positions and the body's facing (the root's forward, level; 0 when it has none), or
 * 0 when they cannot be read. */
static int Head(FpvVec3* head, FpvVec3* root, FpvVec3* facing)
{
    float m[16], r[16];
    if (!fpv_unit_world_pose(g.unit, g.head, m) || !fpv_unit_world_pose(g.unit, 0, r)) return 0;
    *head = fpv_v3(m[12], m[13], m[14]);
    *root = fpv_v3(r[12], r[13], r[14]);
    *facing = fpv_v3(r[4], r[5], 0);
    if (!isfinite(facing->x) || !isfinite(facing->y) || !fpv_normalize(facing)) *facing = fpv_v3(0, 0, 0);
    return isfinite(head->x) && isfinite(head->y) && isfinite(head->z) && isfinite(root->x) && isfinite(root->y) &&
        isfinite(root->z);
}

/* Logs a switch, with why it is third person. */
static void Say(int active, int known, int mission, float distance)
{
    if (active == g.active) return;
    g.active = active;
    if (active) fpv_log("camera", "first person");
    else if (!known) fpv_log("camera", "third person: the Helldiver is not known");
    else if (!mission) fpv_log("camera", "third person: not in a mission");
    else if (distance >= FP_ON_FOOT_RANGE) fpv_log("camera", "third person: the game's camera is %.1f m from the head", distance);
    else fpv_log("camera", "third person: the player's setting or a mod's request");
}

/* Keeps the Helldiver found (looking again every FP_SEARCH_FRAMES while there is none): 1 while there is one. */
static int Keep(uintptr_t world, const float* pose)
{
    if (StillThere(world)) return 1;
    g.unit = 0;
    if (g.wait > 0) { g.wait--; return 0; }
    Search(world, pose);
    if (g.unit == 0) g.wait = FP_SEARCH_FRAMES;
    return g.unit != 0;
}

static float Follow(float rate, float dt) { return 1.0f - expf(-rate * dt); }

/* First Person's pose for this frame, from the game's: the steadied eye, looking where the game aims. */
static void FirstPersonPose(const float* game, FpvVec3 head, FpvVec3 root, FpvVec3 facing, int fresh, float dt,
    float* out)
{
    FpvVec3 offset = fpv_sub(head, root);
    if (fresh) g.offset = offset;
    else
    {
        float k = Follow(FP_EYE_RATE, dt);
        g.offset.x += (offset.x - g.offset.x) * k;
        g.offset.y += (offset.y - g.offset.y) * k;
        g.offset.z += (offset.z - g.offset.z) * Follow(FP_EYE_RATE_UP, dt);
    }
    FpvVec3 steady = fpv_add(root, g.offset), forward = fpv_pose_forward(game), aimed;
    FpvVec3 heading = fpv_v3(forward.x, forward.y, 0);
    if (!fpv_normalize(&heading)) heading = facing;
    /* Further out looking up (the helmet's brim hangs over the eye), and looking down while the body faces away from
     * the view: after a quick turn or running back towards the camera the back and the backpack were under the eye,
     * strafing or running diagonally the shoulder and arm (all seen live). Away: 0 facing the view, a half at 45
     * degrees, 1 at right angles and beyond. */
    float along = fpv_dot(facing, heading);
    float away = fpv_dot(facing, facing) > 0 ? 1 - (along > 0 ? along * along : 0) : 0;
    float ahead = fp_options()->eye_forward + FP_EYE_UP_OUT * (forward.z > 0 ? forward.z : 0) +
        FP_EYE_AWAY_OUT * away * (forward.z < 0 ? -forward.z : 0);
    FpvVec3 eye = fpv_add(steady, fpv_scale(heading, ahead));
    eye.z += fp_options()->eye_up;
    if (fp_aim_forward(game, eye, steady, dt, &aimed) && fpv_pose_look(game, eye, aimed, fpv_v3(0, 0, 1), out)) return;
    memcpy(out, game, 16 * sizeof(float));
    out[9] = eye.x;
    out[10] = eye.y;
    out[11] = eye.z;
}

/* The blend moved a frame towards the view in use. */
static float Blend(float blend, int active, float dt)
{
    float step = dt / FP_BLEND_SECONDS;
    if (dt <= 0) step = 1;  /* the first frame: no time yet, no blend */
    blend += active ? step : -step;
    return blend < 0 ? 0 : blend > 1 ? 1 : blend;
}

/* On foot: in a mission, with the camera within FP_ON_FOOT_RANGE of the head (d: head - camera). A Helldiver kept but
 * too far for FP_SEARCH_FRAMES is let go, so the next frame looks again. */
static int OnFoot(int known, int mission, FpvVec3 d)
{
    int close = fpv_dot(d, d) < FP_ON_FOOT_RANGE * FP_ON_FOOT_RANGE;
    g.away = known && !close ? g.away + 1 : 0;
    if (g.away >= FP_SEARCH_FRAMES)
    {
        g.unit = 0;
        g.away = 0;
    }
    return known && mission && close;
}

int fp_camera_filter(float* pose, void* context)
{
#ifdef FPV_PROBE
    fp_probe_tick();
#endif
    fp_options_tick();
    double now = fpv_seconds();
    float dt = g.last > 0 && now > g.last ? (float)(now - g.last) : 0;
    if (dt > 0.1f) dt = 0.1f;  /* a hitch, or the first frame back: no big jumps */
    g.last = now;
    FpvVec3 camera = fpv_pose_position(pose), head, root, facing;
    int known = Keep(fpv_camera_world(), pose) && Head(&head, &root, &facing);
    FpvVec3 d = known ? fpv_sub(head, camera) : fpv_v3(0, 0, 0);
    int mission = fpv_in_mission();
    int onFoot = OnFoot(known, mission, d);
    fp_view_set_situation(onFoot ? FP_SITUATION_ON_FOOT : 0);
    int active = known && fp_view_active();
    Say(active, known, mission, sqrtf(fpv_dot(d, d)));
    int sights = fpv_dot(d, d) < FP_GAME_SIGHTS * FP_GAME_SIGHTS;
    int fresh = g.blend <= 0;
    g.blend = known ? Blend(g.blend, active && !sights, dt) : 0;
    if (g.blend <= 0) return 0;
    float game[16], view[16];
    memcpy(game, pose, sizeof game);
    FirstPersonPose(game, head, root, facing, fresh, dt, view);
    float t = g.blend * g.blend * (3 - 2 * g.blend);
    if (t >= 1 || !fpv_pose_blend(game, view, t, pose)) memcpy(pose, view, sizeof view);
    return 1;
}
