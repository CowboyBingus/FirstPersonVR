/* First Person's parts: the view decision (fp_view.c) and its entry (fp_mod.c). */
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

/* G1 step 1's probe (fp_probe.c): the Helldiver to log, and the camera filter that logs it (and declines the frame). */
void fp_probe_avatar(uint32_t avatar);
int fp_probe_filter(float* pose, void* context);

/* The table first_person_get_api returns. */
const FirstPersonApi* fp_api_table(void);

#endif
