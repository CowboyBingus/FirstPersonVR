/* Aim: the game shoots from its own (third-person) camera, along that camera's view: from P along f, at whatever the
 * line T = P + f * d hits first. game.dll keeps its camera to itself (it never reads the engine camera, measured
 * 2026-10-31), so First Person cannot move where the game aims from. It turns the eye towards T instead: one physics ray
 * along the game's line each frame gives d, and the view from the eye E looks along T - E, so the screen's centre is
 * where the shots land (and the game's own reticle sits there).
 *
 * The ray: the engine's synchronous raycast, the physics interface's slot +104 (*(game.dll+0x3326328) + 0x68 =
 * helldivers2.exe+0x77DEC0): int (u32 query, const float from[3], const float dir[3], float length, int 0,
 * float result[16], int 1), nonzero on a hit with its position at result[0..2]. The query is the prebuilt one the game
 * uses for its own ground and line-of-sight rays (game.dll+0x84B692, +0x9DA45D): *(*(game.dll+0x346D508)+8)+0xA4.
 * The ray starts a little past the diver along the line, so it never hits the diver itself.
 *
 * d is smoothed: it follows a nearer hit within a frame or two and eases out to a farther one, so the view does not
 * twitch when the line crosses an edge. Cost per frame: one raycast and a few flops.
 */
#include <math.h>
#include <string.h>
#include "fp.h"

#define AIM_PHYSICS 0x3326328u
#define AIM_PHYSICS_SLOT 0x68u
#define AIM_RAYCAST 0x77DEC0u
#define AIM_WORLD 0x346D508u
#define AIM_QUERY 0xA4u
#define AIM_RANGE 400.0f
#define AIM_PAST_DIVER 0.6f       /* metres past the diver's head, along the line, where the ray starts */
#define AIM_NEAR 1.0f             /* a target nearer the eye than this is not turned towards */

typedef int (*RaycastFn)(uint32_t query, const float* from, const float* dir, float length, int zero, float* result,
    int closest);

static const char kRaycast[] = "48 8B C4 48 89 58 10 48 89 70 18 57 48 83 EC 70 8B D9";

static struct
{
    int state;          /* 0 not tried, 1 ready, -1 off */
    RaycastFn raycast;
    float distance;     /* the smoothed d (0: none yet) */
    int loggedOff;
} g;

void fp_aim_reset(void)
{
    memset(&g, 0, sizeof g);
}

static int Ready(void)
{
    if (g.state != 0) return g.state == 1;
    g.state = -1;
    const FpvHost* host = fpv_host();
    uintptr_t physics = 0, slot = 0;
    if (host->game_base == 0 || !fpv_exe_matches(AIM_RAYCAST, kRaycast) ||
        !fpv_read(host->game_base + AIM_PHYSICS, &physics, sizeof physics) || physics == 0 ||
        !fpv_read(physics + AIM_PHYSICS_SLOT, &slot, sizeof slot) || slot != host->exe_base + AIM_RAYCAST)
    {
        fpv_log("aim", "off: the physics raycast does not match build %s; the view keeps the game's direction",
            FPV_GAME_BUILD);
        return 0;
    }
    g.raycast = (RaycastFn)slot;
    g.state = 1;
    fpv_log("aim", "the view turns to where the game aims");
    return 1;
}

#ifdef FPV_TEST
static uint32_t g_testQuery;
void fp_aim_set_for_test(FpAimRaycast raycast, uint32_t query)
{
    fp_aim_reset();
    g.raycast = raycast;
    g.state = raycast != NULL ? 1 : -1;
    g_testQuery = query;
}
#endif

static uint32_t Query(void)
{
#ifdef FPV_TEST
    if (g_testQuery != 0) return g_testQuery;
#endif
    uintptr_t world = 0, physics = 0;
    uint32_t query = 0;
    uintptr_t game = fpv_host()->game_base;
    if (!fpv_read(game + AIM_WORLD, &world, sizeof world) || world == 0 ||
        !fpv_read(world + 8, &physics, sizeof physics) || physics == 0 ||
        !fpv_read(physics + AIM_QUERY, &query, sizeof query)) return 0;
    return query;
}

/* The distance along the game's line (from `from`, along unit `dir`) to what it hits, or AIM_RANGE. */
static float Cast(FpvVec3 from, FpvVec3 dir)
{
    uint32_t query = Query();
    if (query == 0) return AIM_RANGE;
    float start[3] = {from.x, from.y, from.z}, direction[3] = {dir.x, dir.y, dir.z}, result[16];
    memset(result, 0, sizeof result);
    if (!g.raycast(query, start, direction, AIM_RANGE, 0, result, 1)) return AIM_RANGE;
    FpvVec3 hit = fpv_v3(result[0], result[1], result[2]);
    float along = fpv_dot(fpv_sub(hit, from), dir);
    return isfinite(along) && along > 0 ? along : AIM_RANGE;
}

float fp_aim_smooth(float smoothed, float target, float dt)
{
    if (smoothed <= 0 || !isfinite(smoothed)) return target;
    /* Nearer: follow fast (an enemy steps into the line). Farther: ease out over about a fifth of a second. */
    float rate = target < smoothed ? 40.0f : 10.0f;
    float k = 1.0f - expf(-rate * (dt > 0 ? dt : 0.0f));
    return smoothed + (target - smoothed) * k;
}

int fp_aim_forward(const float* game, FpvVec3 eye, FpvVec3 head, float dt, FpvVec3* forward)
{
    if (!Ready()) return 0;
    FpvVec3 from = fpv_pose_position(game), dir = fpv_pose_forward(game);
    if (!fpv_normalize(&dir)) return 0;
    float past = fpv_dot(fpv_sub(head, from), dir) + AIM_PAST_DIVER;
    if (past < 0) past = 0;
    float d = past + Cast(fpv_add(from, fpv_scale(dir, past)), dir);
    g.distance = fp_aim_smooth(g.distance, d, dt);
    FpvVec3 target = fpv_add(from, fpv_scale(dir, g.distance)), to = fpv_sub(target, eye);
    if (fpv_dot(to, to) < AIM_NEAR * AIM_NEAR || !fpv_normalize(&to)) return 0;
    *forward = to;
    return 1;
}
