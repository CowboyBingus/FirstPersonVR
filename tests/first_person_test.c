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

int main(void)
{
    RUN(CameraGoesToTheHead);
    RUN(FarCameraIsNotOnFoot);
    RUN(SwitchedOffStaysThirdPerson);
    RUN(DestroyedHelldiverIsNoticed);
    RUN(NoHeadNoFirstPerson);
    TEST_END();
}
