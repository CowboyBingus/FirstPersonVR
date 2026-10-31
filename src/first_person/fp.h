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

/* The camera filter (fp_camera.c): the camera at the Helldiver's eyes while the view is First Person's. */
void fp_camera_reset(void);
int fp_camera_filter(float* pose, void* context);

#ifdef FPV_PROBE
/* A probe build's pose-setter census (fp_probe.c). */
void fp_probe_install(void);
void fp_probe_tick(void);
#endif

/* The table first_person_get_api returns. */
const FirstPersonApi* fp_api_table(void);

#endif
