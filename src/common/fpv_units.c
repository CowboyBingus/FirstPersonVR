/* Units, as the engine keeps them (build 25480438), read the way its own Lua bindings read them:
 *
 * - A unit reference resolves to its Unit* through exe+0x9D8C0 (fpv_engine_unit); the object holds the reference at +8.
 * - Its resource (Unit.resource_name, binding exe+0x40BD50): the u64 at [[unit + 0x30]].
 * - Its scene graph (the vtable's +0xE8; Unit.has_node, binding exe+0x405530): the node count at +0x10, the local poses
 *   at +0x20 (16 floats each), the world poses at +0x28 (row-major 4x4: right, forward, up, position) and the node names
 *   at +0x40, one IdString32 each (the top 32 bits of the name's murmur64, seed 0: exe+0x5D5840).
 * - The world's units: a u32 count at world + 0x2305B8, the Unit* array at +0x2305C0.
 *
 * Proved live 2026-10-31 (Lua): the Helldiver is resource 0x4D1C334D294DFA97, 107 nodes, its "head" node index 90
 * (Lua's 91), 1.58 m above its root; the main world has one camera unit, which follows the Helldiver.
 */
#include <string.h>
#include "fpv.h"

#define UNITS_WORLD_COUNT 0x2305B8u
#define UNITS_MAX 200000u
#define GRAPH_COUNT 0x10u
#define GRAPH_LINK 0x18u    /* the world poses of the unit it is linked to (live: a weapon's parts -> the weapon's) */
#define GRAPH_WORLD 0x28u
#define GRAPH_PARENTS 0x38u  /* u32 a node: its parent's index in the high 16 bits (live: head 90 -> neck 89) */
#define GRAPH_NAMES 0x40u
#define GRAPH_MAX_NODES 4096u
#define UNIT_GRAPH_METHOD 0xE8u
#define UNIT_CAMERAS_METHOD 0x160u
#define UNIT_CAMERA_METHOD 0x168u    /* mov rax,[rcx+0x118]; mov rax,[rax+rdx*8]: camera i */

typedef uintptr_t (*GraphFn)(uintptr_t unit);
typedef int (*CamerasFn)(uintptr_t unit);
typedef uintptr_t (*CameraFn)(uintptr_t unit, uint32_t index);

uint64_t fpv_murmur64(const void* key, size_t length)
{
    const uint64_t m = 0xC6A4A7935BD1E995ull;
    uint64_t h = length * m;
    const uint8_t* data = (const uint8_t*)key;
    size_t whole = length / 8 * 8;
    for (size_t i = 0; i < whole; i += 8)
    {
        uint64_t k;
        memcpy(&k, data + i, 8);
        k *= m;
        k ^= k >> 47;
        h = (h ^ (k * m)) * m;
    }
    if (whole != length)
    {
        uint64_t tail = 0;
        for (size_t i = length; i > whole; i--) tail = (tail << 8) | data[i - 1];
        h = (h ^ tail) * m;
    }
    h ^= h >> 47;
    h *= m;
    return h ^ (h >> 47);
}

uint32_t fpv_id32(const char* name) { return name != NULL ? (uint32_t)(fpv_murmur64(name, strlen(name)) >> 32) : 0; }

static uintptr_t Method(uintptr_t unit, uint32_t slot)
{
    uintptr_t table = 0, method = 0;
    if (unit == 0 || !fpv_read(unit, &table, sizeof table) || table == 0 || !fpv_read(table + slot, &method, sizeof method))
        return 0;
    return method;
}

uintptr_t fpv_unit_graph(uintptr_t unit)
{
    uintptr_t method = Method(unit, UNIT_GRAPH_METHOD);
    return method != 0 ? ((GraphFn)method)(unit) : 0;
}

int fpv_unit_cameras(uintptr_t unit)
{
    uintptr_t method = Method(unit, UNIT_CAMERAS_METHOD);
    return method != 0 ? ((CamerasFn)method)(unit) : 0;
}

