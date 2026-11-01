/* The ready pose: in first person the Helldiver's weapon held up in the view, as a soldier carries a rifle at the ready
 * (Arma's raised weapon), for the frame's render only.
 *
 * Why: the game's hip-fire pose holds the weapon at the belt, its hands 0.40-0.47 m below the eye and 0.02-0.19 m in front
 * of it (live 2026-10-31): 65-90 degrees below the line of sight, where no field of view a screen can show reaches. Only
 * the game's aim (right mouse) brings the weapon up to the shoulder. So while hip firing, the hands and the weapon are
 * moved together (rigidly, so the hands stay on it) to the ready place, the lower right of the view, the barrel turned
 * to where the shots land; both arms then reach them (two-bone IK: the upper arm turns about the shoulder, the forearm
 * about the new elbow, each keeping its length and its elbow's side).
 *
 * The Helldiver's skeleton (resource 0x4D1C334D294DFA97, live): l_shoulder 44 -> l_elbow 46 -> l_hand 47 (attach_hand_l
 * 48), r_shoulder 67 -> r_elbow 69 -> r_hand 71 (attach_hand_r 84, weapon_aim 85 along the barrel); parents from the scene
 * graph (fpv_unit_parent). The weapon is the unit whose root sits on attach_hand_r, its attachments (scope, magazine,
 * muzzle) the units linked to it (fpv_unit_link; a sight sits 5 cm off its mount node, so not by place); looked for
 * every POSE_SEARCH_FRAMES.
 *
 * Only the six joints change: each gets the local pose that puts it at its new world pose under its (new) parent, set
 * with the engine's set_local_pose, and the Helldiver and the weapon's units are updated with the engine's update_unit,
 * as the camera's pose is (writing world poses alone does not reach the render: tried live). That is before the render
 * step (fpv_camera's filter); after it (fpv_camera_set_after) the original local poses are set and the units updated
 * again, so the game's logic (where shots come from, what others see) never sees the pose. The conversion to local poses
 * is checked once against the engine's own (the elbow's), and the pose is off when they differ.
 *
 * The pose fades out (over a fifth of a second) while the game aims (its field of view zoomed: the right mouse already
 * brings the weapon up), and goes while the barrel points far from the body's facing (the hands busy: throws, melee).
 *
 * Cost per frame while held: six joints set and put back, the Helldiver and the weapon's units updated twice; the search
 * (the roots of the units near the grip) once a second.
 */
#include <math.h>
#include <string.h>
#include "fp.h"

#define POSE_MAX_UNITS 40
#define POSE_SEARCH_FRAMES 30
#define POSE_SEARCH_RANGE 2.5f   /* metres from the grip a weapon's part or the armour's root can be */
#define POSE_ON 0.002f           /* metres: a root this near a node is mounted on it */
#define POSE_RATE 5.0f           /* per second: the fade */
#define POSE_AWAY_FROM 85.0f     /* degrees between the barrel and the body's facing where the pose starts to fade */
#define POSE_AWAY_TO 100.0f      /* and where it is gone */
#define POSE_AWAY_HOLD 0.1f      /* seconds turned away before it counts */
#define POSE_MAX_WORLD 2048      /* world poses kept for the restore (the Helldiver's 107, its armour's and the weapon's) */
#define POSE_CHECK 0.01f         /* the conversion's largest difference from the engine's own local pose */

static struct
{
    uintptr_t diver;
    uint32_t ref;
    int ok;                       /* the skeleton was understood */
    int32_t shoulder[2], elbow[2], hand[2], above[2], grip, muzzle;
    uintptr_t units[POSE_MAX_UNITS];
    int nunits, wait;
    float weight;
    float awayFor;                /* seconds the barrel has been turned away */
    int32_t setNode[6];           /* the joints set this frame, and their original local poses */
    float setOriginal[6][16];
    int nset;
    float* worldAt[1 + POSE_MAX_UNITS];  /* the world poses the update changes, and their originals */
    uint32_t worldCount[1 + POSE_MAX_UNITS];
    float worldOriginal[POSE_MAX_WORLD][16];
    int nworld;
    int loggedSkeleton, loggedWeapon, loggedCheck;
} g;
/* The ready place, in the view's frame (metres): forward, right, up from the eye. */
static const float kReady[3] = {0.36f, 0.18f, -0.27f};

void fp_pose_reset(void)
{
    memset(&g, 0, sizeof g);
}

