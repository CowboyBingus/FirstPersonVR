/* First Person's entry point: the host seam, the camera hook (ahead of VR Mode's), and the API other mods find with
 * first_person_api_find (include/first_person_api.h).
 *
 * G0 sets no camera filter and detects no situation yet, so nothing here changes the game.
 */
#include "fp.h"
#include "fpv_init.h"

HF_MOD_EXPORT const FirstPersonApi* first_person_get_api(uint32_t version)
{
    return version != 0 && version <= FIRST_PERSON_API_VERSION ? fp_api_table() : NULL;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    fp_view_reset();
    if (!fpv_host_init(api, mod)) return 1;
    fpv_camera_install(FPV_PRIORITY_FIRST_PERSON);
    fpv_log("mod", "%s %s loaded for game build %s (mod SDK %u, API %u)", FP_MOD_NAME, FPV_VERSION, FPV_GAME_BUILD,
        api->version, FIRST_PERSON_API_VERSION);
    return 0;
}
