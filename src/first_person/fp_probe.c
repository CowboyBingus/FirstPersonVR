/* G1 step 1's probe: logs the local Helldiver's unit and scene graph once per Helldiver, so the head node and the graph's
 * layout can be read off a live run. It changes nothing: its camera filter always declines the frame.
 *
 * The Helldiver: HF_ON_WEAPON's avatar entity. Its unit: the entity manager's descriptor for it (game.dll + 0x346BF98,
 * the map at +0xF1AEB0 and the 24-byte descriptors at +0xF32F18: resource, entity, unit), as HellForge's event polls read
 * it (HellForge native/mod-loader/event_polls.c). The Unit*: the engine's unit lookup (fpv_camera's). Its scene graph:
 * the unit vtable's +0xE8 (object + 0x60), local poses at +0x20 and world poses (4x4, row-major) at +0x28.
 *
 * Cost: with no avatar known, one branch a frame; once known, one block of reads and log lines per Helldiver.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fp.h"

#define PROBE_ENTITIES 0x346BF98u
#define PROBE_ENTITY_MAP 0xF1AEB0u
#define PROBE_DESCRIPTORS 0xF32F18u
#define PROBE_MAX_NODES 400

static uint32_t g_avatar, g_dumped;
static int g_state; /* 0 idle, 1 an avatar to dump */

void fp_probe_avatar(uint32_t avatar)
{
    if (avatar == 0 || avatar == g_dumped) return;
    g_avatar = avatar;
    g_state = 1;
}

static int Ptr(uintptr_t at, uintptr_t* out)
{
    uint64_t v = 0;
    if (!fpv_read(at, &v, sizeof v) || v < 0x10000 || (v >> 32) > 0x7FFF) return 0;
    *out = (uintptr_t)v;
    return 1;
}

/* The game's u32 hash map lookup (header {u64 buckets, u32 capacity, u32 empty, u32 multiplier}). */
static int MapGet(uintptr_t header, uint32_t key, uint32_t stride, uint32_t valueAt, uint32_t* value)
{
    uint8_t h[20];
    if (key == 0 || !fpv_read(header, h, sizeof h)) return 0;
    uint64_t buckets;
    uint32_t capacity, empty, multiplier;
    memcpy(&buckets, h, 8);
    memcpy(&capacity, h + 8, 4);
    memcpy(&empty, h + 12, 4);
    memcpy(&multiplier, h + 16, 4);
    if (buckets < 0x10000 || capacity == 0 || capacity > 1048576) return 0;
    uint32_t start = key * multiplier, probes = capacity < 4096 ? capacity : 4096;
    for (uint32_t i = 0; i < probes; i++)
    {
        uintptr_t at = (uintptr_t)(buckets + (uint64_t)((start + i) % capacity) * stride);
        uint32_t k;
        if (!fpv_read(at, &k, 4)) return 0;
        if (k == key) return fpv_read(at + valueAt, value, 4) && *value != 0xFFFFFFFFu;
        if (k == empty) return 0;
    }
    return 0;
}

static uint32_t UnitOf(uint32_t entity)
{
    uintptr_t game = fpv_host()->game_base, em;
    uint32_t index, named, unit;
    uint8_t d[24];
    if (game == 0 || !Ptr(game + PROBE_ENTITIES, &em) || !MapGet(em + PROBE_ENTITY_MAP, entity, 8, 4, &index) ||
        index >= 0x100000 || !fpv_read(em + PROBE_DESCRIPTORS + (uintptr_t)index * 24, d, sizeof d)) return 0;
    memcpy(&named, d + 8, 4);
    memcpy(&unit, d + 12, 4);
    return named == entity ? unit : 0;
}

static void Hex(const char* label, uintptr_t at, size_t size)
{
    uint8_t bytes[128];
    if (size > sizeof bytes || !fpv_read(at, bytes, size)) { fpv_log("probe", "%s: unreadable", label); return; }
    char text[400];
    size_t used = 0;
    for (size_t i = 0; i < size; i++) used += (size_t)snprintf(text + used, sizeof text - used, "%02X%s", bytes[i],
        (i % 8) == 7 ? " " : "");
    fpv_log("probe", "%s: %s", label, text);
}

/* The graph header and what its pointers point at, then every node's world position relative to the unit's root and
 * the camera, as long as they read. */
static void Dump(uint32_t unitRef, const float* camera)
{
    uintptr_t unit = fpv_engine_unit(unitRef), table = 0, method = 0;
    if (unit == 0 || !Ptr(unit, &table) || !Ptr(table + 0xE8, &method)) { fpv_log("probe", "unit %u: no Unit*", unitRef); return; }
    uintptr_t graph = unit + 0x60;
    fpv_log("probe", "Helldiver %u: unit %u at %p, vtable exe+0x%llX, graph method exe+0x%llX", g_avatar, unitRef,
        (void*)unit, (unsigned long long)(table - fpv_host()->exe_base), (unsigned long long)(method - fpv_host()->exe_base));
    Hex("graph +0x00", graph, 64);
    Hex("graph +0x40", graph + 64, 64);
    for (int i = 0; i < 16; i++)
    {
        uintptr_t p;
        char label[48];
        snprintf(label, sizeof label, "graph +0x%02X ->", i * 8);
        if (Ptr(graph + (uintptr_t)i * 8, &p)) Hex(label, p, 64);
    }
    uintptr_t world;
    if (!Ptr(graph + 0x28, &world)) { fpv_log("probe", "no world poses"); return; }
    float root[16];
    if (!fpv_read(world, root, sizeof root)) return;
    for (int n = 0; n < PROBE_MAX_NODES; n++)
    {
        float m[16];
        if (!fpv_read(world + (uintptr_t)n * 64, m, sizeof m) || !isfinite(m[12]) || fabsf(m[12]) > 100000) break;
        float dz = m[14] - root[14];
        float cx = m[12] - camera[9], cy = m[13] - camera[10], cz = m[14] - camera[11];
        fpv_log("probe", "node %3d: up %.3f from root, %.3f m from the camera; fwd (%.2f %.2f %.2f)", n, dz,
            sqrtf(cx * cx + cy * cy + cz * cz), m[4], m[5], m[6]);
    }
}

int fp_probe_filter(float* pose, void* context)
{
    if (g_state != 1) return 0;
    g_state = 0;
    uint32_t unit = UnitOf(g_avatar);
    if (unit == 0) { fpv_log("probe", "Helldiver %u: no unit", g_avatar); return 0; }
    g_dumped = g_avatar;
    fpv_log("probe", "camera at (%.2f %.2f %.2f), forward (%.2f %.2f %.2f)", pose[9], pose[10], pose[11], pose[3], pose[4],
        pose[5]);
    Dump(unit, pose);
    return 0;
}
