/* First Person's camera against a fake world: the camera at the Helldiver's head on foot, third person when the camera
 * is far from it or the player switched First Person off, and a destroyed Helldiver noticed. */
#include "fp.h"
#include "harness/fake_world.h"
#include "harness/test.h"

TEST_GLOBALS;

#define AVATAR 0x4D1C334D294DFA97ull

static int Near(float a, float b) { return a - b < 1e-3f && b - a < 1e-3f; }

static void (*Setup(void))(void*)
{
    fp_view_reset();
    fp_camera_reset();
    void (*step)(void*) = fake_world();
    fpv_camera_set_filter(fp_camera_filter, NULL);
    return step;
}

static void CameraGoesToTheHead(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);           /* the camera, behind the Helldiver, looking +Y */
    FakeUnit* other = fake_add_unit(AVATAR, 0, 30, 0, 0); /* someone else's Helldiver */
    fake_add_node(other, "head", 30, 0, 1.6f);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0.5f, 0, 0);
    fake_add_node(diver, "neck", 0.5f, 0, 1.45f);
    fake_add_node(diver, "head", 0.5f, 0, 1.58f);
    step(NULL);
    CHECK_EQ(fake_sets, 1);
    CHECK(Near(fake_set_pose[9], 0.5f) && Near(fake_set_pose[10], 0.12f) && Near(fake_set_pose[11], 1.64f));
    CHECK(Near(fake_set_pose[4], 1)); /* the game's rotation kept */
    CHECK_LOG("the Helldiver: unit 102, head node 2");
    CHECK_LOG("[camera] first person");
    CHECK_EQ(fp_view_situation(), FP_SITUATION_ON_FOOT);
    CHECK_EQ(fp_view_active(), 1);
}

static void TeammateBehindTheCameraIsNotTaken(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);           /* the camera, looking +Y */
    FakeUnit* mate = fake_add_unit(AVATAR, 0, 0, -4, 0); /* a teammate just behind it, nearer than ours */
    fake_add_node(mate, "head", 0, -4, 1.6f);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0.5f, 0, 0);
    fake_add_node(diver, "head", 0.5f, 0, 1.6f);
    step(NULL);
    CHECK_LOG("the Helldiver: unit 102");
    CHECK(Near(fake_set_pose[9], 0.5f));
}

static void FarCameraIsNotOnFoot(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -9, 4);           /* a vehicle's camera, say */
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    step(NULL);
    CHECK_EQ(fake_sets, 0);
    CHECK_EQ(fp_view_situation(), 0);
}

static void SwitchedOffStaysThirdPerson(void)
{
    void (*step)(void*) = Setup();
    FpSettings off = {0, 1, FP_SITUATION_ON_FOOT};
    fp_view_settings(&off);
    fake_add_unit(1, 1, 0, -3, 2);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    step(NULL);
    CHECK_EQ(fake_sets, 0);
    CHECK_EQ(fp_view_situation(), FP_SITUATION_ON_FOOT);
}

static void DestroyedHelldiverIsNoticed(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    step(NULL);
    CHECK_EQ(fake_sets, 1);
    fake_remove_last();
    step(NULL);
    CHECK_EQ(fake_sets, 1);
    CHECK_LOG("[camera] third person");
}

static void NoHeadNoFirstPerson(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);
    fake_add_unit(AVATAR, 0, 0, 0, 0);
    step(NULL);
    CHECK_EQ(fake_sets, 0);
    CHECK_LOG("the Helldiver has no head node");
}

static uint32_t g_state;
static uint32_t State(void) { return g_state; }

static void OutsideAMissionIsNotOnFoot(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -1, 1.6f);        /* the screen after a mission: its camera near the head */
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    fpv_host_for_test()->game_state = State;
    g_state = 3;                             /* HF_STATE_SHIP */
    step(NULL);
    CHECK_EQ(fake_sets, 0);
    CHECK_EQ(fp_view_situation(), 0);
    g_state = 4;                             /* HF_STATE_MISSION */
    step(NULL);
    CHECK_EQ(fake_sets, 1);
    fpv_host_for_test()->game_state = NULL;
}

