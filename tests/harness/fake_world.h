/* A fake main world for the camera and First Person tests: units laid out as the engine's (fpv_units.c), a camera unit,
 * Helldivers with a "head" node, and the engine functions the camera calls. */
#ifndef FPV_FAKE_WORLD_H
#define FPV_FAKE_WORLD_H

#include <stdint.h>
#include "fpv.h"

#define FAKE_UNITS 8
#define FAKE_NODES 4

typedef struct FakeUnit
{
    uintptr_t vtable;          /* +0x00 */
    uint32_t ref;              /* +0x08 */
    uint8_t pad[0x30 - 0x0C];
    uintptr_t resource;        /* +0x30: points at resource_name */
    uint64_t resource_name;
    int cameras;
    struct
    {
        uint8_t pad0[0x10];
        uint32_t count;        /* +0x10 */
        uint8_t pad1[0x0C];
        uintptr_t local;       /* +0x20 */
        uintptr_t world;       /* +0x28 */
        uint8_t pad2[0x10];
        uintptr_t names;       /* +0x40 */
    } graph;
    float local[FAKE_NODES][16];
    float world[FAKE_NODES][16];
    uint32_t names[FAKE_NODES];
    float game[16];            /* the pose the game gives node 0 every frame (the render step puts it back) */
} FakeUnit;

extern FakeUnit fake_units[FAKE_UNITS];
extern int fake_steps, fake_sets, fake_updates;
extern float fake_set_pose[16];

/* An empty world (render pose at the origin) with the camera installed on it; returns the render step's detour. */
void (*fake_world(void))(void* app);
/* Adds a unit; node 0 at `position`, facing +Y with +Z up, in both its local and world poses. */
FakeUnit* fake_add_unit(uint64_t resource, int cameras, float x, float y, float z);
/* Adds a node `name` to the unit, at `position` in the world. */
void fake_add_node(FakeUnit* unit, const char* name, float x, float y, float z);
void fake_render_pose(float x, float y, float z);
/* Removes the last unit (as the engine's array would lose one). */
void fake_remove_last(void);

#endif
