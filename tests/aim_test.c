/* Aim: the view turns from the eye to where the game's line hits, the ray starts past the diver, and the distance is
 * smoothed (nearer fast, farther slowly). */
#include <math.h>
#include <string.h>
#include "fp.h"
#include "harness/fake_world.h"
#include "harness/test.h"

TEST_GLOBALS;

#define AVATAR 0x4D1C334D294DFA97ull

static float g_wall = 10;        /* the wall's y; the game's line runs along +y */
static float g_from[3];
static int g_casts;

static int Raycast(uint32_t query, const float* from, const float* dir, float length, int zero, float* result, int one)
{
    g_casts++;
    memcpy(g_from, from, sizeof g_from);
    if (dir[1] <= 0 || from[1] >= g_wall) return 0;
    float t = (g_wall - from[1]) / dir[1];
    for (int i = 0; i < 3; i++) result[i] = from[i] + dir[i] * t;
    return 1;
}

static int Near(float a, float b, float e) { return fabsf(a - b) < e; }

static void ViewTurnsToTheHit(void)
{
    fp_aim_set_for_test(Raycast, 7);
    g_wall = 4;
    float game[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0.6f, -3, 1.9f, 1, 1, 1, 0}; /* over the right shoulder, looking +y */
    FpvVec3 eye = fpv_v3(0, 0.12f, 1.66f), head = fpv_v3(0, 0, 1.6f), forward;
    CHECK(fp_aim_forward(game, eye, head, 0.016f, &forward));
    /* the line hits (0.6, 4, 1.9); the eye looks at it */
    CHECK(Near(forward.x, 0.6f / sqrtf(0.36f + 3.88f * 3.88f + 0.24f * 0.24f), 1e-3f));
    CHECK(forward.y > 0.98f);
    CHECK(g_from[1] > 0.5f); /* started past the diver's head (y 0) */
}

static void NoHitLooksFar(void)
{
    fp_aim_set_for_test(Raycast, 7);
    g_wall = 1000;
    float game[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0.6f, -3, 1.9f, 1, 1, 1, 0};
    FpvVec3 forward;
    CHECK(fp_aim_forward(game, fpv_v3(0, 0.12f, 1.66f), fpv_v3(0, 0, 1.6f), 0.016f, &forward));
    CHECK(Near(forward.x, 0, 0.003f) && forward.y > 0.999f); /* 400 m away: all but parallel */
}

static void NoRaycastKeepsTheGamesDirection(void)
{
    fp_aim_set_for_test(NULL, 0);
    float game[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0.6f, -3, 1.9f, 1, 1, 1, 0};
    FpvVec3 forward;
    CHECK(!fp_aim_forward(game, fpv_v3(0, 0, 1.6f), fpv_v3(0, 0, 1.6f), 0.016f, &forward));
}

static void SmoothingNearFastFarSlow(void)
{
    CHECK(Near(fp_aim_smooth(0, 20, 0.016f), 20, 1e-6f)); /* the first one is taken as it is */
    float nearer = fp_aim_smooth(20, 5, 1.0f / 30), farther = fp_aim_smooth(5, 20, 1.0f / 30);
    CHECK(nearer < 10);                      /* most of the way in one frame at 30 fps */
    CHECK(farther > 5 && farther < 10);      /* a small step out */
    float d = 5;
    for (int i = 0; i < 30; i++) d = fp_aim_smooth(d, 20, 1.0f / 30);
    CHECK(Near(d, 20, 0.1f));                /* there within a second */
}

static void FilterUsesTheAim(void)
{
    fp_view_reset();
    fp_camera_reset();
    fp_aim_set_for_test(Raycast, 7);
    g_wall = 6;
    void (*step)(void*) = fake_world();
    fpv_camera_set_filter(fp_camera_filter, NULL);
    fake_add_unit(1, 1, 0.6f, -3, 1.9f);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    step(NULL);
    CHECK_EQ(fake_sets, 1);
    CHECK(fake_set_pose[3] > 0.05f);          /* turned right, towards the hit at x 0.6 */
    CHECK(Near(fake_set_pose[11], 1.66f, 1e-3f));
    CHECK(Near(fake_set_pose[8], 1, 0.01f));  /* no roll: up stays near world up */
}

int main(void)
{
    RUN(ViewTurnsToTheHit);
    RUN(NoHitLooksFar);
    RUN(NoRaycastKeepsTheGamesDirection);
    RUN(SmoothingNearFastFarSlow);
    RUN(FilterUsesTheAim);
    TEST_END();
}
