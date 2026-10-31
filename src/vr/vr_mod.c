/* VR Mode's entry point: the host seam and the camera hook (after First Person's, so the headset's pose goes on top of
 * First Person's camera). With First Person loaded, VR follows its view through its API; without it, VR is seated third
 * person (the game's camera with the head on top).
 *
 * G0 has no OpenXR yet: the mod only looks First Person up when the game has loaded and says what it found.
 */
#include "first_person_api.h"
#include "fpv_init.h"

#define VR_MOD_NAME "VR Mode"

static const FirstPersonApi* g_firstPerson;

/* The exe's code reads as shipped only once the game has loaded, so the camera hook waits for it. */
static void OnGameLoaded(HfMod* mod, HfEvent event, void* user)
{
    fpv_host_modules();
    fpv_camera_install(FPV_PRIORITY_VR);
    g_firstPerson = first_person_api_find();
    if (g_firstPerson != NULL)
        fpv_log("mod", "First Person is loaded (API %u): VR follows its view", g_firstPerson->version);
    else
        fpv_log("mod", "First Person is not loaded: VR is seated third person");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    g_firstPerson = NULL;
    if (!fpv_host_init(api, mod)) return 1;
    if (api->on(mod, HF_EVENT_GAME_LOADED, OnGameLoaded, NULL) != 0)
        fpv_log("mod", "HF_EVENT_GAME_LOADED could not be registered");
    fpv_log("mod", "%s %s loaded for game build %s (mod SDK %u)", VR_MOD_NAME, FPV_VERSION, FPV_GAME_BUILD, api->version);
    return 0;
}
