/* The pose helpers: a look pose is square and keeps the unit's scale, and local offsets land in world space. */
#include <math.h>
#include "fpv.h"
#include "harness/test.h"

TEST_GLOBALS;

static int Near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static void LookIsSquare(void)
{
    float keep[16] = {0};
    keep[12] = keep[13] = keep[14] = 2;
    keep[15] = 7;
    float out[16];
    CHECK(fpv_pose_look(keep, fpv_v3(1, 2, 3), fpv_v3(1, 1, 0), fpv_v3(0, 0, 1), out));
    FpvVec3 r = fpv_pose_right(out), f = fpv_pose_forward(out), u = fpv_pose_up(out);
    CHECK(Near(fpv_dot(r, f), 0) && Near(fpv_dot(r, u), 0) && Near(fpv_dot(f, u), 0));
    CHECK(Near(fpv_dot(f, f), 1) && Near(fpv_dot(u, u), 1));
    CHECK(Near(f.x, 0.70710678f) && Near(u.z, 1));
    CHECK(Near(r.x, 0.70710678f) && Near(r.y, -0.70710678f)); /* right-handed, Z up: forward x up */
    CHECK(Near(out[9], 1) && Near(out[11], 3) && Near(out[12], 2) && Near(out[15], 7));
}

static void LookRefusesParallel(void)
{
    float keep[16] = {0}, out[16];
    CHECK(!fpv_pose_look(keep, fpv_v3(0, 0, 0), fpv_v3(0, 0, 1), fpv_v3(0, 0, 2), out));
    CHECK(!fpv_pose_look(keep, fpv_v3(0, 0, 0), fpv_v3(0, 0, 0), fpv_v3(0, 0, 1), out));
}

static void PointIsInThePosesFrame(void)
{
    float keep[16] = {0}, pose[16];
    fpv_pose_look(keep, fpv_v3(10, 0, 0), fpv_v3(-1, 0, 0), fpv_v3(0, 0, 1), pose);
    FpvVec3 p = fpv_pose_point(pose, fpv_v3(0, 2, 0.5f)); /* 2 m ahead (-X), half a metre up */
    CHECK(Near(p.x, 8) && Near(p.y, 0) && Near(p.z, 0.5f));
}

int main(void)
{
    RUN(LookIsSquare);
    RUN(LookRefusesParallel);
    RUN(PointIsInThePosesFrame);
    TEST_END();
}
