/* First Person's entry point: the host seam, the camera hook (ahead of VR Mode's), and the API other mods find with
 * first_person_api_find (include/first_person_api.h).
 *
 * G1 step 1: the probe (fp_probe.c) logs the Helldiver's scene graph; nothing here changes the game yet.
 */
#include "fp.h"
#include "fpv_init.h"

HF_MOD_EXPORT const FirstPersonApi* first_person_get_api(uint32_t version)
{
    return version != 0 && version <= FIRST_PERSON_API_VERSION ? fp_api_table() : NULL;
}

/* The exe's code reads as shipped only once the game has loaded (HellForge's own exe hooks wait for it too). */
static void OnGameLoaded(HfMod* mod, HfEvent event, void* user)
{
    if (fpv_camera_install(FPV_PRIORITY_FIRST_PERSON)) fpv_camera_set_filter(fp_probe_filter, NULL);
}

static void OnWeapon(HfMod* mod, const HfEventInfo* event, void* user)
{
    if (event == NULL || event->size < sizeof(HfWeaponEvent)) return;
    fp_probe_avatar(((const HfWeaponEvent*)event)->avatar);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    fp_view_reset();
    if (!fpv_host_init(api, mod)) return 1;
    if (api->on(mod, HF_EVENT_GAME_LOADED, OnGameLoaded, NULL) != 0)
        fpv_log("mod", "HF_EVENT_GAME_LOADED could not be registered: no camera");
    if (api->size < offsetof(HfApi, subscribe) + sizeof(api->subscribe) ||
        api->subscribe(mod, HF_ON_WEAPON, OnWeapon, NULL, 0) != 1)
        fpv_log("mod", "HF_ON_WEAPON could not be subscribed: the probe has no Helldiver");
    fpv_log("mod", "%s %s loaded for game build %s (mod SDK %u, API %u)", FP_MOD_NAME, FPV_VERSION, FPV_GAME_BUILD,
        api->version, FIRST_PERSON_API_VERSION);
    return 0;
}
