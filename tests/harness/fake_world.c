#include <string.h>
#include "harness/fake_host.h"
#include "harness/fake_world.h"

#define WORLD_UNITS 0x2305B8u
#define WORLD_RENDER_POSE 0x230920u

FakeUnit fake_units[FAKE_UNITS];
int fake_steps, fake_sets, fake_updates;
float fake_set_pose[16];

static uint8_t g_world[WORLD_RENDER_POSE + 64];
static uintptr_t g_vtable[0x170 / 8];
static uintptr_t g_list[FAKE_UNITS];
static uint32_t g_count;

static int Cameras(uintptr_t unit) { return ((FakeUnit*)unit)->cameras; }
static uintptr_t Graph(uintptr_t unit) { return (uintptr_t)&((FakeUnit*)unit)->graph; }
static void Step(void* app) { fake_steps++; }
static void Update(uintptr_t world, uintptr_t unit) { fake_updates++; }
static uintptr_t World(void) { return (uintptr_t)g_world; }

static uintptr_t Unit(uint32_t ref)
{
    for (uint32_t i = 0; i < g_count; i++)
        if (fake_units[i].ref == ref) return (uintptr_t)&fake_units[i];
    return 0;
}

static void SetPose(uint32_t ref, int32_t node, const float* pose)
{
    fake_sets++;
    memcpy(fake_set_pose, pose, sizeof fake_set_pose);
    FakeUnit* unit = (FakeUnit*)Unit(ref);
    if (unit != NULL) memcpy(unit->local[0], pose, sizeof unit->local[0]);
}

static void Count(void)
{
    memcpy(g_world + WORLD_UNITS, &g_count, sizeof g_count);
}

void (*fake_world(void))(void* app)
{
    memset(g_world, 0, sizeof g_world);
    memset(fake_units, 0, sizeof fake_units);
    fake_steps = fake_sets = fake_updates = 0;
    g_count = 0;
    g_vtable[0x160 / 8] = (uintptr_t)Cameras;
    g_vtable[0xE8 / 8] = (uintptr_t)Graph;
    uintptr_t list = (uintptr_t)g_list;
    memcpy(g_world + WORLD_UNITS + 8, &list, sizeof list);
    Count();
    fake_readable(g_world, sizeof g_world);
    fake_readable(g_vtable, sizeof g_vtable);
    fake_readable(g_list, sizeof g_list);
    fake_readable(fake_units, sizeof fake_units);
    FpvEngineFns fns = {Step, SetPose, Update, Unit, World};
    return fpv_camera_install_for_test(&fns);
}

void fake_render_pose(float x, float y, float z)
{
    float render[16] = {0};
    render[12] = x;
    render[13] = y;
    render[14] = z;
    memcpy(g_world + WORLD_RENDER_POSE, render, sizeof render);
}

static void Pose(float* m, float x, float y, float z)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 1;
    m[4] = 1;
    m[8] = 1;
    m[15] = 1;
}

FakeUnit* fake_add_unit(uint64_t resource, int cameras, float x, float y, float z)
{
    FakeUnit* unit = &fake_units[g_count];
    unit->vtable = (uintptr_t)g_vtable;
    unit->ref = 100 + g_count;
    unit->resource_name = resource;
    unit->resource = (uintptr_t)&unit->resource_name;
    unit->cameras = cameras;
    unit->graph.local = (uintptr_t)unit->local;
    unit->graph.world = (uintptr_t)unit->world;
    unit->graph.names = (uintptr_t)unit->names;
    unit->graph.count = 1;
    unit->names[0] = fpv_id32("root");
    Pose(unit->local[0], x, y, z);
    /* local poses: rotation rows, then position at 9-11 */
    unit->local[0][9] = x;
    unit->local[0][10] = y;
    unit->local[0][11] = z;
    unit->local[0][12] = unit->local[0][13] = unit->local[0][14] = 1;
    unit->local[0][15] = 0;
    Pose(unit->world[0], x, y, z);
    unit->world[0][12] = x;
    unit->world[0][13] = y;
    unit->world[0][14] = z;
    g_list[g_count++] = (uintptr_t)unit;
    Count();
    return unit;
}

void fake_add_node(FakeUnit* unit, const char* name, float x, float y, float z)
{
    uint32_t n = unit->graph.count++;
    unit->names[n] = fpv_id32(name);
    Pose(unit->world[n], x, y, z);
    unit->world[n][12] = x;
    unit->world[n][13] = y;
    unit->world[n][14] = z;
}

void fake_remove_last(void)
{
    if (g_count == 0) return;
    g_count--;
    g_list[g_count] = 0;
    Count();
}
