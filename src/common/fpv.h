/* What the two mods (First Person, VR Mode) share. They reach the game through FpvHost (filled by fpv_host_init, or by
 * a test); only fpv_host.c and the mods' entry files include hellforge_mod.h (through fpv_init.h).
 *
 * Poses are the engine's scene-graph local poses, 16 floats: rotation rows right (0-2), forward (3-5), up (6-8), then
 * position (9-11), scale (12-14) and one float the engine keeps (15). Stingray is Z-up and right-handed.
 */
#ifndef FPV_H
#define FPV_H

#include <stddef.h>
#include <stdint.h>

#define FPV_VERSION "0.1.0"
#define FPV_GAME_BUILD "25480438"

typedef struct FpvHost
{
    void (*log)(const char* feature, const char* text);
    int (*read)(uintptr_t address, void* out, size_t size);              /* 1 read, 0 not */
    int (*code_matches)(uintptr_t address, const char* pattern);         /* 1 the unhooked bytes match */
    int (*hook)(uintptr_t target, const char* shipped, void* detour, void** original, int32_t priority); /* 1 hooked */
    uintptr_t exe_base;
    size_t exe_size;
    uintptr_t game_base;
    size_t game_size;
} FpvHost;

const FpvHost* fpv_host(void);
#ifdef FPV_TEST
FpvHost* fpv_host_for_test(void);
#endif

void fpv_log(const char* feature, const char* format, ...);
void fpv_log_once(int* logged, const char* feature, const char* format, ...);
int fpv_read(uintptr_t address, void* out, size_t size);
/* Seconds from a steady clock (QueryPerformanceCounter). */
double fpv_seconds(void);
/* hook_ex at helldivers2.exe + rva, after its unhooked bytes match `shipped`. 1 hooked, 0 refused. */
int fpv_exe_hook(uint32_t rva, const char* shipped, void* detour, void** original, int32_t priority);
/* 1 when the unhooked bytes at helldivers2.exe + rva match the pattern. */
int fpv_exe_matches(uint32_t rva, const char* pattern);

/* ---- pose math (fpv_math.c) -------------------------------------------------------------------------------------- */

typedef struct FpvVec3
{
    float x, y, z;
} FpvVec3;

FpvVec3 fpv_v3(float x, float y, float z);
FpvVec3 fpv_add(FpvVec3 a, FpvVec3 b);
FpvVec3 fpv_sub(FpvVec3 a, FpvVec3 b);
FpvVec3 fpv_scale(FpvVec3 a, float s);
float fpv_dot(FpvVec3 a, FpvVec3 b);
FpvVec3 fpv_cross(FpvVec3 a, FpvVec3 b);
/* a / |a|; 0 when a is (nearly) zero, with *out left unchanged. */
int fpv_normalize(FpvVec3* a);

FpvVec3 fpv_pose_right(const float* pose);
FpvVec3 fpv_pose_forward(const float* pose);
FpvVec3 fpv_pose_up(const float* pose);
FpvVec3 fpv_pose_position(const float* pose);
/* The pose looking along `forward` with `up` as near as can be, at `position`; scale and the last float from `keep`
 * (a pose of the same unit). 0 when forward and up are parallel or zero (out is then a copy of keep). */
int fpv_pose_look(const float* keep, FpvVec3 position, FpvVec3 forward, FpvVec3 up, float* out);
/* `local` (an offset in the pose's own frame: x right, y forward, z up) in world space. */
FpvVec3 fpv_pose_point(const float* pose, FpvVec3 local);

/* ---- the camera (fpv_camera.c) ----------------------------------------------------------------------------------- */

/* Called each frame before the game renders, with the camera unit's pose as the game set it. A filter changes `pose`
 * and returns 1 for the frame to render from it, or returns 0 to leave the frame as it is. */
typedef int (*FpvCameraFilter)(float* pose, void* context);

typedef struct FpvCameraStatus
{
    int installed;       /* 1 hooked, -1 refused (the log says why), 0 not tried */
    uint32_t unit;       /* the camera unit's reference while known, else 0 */
    float nearest;       /* the last search's closest camera unit to the render pose, in metres (-1 none) */
    uint32_t frames;     /* frames the filter changed */
} FpvCameraStatus;

/* The render step's hook priorities: First Person's filter runs before VR Mode's, so VR adds the head to First Person's
 * camera (each mod hooks the step itself; HellForge chains them, highest priority first). */
#define FPV_PRIORITY_FIRST_PERSON 20
#define FPV_PRIORITY_VR 10

/* Hooks the engine's script render step (once). 1 installed, 0 refused. */
int fpv_camera_install(int32_t priority);
void fpv_camera_set_filter(FpvCameraFilter filter, void* context);
/* Forget the camera unit (the world changed): it is looked for again on the next frame that needs it. */
void fpv_camera_forget(void);
void fpv_camera_status(FpvCameraStatus* out);
/* The engine's Unit* for a unit reference (0 when the camera is not installed or the unit is gone). */
uintptr_t fpv_engine_unit(uint32_t unit);
/* The main world this frame's filter runs for (0 outside a filter's frame). */
uintptr_t fpv_camera_world(void);

/* ---- units (fpv_units.c) ----------------------------------------------------------------------------------------- */

uint64_t fpv_murmur64(const void* key, size_t length);
/* An IdString32: the top 32 bits of the name's murmur64 (node names). */
uint32_t fpv_id32(const char* name);
uintptr_t fpv_unit_graph(uintptr_t unit);
int fpv_unit_cameras(uintptr_t unit);
uint32_t fpv_unit_ref(uintptr_t unit);
uint64_t fpv_unit_resource(uintptr_t unit);
/* The node's index by its IdString32, or -1. */
int32_t fpv_unit_node(uintptr_t unit, uint32_t name);
/* The node's world pose (row-major 4x4: right, forward, up, position rows). 1 read. */
int fpv_unit_world_pose(uintptr_t unit, int32_t node, float* matrix);
/* The world's units: the count, and *list for fpv_world_unit. */
uint32_t fpv_world_units(uintptr_t world, uintptr_t* list);
uintptr_t fpv_world_unit(uintptr_t list, uint32_t index);

#ifdef FPV_TEST
typedef struct FpvEngineFns
{
    void (*step)(void* app);
    void (*set_local_pose)(uint32_t unit, int32_t node, const float* pose);
    void (*update_unit)(uintptr_t world, uintptr_t unit);
    uintptr_t (*unit)(uint32_t unit);
    uintptr_t (*main_world)(void);
} FpvEngineFns;
/* The engine functions a test stands in for; the render step's detour is returned for the test to call. */
void (*fpv_camera_install_for_test(const FpvEngineFns* fns))(void* app);
void fpv_camera_reset_for_test(void);
#endif

#endif
