/* The camera override against a fake world: finding the camera unit by the render pose, filtering its pose, and
 * leaving the frame alone without a filter. */
#include <string.h>
#include "fpv.h"
#include "harness/test.h"

TEST_GLOBALS;

#define WORLD_UNITS 0x2305B8u
#define WORLD_RENDER_POSE 0x230920u

typedef struct FakeUnit
{
    uintptr_t vtable;
    uint32_t ref;
    int cameras;
    uintptr_t graph;
    float poses[16];
} FakeUnit;

static uint8_t g_world[WORLD_RENDER_POSE + 64];
static uintptr_t g_vtable[0x170 / 8];
static uint8_t g_graph[2][0x28];
static FakeUnit g_units[2];
static uintptr_t g_unitList[2];
static int g_steps, g_sets, g_updates;
static float g_setPose[16];

static int Cameras(uintptr_t unit) { return ((FakeUnit*)unit)->cameras; }
static uintptr_t Graph(uintptr_t unit) { return ((FakeUnit*)unit)->graph; }
static void Step(void* app) { g_steps++; }
static void SetPose(uint32_t unit, int32_t node, const float* pose) { g_sets++; memcpy(g_setPose, pose, sizeof g_setPose); }
static void Update(uintptr_t world, uintptr_t unit) { g_updates++; }
static uintptr_t World(void) { return (uintptr_t)g_world; }
static uintptr_t Unit(uint32_t ref)
{
    for (int i = 0; i < 2; i++)
        if (g_units[i].ref == ref) return (uintptr_t)&g_units[i];
    return 0;
}

static void Pose(float* pose, float x, float y, float z)
{
    memset(pose, 0, 16 * sizeof(float));
    pose[0] = 1;  /* right +X */
    pose[4] = 1;  /* forward +Y */
    pose[8] = 1;  /* up +Z */
    pose[9] = x;
    pose[10] = y;
    pose[11] = z;
    pose[12] = pose[13] = pose[14] = 1;
}

/* Two units: 0 has no camera and sits on the render pose, 1 has a camera 0.5 m from it. */
static void (*World2(void))(void* app)
{
    memset(g_world, 0, sizeof g_world);
    g_steps = g_sets = g_updates = 0;
    g_vtable[0x160 / 8] = (uintptr_t)Cameras;
    g_vtable[0xE8 / 8] = (uintptr_t)Graph;
    for (int i = 0; i < 2; i++)
    {
        FakeUnit* unit = &g_units[i];
        unit->vtable = (uintptr_t)g_vtable;
        unit->ref = 100 + (uint32_t)i;
        unit->cameras = i;
        unit->graph = (uintptr_t)g_graph[i];
        uintptr_t poses = (uintptr_t)unit->poses;
        memcpy(g_graph[i] + 0x20, &poses, sizeof poses);
        Pose(unit->poses, 10, 20, i == 0 ? 2 : 2.5f);
        g_unitList[i] = (uintptr_t)unit;
        fake_readable(unit, sizeof *unit);
        fake_readable(g_graph[i], sizeof g_graph[i]);
    }
    fake_readable(g_vtable, sizeof g_vtable);
    fake_readable(g_unitList, sizeof g_unitList);
    uint32_t count = 2;
    uintptr_t list = (uintptr_t)g_unitList;
    memcpy(g_world + WORLD_UNITS, &count, sizeof count);
    memcpy(g_world + WORLD_UNITS + 8, &list, sizeof list);
    float render[16] = {0};
    render[12] = 10;
    render[13] = 20;
    render[14] = 2;
    memcpy(g_world + WORLD_RENDER_POSE, render, sizeof render);
    fake_readable(g_world, sizeof g_world);
    FpvEngineFns fns = {Step, SetPose, Update, Unit, World};
    return fpv_camera_install_for_test(&fns);
}

static int Raise(float* pose, void* context)
{
    pose[11] += *(float*)context;
    return 1;
}

static int Leave(float* pose, void* context) { return 0; }

static void NoFilterLeavesTheFrame(void)
{
    void (*step)(void*) = World2();
    step(NULL);
    CHECK_EQ(g_steps, 1);
    CHECK_EQ(g_sets, 0);
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK_EQ(status.unit, 0); /* not even looked for */
}

static void FilterMovesTheCameraUnit(void)
{
    void (*step)(void*) = World2();
    float up = 1.5f;
    fpv_camera_set_filter(Raise, &up);
    step(NULL);
    CHECK_EQ(g_steps, 1);
    CHECK_EQ(g_sets, 1);
    CHECK_EQ(g_updates, 1);
    CHECK(g_setPose[11] > 3.99f && g_setPose[11] < 4.01f); /* the camera unit's 2.5, raised */
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK_EQ(status.unit, 101);
    CHECK_EQ(status.frames, 1);
    CHECK_LOG("found the camera unit (101)");
}

static void DecliningFilterLeavesTheFrame(void)
{
    void (*step)(void*) = World2();
    fpv_camera_set_filter(Leave, NULL);
    step(NULL);
    CHECK_EQ(g_sets, 0);
    CHECK_EQ(g_steps, 1);
}

static void NoCameraGivesUpAndSays(void)
{
    void (*step)(void*) = World2();
    g_units[1].cameras = 0;
    float up = 1;
    fpv_camera_set_filter(Raise, &up);
    for (int i = 0; i < 40; i++) step(NULL);
    CHECK_EQ(g_steps, 40);
    CHECK_EQ(g_sets, 0);
    CHECK_LOG("no camera unit at the render pose");
}

static void FarCameraIsNotTaken(void)
{
    void (*step)(void*) = World2();
    g_units[1].poses[11] = 9; /* 7 m above the render pose */
    float up = 1;
    fpv_camera_set_filter(Raise, &up);
    step(NULL);
    CHECK_EQ(g_sets, 0);
    FpvCameraStatus status;
    fpv_camera_status(&status);
    CHECK(status.nearest > 6.9f && status.nearest < 7.1f);
}

int main(void)
{
    RUN(NoFilterLeavesTheFrame);
    RUN(FilterMovesTheCameraUnit);
    RUN(DecliningFilterLeavesTheFrame);
    RUN(NoCameraGivesUpAndSays);
    RUN(FarCameraIsNotTaken);
    TEST_END();
}