/* Frames at 40 per second until `seconds` have passed. */
static double Run(void (*step)(void*), double now, double seconds)
{
    for (double end = now + seconds; now < end - 1e-9;)
    {
        fpv_seconds_for_test(now += 0.025);
        step(NULL);
    }
    return now;
}

static void SwitchingBlends(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    double now = 100;
    fpv_seconds_for_test(now);
    step(NULL);                              /* the first frame: straight to the eye */
    CHECK(Near(fake_set_pose[10], 0.12f));
    uint32_t off = fp_view_request("test", FP_SITUATION_ON_FOOT, FP_VIEW_THIRD, 0);
    now = Run(step, now, 0.125);             /* half the blend */
    CHECK(fake_set_pose[10] < -1.0f && fake_set_pose[10] > -2.0f); /* between the eye (0.12) and the game's (-3) */
    now = Run(step, now, 0.125);
    int sets = fake_sets;
    now = Run(step, now, 0.05);
    CHECK_EQ(fake_sets, sets);               /* blended out: the game's own frame */
    fp_view_release(off);
    now = Run(step, now, 0.025);
    CHECK(fake_set_pose[10] < -2.5f);        /* coming back in, near the game's still */
    Run(step, now, 0.25);
    CHECK(Near(fake_set_pose[10], 0.12f));
    fpv_seconds_for_test(-1);
}

static void EyeIsSteadied(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    double now = 100;
    fpv_seconds_for_test(now);
    step(NULL);
    diver->world[1][14] = 1.65f;                         /* a bob: the eye follows part of it in a frame */
    fpv_seconds_for_test(now += 1.0 / 60);
    step(NULL);
    CHECK(fake_set_pose[11] > 1.661f && fake_set_pose[11] < 1.68f);
    for (int i = 0; i < 60; i++) { fpv_seconds_for_test(now += 1.0 / 60); step(NULL); }
    CHECK(Near(fake_set_pose[11], 1.71f));   /* and all of it once it stays */
    fpv_seconds_for_test(-1);
}

static void GamesOwnSightsAreKept(void)
{
    void (*step)(void*) = Setup();
    FakeUnit* camera = fake_add_unit(1, 1, 0, -3, 2);
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    double now = Run(step, 100, 0.1);
    CHECK(Near(fake_set_pose[10], 0.12f));
    camera->game[10] = 0.1f;                 /* the game's first-person aim: its camera at the head */
    camera->game[11] = 1.62f;
    now = Run(step, now, 0.3);
    int sets = fake_sets;
    now = Run(step, now, 0.1);
    CHECK_EQ(fake_sets, sets);               /* the game's sights, untouched */
    CHECK_EQ(fp_view_active(), 1);           /* still First Person's situation */
    camera->game[10] = -3;                   /* the aim ends */
    camera->game[11] = 2;
    Run(step, now, 0.3);
    CHECK(Near(fake_set_pose[10], 0.12f));
    fpv_seconds_for_test(-1);
}

static void HelldiverArrivingLaterIsFound(void)
{
    void (*step)(void*) = Setup();
    fake_add_unit(1, 1, 0, -3, 2);
    step(NULL); /* the title screen: no Helldiver yet */
    FakeUnit* diver = fake_add_unit(AVATAR, 0, 0, 0, 0);
    fake_add_node(diver, "head", 0, 0, 1.6f);
    for (int i = 0; i < 40; i++) step(NULL);
    CHECK(fake_sets > 0);
    CHECK_LOG("the Helldiver: unit 101");
}

int main(void)
{
    RUN(CameraGoesToTheHead);
    RUN(TeammateBehindTheCameraIsNotTaken);
    RUN(FarCameraIsNotOnFoot);
    RUN(SwitchedOffStaysThirdPerson);
    RUN(DestroyedHelldiverIsNoticed);
    RUN(NoHeadNoFirstPerson);
    RUN(HelldiverArrivingLaterIsFound);
    RUN(OutsideAMissionIsNotOnFoot);
    RUN(SwitchingBlends);
    RUN(EyeIsSteadied);
    RUN(GamesOwnSightsAreKept);
    TEST_END();
}