/* ---- 3x3 rotations (v' = R v, row-major) -------------------------------------------------------------------------- */

typedef struct Rot { float m[9]; } Rot;

static Rot Identity(void) { Rot r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}}; return r; }

static Rot AxisAngle(FpvVec3 axis, float angle)
{
    if (!fpv_normalize(&axis) || angle == 0) return Identity();
    float c = cosf(angle), s = sinf(angle), t = 1 - c, x = axis.x, y = axis.y, z = axis.z;
    Rot r = {{t * x * x + c, t * x * y - s * z, t * x * z + s * y,
              t * x * y + s * z, t * y * y + c, t * y * z - s * x,
              t * x * z - s * y, t * y * z + s * x, t * z * z + c}};
    return r;
}

/* The smallest rotation taking direction a to direction b, scaled by w (0 none, 1 all of it). */
static Rot Between(FpvVec3 a, FpvVec3 b, float w)
{
    if (!fpv_normalize(&a) || !fpv_normalize(&b)) return Identity();
    float c = fpv_dot(a, b);
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    FpvVec3 axis = fpv_cross(a, b);
    if (fpv_dot(axis, axis) < 1e-12f) return Identity();  /* the same (or opposite: not turned) */
    return AxisAngle(axis, acosf(c) * w);
}

static FpvVec3 Turn(const Rot* r, FpvVec3 v)
{
    return fpv_v3(r->m[0] * v.x + r->m[1] * v.y + r->m[2] * v.z, r->m[3] * v.x + r->m[4] * v.y + r->m[5] * v.z,
        r->m[6] * v.x + r->m[7] * v.y + r->m[8] * v.z);
}

static Rot Then(const Rot* first, const Rot* second)  /* second after first */
{
    Rot r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[3 * i + j] = second->m[3 * i] * first->m[j] + second->m[3 * i + 1] * first->m[3 + j] +
                second->m[3 * i + 2] * first->m[6 + j];
    return r;
}

/* A rigid move: x' = to + R (x - from). */
typedef struct Move { Rot r; FpvVec3 from, to; } Move;

static FpvVec3 Point(const float* m) { return fpv_v3(m[12], m[13], m[14]); }

/* A world pose (rows right, forward, up, position) moved into out. */
static void MoveMatrix(const float* m, const Move* move, float* out)
{
    memcpy(out, m, 16 * sizeof(float));
    for (int row = 0; row < 3; row++)
    {
        FpvVec3 v = Turn(&move->r, fpv_v3(m[4 * row], m[4 * row + 1], m[4 * row + 2]));
        out[4 * row] = v.x;
        out[4 * row + 1] = v.y;
        out[4 * row + 2] = v.z;
    }
    FpvVec3 p = fpv_add(move->to, Turn(&move->r, fpv_sub(Point(m), move->from)));
    out[12] = p.x;
    out[13] = p.y;
    out[14] = p.z;
}

static FpvVec3 Row(const float* m, int i) { return fpv_v3(m[4 * i], m[4 * i + 1], m[4 * i + 2]); }

/* The local pose (rotation rows 0-8, position 9-11, the original's scale 12-15) that puts a node at world pose child
 * under a parent at world pose parent: child = local x parent, rows as vectors. */
static void Local(const float* child, const float* parent, const float* original, float* out)
{
    for (int i = 0; i < 3; i++)
    {
        FpvVec3 c = Row(child, i), l;
        float v[3];
        for (int j = 0; j < 3; j++)
        {
            FpvVec3 pj = Row(parent, j);
            float n = fpv_dot(pj, pj);
            v[j] = n > 1e-12f ? fpv_dot(c, pj) / n : 0;
        }
        l = fpv_v3(v[0], v[1], v[2]);
        fpv_normalize(&l);
        out[3 * i] = l.x;
        out[3 * i + 1] = l.y;
        out[3 * i + 2] = l.z;
    }
    FpvVec3 d = fpv_sub(Point(child), Point(parent));
    for (int j = 0; j < 3; j++)
    {
        FpvVec3 pj = Row(parent, j);
        float n = fpv_dot(pj, pj);
        out[9 + j] = n > 1e-12f ? fpv_dot(d, pj) / n : 0;
    }
    memcpy(out + 12, original + 12, 4 * sizeof(float));
}

static FpvVec3 Moved(const Move* move, FpvVec3 x) { return fpv_add(move->to, Turn(&move->r, fpv_sub(x, move->from))); }

/* ---- the skeleton --------------------------------------------------------------------------------------------------- */

