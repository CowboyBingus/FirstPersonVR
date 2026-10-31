/* First Person's parts: the view decision (fp_view.c), the camera (fp_camera.c) and the entry (fp_mod.c). */
#ifndef FP_H
#define FP_H

#include "first_person_api.h"
#include "fpv.h"

#define FP_MOD_NAME "First Person"
#define FP_MAX_REQUESTS 64

/* The player's settings the decision reads (the options file's, from G1 on). */
typedef struct FpSettings
{
    int enabled;            /* the master switch (and the toggle key's state) */
    int mods_may_change;    /* other mods' requests count */
    uint32_t first_in;      /* FP_SITUATION_... bits that are first person without a request */
} FpSettings;

/* The player's options file (fp_options.c): the view settings, the toggle key and the eye's place. */
typedef struct FpOptions
{
    FpSettings settings;
    uint32_t toggle_key;    /* a Windows virtual-key code; 0: none */
    float eye_forward, eye_up;
    float fov;              /* first person's vertical field of view in degrees when the game's is unzoomed; 0: the game's */
    int ready_pose;         /* the weapon held up in the view while hip firing (fp_pose.c) */
} FpOptions;

void fp_options_defaults(FpOptions* options);
/* Reads `key = value` lines into options (defaults kept for what is not understood); returns the lines not understood. */
int fp_options_parse(const char* text, FpOptions* options);
int fp_options_key(const char* name, uint32_t* vk);
void fp_options_load(void);   /* options.ini in the mod's folder, or the defaults */
void fp_options_apply(void);  /* hands the settings to the view decision */
void fp_options_tick(void);   /* once a frame: the toggle key */
const FpOptions* fp_options(void);
const char* fp_options_default_text(void); /* mods/first-person/options.ini as shipped */
#ifdef FPV_TEST
void fp_options_set_key_for_test(int down);
#endif

void fp_view_reset(void);
void fp_view_settings(const FpSettings* settings);
uint32_t fp_view_request(const char* owner, uint32_t situations, uint32_t view, int32_t priority);
int fp_view_release(uint32_t handle);
uint32_t fp_view_in(uint32_t situation);
/* The situation this frame (G1 detects it; until then 0) and whether the camera is First Person's. */
void fp_view_set_situation(uint32_t situation);
uint32_t fp_view_situation(void);
int fp_view_active(void);

/* Aim (fp_aim.c): the view's forward from the eye towards where the game's own line of fire hits. `game` is the game's
 * camera pose this frame, `head` the head node's position. 1 with *forward, 0 to keep the game's direction. */
void fp_aim_reset(void);
int fp_aim_forward(const float* game, FpvVec3 eye, FpvVec3 head, float dt, FpvVec3* forward);
/* The smoothed hit distance: nearer within a frame or two, farther over about a fifth of a second. */
float fp_aim_smooth(float smoothed, float target, float dt);
#ifdef FPV_TEST
typedef int (*FpAimRaycast)(uint32_t query, const float* from, const float* dir, float length, int zero, float* result,
    int closest);
void fp_aim_set_for_test(FpAimRaycast raycast, uint32_t query);
#endif

/* Depth of field (fp_dof.c): off while firstPerson (the weapon is in its near blur), on again after when First Person
 * turned it off. Once a frame. */
void fp_dof_reset(void);
void fp_dof_tick(int firstPerson);
typedef struct FpDofFns
{
    int (*get)(const char* name);                     /* a bool render setting: 1, 0, or -1 not found */
    void (*set)(const char* name, const char* value);
} FpDofFns;
#ifdef FPV_TEST
void fp_dof_set_for_test(const FpDofFns* fns);
#endif

/* The body (fp_body.c): the Helldiver's capes and helmet hidden while `hide`, shown again after (or when ref changes). Once a
 * frame with the Helldiver's unit reference (0: none). */
#define FP_BODY_CAPES 6
void fp_body_reset(void);
void fp_body_tick(uint32_t ref, int hide);
int fp_body_hidden(void);
typedef struct FpBodyFns
{
    uint8_t (*alive)(uint32_t unit);
    uint32_t (*meshes)(uint32_t unit);
    uintptr_t (*mesh)(uint32_t unit, uint32_t index);
    void (*visibility)(uint32_t unit, uint32_t visible, uint8_t children);
    /* the Helldiver's cape units and first meshes: how many, or -1 with *why */
    int (*capes)(uint32_t ref, uint32_t* units, uintptr_t* meshes, int max, const char** why);
    int (*mask)(uintptr_t mesh, uint8_t* mask);  /* a mesh's visibility mask: 1 read */
} FpBodyFns;
#ifdef FPV_TEST
void fp_body_set_for_test(const FpBodyFns* fns);
#endif

/* The ready pose (fp_pose.c): for this frame's render, the hands and the weapon moved to the lower right of the view
 * (`view`: the camera's pose this frame), the barrel turned to `target`, and the arms reaching them (`facing`: the body's
 * level forward); `mix` 0..1 how much
 * First Person's view this frame is, `aiming` the game's own aim (no pose). fp_pose_restore puts every pose back after the
 * render. */
void fp_pose_reset(void);
void fp_pose_apply(uintptr_t diver, const float* view, FpvVec3 target, FpvVec3 facing, int aiming,
    float mix, float dt);
void fp_pose_restore(void);
float fp_pose_weight(void);
float fp_aim_distance(void);  /* the smoothed distance to where the game's line of fire hits (0: none yet) */

/* The camera filter (fp_camera.c): the camera at the Helldiver's eyes while the view is First Person's. */
void fp_camera_reset(void);
int fp_camera_filter(float* pose, void* context);
void fp_camera_after(void* context);  /* after the render: the ready pose put back */

#ifdef FPV_PROBE
/* A probe build's pose-setter census (fp_probe.c). */
void fp_probe_install(void);
void fp_probe_tick(void);
#endif

/* The table first_person_get_api returns. */
const FirstPersonApi* fp_api_table(void);

#endif
