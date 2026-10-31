/* The camera override against a fake world: finding the camera unit, filtering its pose, leaving the frame alone without
 * a filter, and searching again after a miss. */
#include "fpv.h"
#include "harness/fake_world.h"
#include "harness/test.h"

TEST_GLOBALS;

static int Raise(float* pose, void* context)
{
    pose[11] += *(float*)context;
    return 1;
}

static int Leave(float* pose, void* context) { return 0; }

static void NoFilterLeavesTheFrame(void)
{
    void (*step)(void*) = fake_world();
    fake_add_unit(1, 1, 10, 20, 2.5f);
    step(NULL);
    CHECK_EQ(fake_steps, 1);
    CHECK_EQ(fake_sets, 0);
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK_EQ(status.unit, 0); /* not even looked for */
}

static void FilterMovesTheCameraUnit(void)
{
    void (*step)(void*) = fake_world();
    fake_add_unit(1, 0, 10, 20, 2); /* no camera, on the render pose */
    fake_add_unit(2, 1, 10, 20, 2.5f);
    fake_render_pose(10, 20, 2);
    float up = 1.5f;
    fpv_camera_set_filter(Raise, &up);
    step(NULL);
    CHECK_EQ(fake_steps, 1);
    CHECK_EQ(fake_sets, 1);
    CHECK_EQ(fake_updates, 1);
    CHECK(fake_set_pose[11] > 3.99f && fake_set_pose[11] < 4.01f); /* the camera unit's 2.5, raised */
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK_EQ(status.unit, 101);
    CHECK_EQ(status.frames, 1);
    CHECK_LOG("found the camera unit (101, 0.50 m from the render pose)");
}

static void NearestOfSeveralCameras(void)
{
    void (*step)(void*) = fake_world();
    fake_add_unit(1, 1, 50, 0, 0);
    fake_add_unit(1, 1, 1, 0, 0);
    float up = 1;
    fpv_camera_set_filter(Raise, &up);
    step(NULL);
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK_EQ(status.unit, 101);
}

static void OneFarCameraIsTaken(void)
{
    void (*step)(void*) = fake_world();
    fake_add_unit(1, 1, 0, 0, 200); /* the render pose is stale while a world loads */
    float up = 1;
    fpv_camera_set_filter(Raise, &up);
    step(NULL);
    CHECK_EQ(fake_sets, 1);
}

static void DecliningFilterLeavesTheFrame(void)
{
    void (*step)(void*) = fake_world();
    fake_add_unit(1, 1, 0, 0, 0);
    fpv_camera_set_filter(Leave, NULL);
    step(NULL);
    CHECK_EQ(fake_sets, 0);
    CHECK_EQ(fake_steps, 1);
}

static void NoCameraWaitsAndLooksAgain(void)
{
    void (*step)(void*) = fake_world();
    float up = 1;
    fpv_camera_set_filter(Raise, &up);
    for (int i = 0; i < 10; i++) step(NULL);
    CHECK_LOG("no camera unit in the main world yet");
    fake_add_unit(1, 1, 0, 0, 0); /* the world finished loading */
    for (int i = 0; i < 40; i++) step(NULL);
    CHECK(fake_sets > 0);
    CHECK_EQ(fake_steps, 50);
}

int main(void)
{
    RUN(NoFilterLeavesTheFrame);
    RUN(FilterMovesTheCameraUnit);
    RUN(NearestOfSeveralCameras);
    RUN(OneFarCameraIsTaken);
    RUN(DecliningFilterLeavesTheFrame);
    RUN(NoCameraWaitsAndLooksAgain);
    TEST_END();
}