static int32_t Node(uintptr_t unit, const char* name) { return fpv_unit_node(unit, fpv_id32(name)); }

/* The conversion to local poses agrees with the engine's own: the elbow's local pose from its and the shoulder's world
 * poses. */
static int Agrees(uintptr_t diver)
{
    float child[16], parent[16], local[16], mine[16];
    if (!fpv_unit_world_pose(diver, g.elbow[1], child) || !fpv_unit_world_pose(diver, g.shoulder[1], parent) ||
        !fpv_unit_local_pose(diver, g.elbow[1], local)) return 0;
    Local(child, parent, local, mine);
    float worst = 0;
    for (int i = 0; i < 12; i++) worst = fmaxf(worst, fabsf(mine[i] - local[i]));
    if (worst <= POSE_CHECK) return 1;
    fpv_log_once(&g.loggedCheck, "pose", "local poses do not convert as expected (off by %.3f): no ready pose (said once)", worst);
    return 0;
}

/* The arms' joints by name, checked against the scene graph's parents. */
static int Classify(uintptr_t diver)
{
    g.diver = diver;
    g.ref = fpv_unit_ref(diver);
    g.ok = 0;
    g.nunits = 0;
    g.wait = 0;
    const char* names[2][3] = {{"l_shoulder", "l_elbow", "l_hand"}, {"r_shoulder", "r_elbow", "r_hand"}};
    for (int side = 0; side < 2; side++)
    {
        g.shoulder[side] = Node(diver, names[side][0]);
        g.elbow[side] = Node(diver, names[side][1]);
        g.hand[side] = Node(diver, names[side][2]);
        g.above[side] = fpv_unit_parent(diver, g.shoulder[side]);
        if (g.above[side] < 0 || fpv_unit_parent(diver, g.elbow[side]) != g.shoulder[side] ||
            fpv_unit_parent(diver, g.hand[side]) != g.elbow[side]) return 0;
    }
    g.grip = Node(diver, "attach_hand_r");
    g.muzzle = Node(diver, "weapon_aim");
    if (g.grip < 0 || g.muzzle < 0 || !Agrees(diver)) return 0;
    g.ok = 1;
    return 1;
}
/* ---- the weapon ----------------------------------------------------------------------------------------------------- */

/* Linked to the Helldiver (its armour: the arms seen are the armour's, with its own copies of the arm bones) or to one
 * of the units found already (the engine's link: a scope sits off its mount node, live 5 cm). */
static int Mounted(uintptr_t unit)
{
    uintptr_t link = fpv_unit_link(unit);
    uint32_t count;
    if (link != 0 && (uintptr_t)fpv_unit_world_poses(g.diver, &count) == link) return 1;
    for (int k = 0; link != 0 && k < g.nunits; k++)
        if ((uintptr_t)fpv_unit_world_poses(g.units[k], &count) == link) return 1;
    return 0;
}

/* Whether a unit near the grip is taken in this pass: the weapon first (on the grip), then what is linked to it. */
static int Takes(uintptr_t unit, FpvVec3 grip, int pass)
{
    float root[16];
    if (unit == 0 || unit == g.diver || !fpv_unit_world_pose(unit, 0, root)) return 0;
    FpvVec3 d = fpv_sub(Point(root), grip);
    if (fpv_dot(d, d) > POSE_SEARCH_RANGE * POSE_SEARCH_RANGE) return 0;
    for (int k = 0; k < g.nunits; k++)
        if (g.units[k] == unit) return 0;
    return pass == 0 ? fpv_dot(d, d) < POSE_ON * POSE_ON : Mounted(unit);
}

/* The weapon (the unit on the grip) and what is mounted on it. */
static void Search(uintptr_t world, FpvVec3 grip)
{
    g.nunits = 0;
    uintptr_t list;
    uint32_t count = fpv_world_units(world, &list);
    for (int pass = 0; pass < 3; pass++)
        for (uint32_t i = 0; i < count && g.nunits < POSE_MAX_UNITS; i++)
        {
            uintptr_t unit = fpv_world_unit(list, i);
            if (!Takes(unit, grip, pass)) continue;
            g.units[g.nunits++] = unit;
            if (pass == 0) break;  /* one weapon; its parts next */
        }
    if (g.nunits == 0) fpv_log_once(&g.loggedWeapon, "pose", "no weapon on the right hand (said once)");
}

/* ---- the pose ------------------------------------------------------------------------------------------------------- */