uintptr_t fpv_unit_camera(uintptr_t unit, int index)
{
    if (index < 0 || index >= fpv_unit_cameras(unit)) return 0;
    uintptr_t method = Method(unit, UNIT_CAMERA_METHOD);
    return method != 0 ? ((CameraFn)method)(unit, (uint32_t)index) : 0;
}

uint32_t fpv_unit_ref(uintptr_t unit)
{
    uint32_t ref = 0;
    return unit != 0 && fpv_read(unit + 8, &ref, sizeof ref) ? ref : 0;
}

uint64_t fpv_unit_resource(uintptr_t unit)
{
    uintptr_t resource = 0;
    uint64_t name = 0;
    if (unit == 0 || !fpv_read(unit + 0x30, &resource, sizeof resource) || resource == 0 ||
        !fpv_read(resource, &name, sizeof name)) return 0;
    return name;
}

int32_t fpv_unit_node(uintptr_t unit, uint32_t name)
{
    uintptr_t graph = fpv_unit_graph(unit), names = 0;
    uint32_t count = 0;
    if (graph == 0 || !fpv_read(graph + GRAPH_COUNT, &count, sizeof count) || count == 0 || count > GRAPH_MAX_NODES ||
        !fpv_read(graph + GRAPH_NAMES, &names, sizeof names) || names == 0) return -1;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t hash = 0;
        if (!fpv_read(names + 4 * (uintptr_t)i, &hash, sizeof hash)) return -1;
        if (hash == name) return (int32_t)i;
    }
    return -1;
}

int32_t fpv_unit_parent(uintptr_t unit, int32_t node)
{
    uintptr_t graph = fpv_unit_graph(unit), parents = 0;
    uint32_t count = 0, entry = 0;
    if (graph == 0 || node <= 0 || !fpv_read(graph + GRAPH_COUNT, &count, sizeof count) || (uint32_t)node >= count ||
        !fpv_read(graph + GRAPH_PARENTS, &parents, sizeof parents) || parents == 0 ||
        !fpv_read(parents + 4 * (uintptr_t)node, &entry, sizeof entry)) return -1;
    uint32_t parent = entry >> 16;
    return parent < count && parent != (uint32_t)node ? (int32_t)parent : -1;
}

uintptr_t fpv_unit_link(uintptr_t unit)
{
    uintptr_t graph = fpv_unit_graph(unit), poses = 0;
    return graph != 0 && fpv_read(graph + GRAPH_LINK, &poses, sizeof poses) ? poses : 0;
}

float* fpv_unit_world_poses(uintptr_t unit, uint32_t* count)
{
    uintptr_t graph = fpv_unit_graph(unit), poses = 0;
    *count = 0;
    if (graph == 0 || !fpv_read(graph + GRAPH_COUNT, count, sizeof *count) || *count == 0 || *count > GRAPH_MAX_NODES ||
        !fpv_read(graph + GRAPH_WORLD, &poses, sizeof poses) || poses == 0) { *count = 0; return NULL; }
    return (float*)poses;
}

int fpv_unit_world_pose(uintptr_t unit, int32_t node, float* matrix)
{
    uintptr_t graph = fpv_unit_graph(unit), poses = 0;
    uint32_t count = 0;
    if (graph == 0 || node < 0 || !fpv_read(graph + GRAPH_COUNT, &count, sizeof count) || (uint32_t)node >= count ||
        !fpv_read(graph + GRAPH_WORLD, &poses, sizeof poses) || poses == 0) return 0;
    return fpv_read(poses + (uintptr_t)node * 64, matrix, 16 * sizeof(float));
}

uint32_t fpv_world_units(uintptr_t world, uintptr_t* list)
{
    uint32_t count = 0;
    *list = 0;
    if (world == 0 || !fpv_read(world + UNITS_WORLD_COUNT, &count, sizeof count) || count > UNITS_MAX ||
        !fpv_read(world + UNITS_WORLD_COUNT + 8, list, sizeof *list) || *list == 0) return 0;
    return count;
}

uintptr_t fpv_world_unit(uintptr_t list, uint32_t index)
{
    uintptr_t unit = 0;
    return list != 0 && fpv_read(list + 8 * (uintptr_t)index, &unit, sizeof unit) ? unit : 0;
}