/* Two-bone IK: the elbow for the shoulder s reaching t, with upper arm a and forearm b, on the side of the old elbow e. */
static FpvVec3 Elbow(FpvVec3 s, FpvVec3 e, FpvVec3 t, float a, float b)
{
    FpvVec3 u = fpv_sub(t, s);
    float d = sqrtf(fpv_dot(u, u));
    if (!fpv_normalize(&u)) return e;
    float lo = fabsf(a - b) + 1e-3f, hi = a + b - 1e-3f;
    d = d < lo ? lo : d > hi ? hi : d;
    FpvVec3 se = fpv_sub(e, s), v = fpv_sub(se, fpv_scale(u, fpv_dot(se, u)));
    if (!fpv_normalize(&v)) return e;
    float c = (a * a + d * d - b * b) / (2 * a * d);
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    return fpv_add(s, fpv_add(fpv_scale(u, a * c), fpv_scale(v, a * sqrtf(1 - c * c))));
}

/* A joint set to its new world pose under its parent's new one (its original local pose kept for the restore). */
static void Set(int32_t node, const float* world, const float* parentWorld)
{
    float original[16], local[16];
    if (g.nset >= 6 || !fpv_unit_local_pose(g.diver, node, original)) return;
    Local(world, parentWorld, original, local);
    g.setNode[g.nset] = node;
    memcpy(g.setOriginal[g.nset++], original, sizeof original);
    fpv_unit_set_local_pose(g.ref, node, local);
}

/* One arm to its hand's new place: the upper arm about the shoulder, the forearm about the new elbow. */
static void Arm(int side, const Move* hands)
{
    float above[16], sh[16], el[16], ha[16], sh2[16], el2[16], ha2[16];
    if (!fpv_unit_world_pose(g.diver, g.above[side], above) || !fpv_unit_world_pose(g.diver, g.shoulder[side], sh) ||
        !fpv_unit_world_pose(g.diver, g.elbow[side], el) || !fpv_unit_world_pose(g.diver, g.hand[side], ha)) return;
    FpvVec3 s = Point(sh), e = Point(el), h = Point(ha), t = Moved(hands, h);
    float a = sqrtf(fpv_dot(fpv_sub(e, s), fpv_sub(e, s))), b = sqrtf(fpv_dot(fpv_sub(h, e), fpv_sub(h, e)));
    FpvVec3 e2 = Elbow(s, e, t, a, b);
    Move upper = {Between(fpv_sub(e, s), fpv_sub(e2, s), 1), s, s};
    Rot r2 = Between(fpv_sub(Moved(&upper, h), e2), fpv_sub(t, e2), 1);
    /* x' = e2 + r2 (s + r1 (x - s) - e2) = (e2 + r2 (s - e2)) + (r2 r1)(x - s) */
    Move fore = {Then(&upper.r, &r2), s, fpv_add(e2, Turn(&r2, fpv_sub(s, e2)))};
    MoveMatrix(sh, &upper, sh2);
    MoveMatrix(el, &fore, el2);
    MoveMatrix(ha, hands, ha2);
    Set(g.shoulder[side], sh2, above);
    Set(g.elbow[side], el2, sh2);
    Set(g.hand[side], ha2, el2);
}

/* A unit's world poses kept (all of them: the update recomputes every node) before the update changes them. */
static void Keep(int slot, uintptr_t unit)
{
    uint32_t count = 0, used = 0;
    float* poses = fpv_unit_world_poses(unit, &count);
    g.worldAt[slot] = NULL;
    for (int i = 0; i < slot; i++) used += g.worldAt[i] != NULL ? g.worldCount[i] : 0;
    if (poses == NULL || used + count > POSE_MAX_WORLD) return;
    g.worldAt[slot] = poses;
    g.worldCount[slot] = count;
    memcpy(g.worldOriginal[used], poses, 64 * (size_t)count);
}

/* The engine's update of the Helldiver and the weapon's units, their world poses kept first: it takes the pose to the
 * render (writing world poses alone does not). */
static void Update(void)
{
    g.nworld = 1 + g.nunits;
    Keep(0, g.diver);
    for (int u = 0; u < g.nunits; u++) Keep(1 + u, g.units[u]);
    fpv_unit_update(g.diver);
    for (int u = 0; u < g.nunits; u++) fpv_unit_update(g.units[u]);
}

/* The world poses as they were: the game's logic next frame reads the game's own pose. Without the engine's update, so
 * the render keeps this frame's pose as the last one it drew (an update back made the renderer blur the weapon, as if
 * it moved between the two every frame: live). */
static void Unkeep(void)
{
    uint32_t used = 0;
    for (int i = 0; i < g.nworld; i++)
    {
        if (g.worldAt[i] == NULL) continue;
        memcpy(g.worldAt[i], g.worldOriginal[used], 64 * (size_t)g.worldCount[i]);
        used += g.worldCount[i];
    }
    g.nworld = 0;
}
/* How much of the pose the barrel's direction allows: all of it near the body's facing, none past POSE_AWAY_TO. The
 * body's, not the view's: the Helldiver faces where it walks (the barrel 37 degrees from the view walking forward, 91
 * walking back: live), and in first person the weapon is held where the player looks all the same. From the body (live):
 * standing 16 degrees, walking, strafing and sprinting 47-52 (carried across the chest), reloading 80-90 (tilted up 60). */
static float Away(FpvVec3 barrel, FpvVec3 forward)
{
    if (!fpv_normalize(&barrel)) return 0;
    float c = fpv_dot(barrel, forward), angle = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
    if (angle <= POSE_AWAY_FROM) return 1;
    if (angle >= POSE_AWAY_TO) return 0;
    return (POSE_AWAY_TO - angle) / (POSE_AWAY_TO - POSE_AWAY_FROM);
}

/* The weapon and its parts looked for every POSE_SEARCH_FRAMES, and at once when the weapon switched. */
static void Find(FpvVec3 grip)
{
    float root[16];
    FpvVec3 off = g.nunits > 0 && fpv_unit_world_pose(g.units[0], 0, root) ? fpv_sub(Point(root), grip) : fpv_v3(0, 0, 0);
    if (g.wait-- > 0 && fpv_dot(off, off) <= POSE_ON * POSE_ON) return;
    Search(fpv_camera_world(), grip);
    g.wait = POSE_SEARCH_FRAMES;
}

void fp_pose_apply(uintptr_t diver, const float* view, FpvVec3 target, FpvVec3 facing, int aiming,
    float mix, float dt)
{
    g.nset = 0;
    if (diver != g.diver && !Classify(diver))
        fpv_log_once(&g.loggedSkeleton, "pose", "the Helldiver's arms are not as expected: no ready pose (said once)");
    float gw[16], mw[16];
    if (!g.ok || !fpv_unit_world_pose(diver, g.grip, gw) || !fpv_unit_world_pose(diver, g.muzzle, mw)) return;
    FpvVec3 grip = Point(gw), barrel = fpv_sub(Point(mw), grip);
    FpvVec3 eye = fpv_pose_position(view), forward = fpv_pose_forward(view), right = fpv_pose_right(view),
            up = fpv_pose_up(view);
    /* The game's aim and the switch fade; the barrel turning away (reloads, sprints, throws) takes the pose with it at
     * once (a fading pose turned a reloading weapon upside down over the view: live). */
    float want = (aiming ? 0.0f : 1.0f) * (mix < 0 ? 0 : mix > 1 ? 1 : mix);
    float k = 1 - expf(-POSE_RATE * (dt > 0 ? dt : 0));
    g.weight += (want - g.weight) * (dt > 0 ? k : 1);
    float away = Away(barrel, fpv_dot(facing, facing) > 0.5f ? facing : forward);
    g.awayFor = away < 1 ? g.awayFor + dt : 0;
    if (g.awayFor < POSE_AWAY_HOLD) away = 1;  /* a walk's sway spikes for a frame (live): only a held turn counts */
    if (g.weight * away < 0.01f) return;
    Find(grip);
    FpvVec3 ready = fpv_add(eye, fpv_add(fpv_scale(forward, kReady[0]), fpv_add(fpv_scale(right, kReady[1]),
        fpv_scale(up, kReady[2]))));
    float w = g.weight * g.weight * (3 - 2 * g.weight) * away;
    Move hands = {Between(barrel, fpv_sub(target, ready), w), grip, fpv_add(grip, fpv_scale(fpv_sub(ready, grip), w))};
    Arm(0, &hands);
    Arm(1, &hands);
    Update();
}

void fp_pose_restore(void)
{
    if (g.nset == 0) return;
    for (int i = g.nset - 1; i >= 0; i--) fpv_unit_set_local_pose(g.ref, g.setNode[i], g.setOriginal[i]);
    g.nset = 0;
    Unkeep();
}
float fp_pose_weight(void) { return g.weight; }
