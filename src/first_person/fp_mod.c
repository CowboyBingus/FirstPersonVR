/* First Person's entry point: the host seam, the camera hook (ahead of VR Mode's), and the API other mods find with
 * first_person_api_find (include/first_person_api.h).
 *
 * G1 step 2: on foot, the camera sits at the Helldiver's eyes (fp_camera.c); everything else stays third person.
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
    fpv_host_modules();
    if (fpv_camera_install(FPV_PRIORITY_FIRST_PERSON))
    {
        fpv_camera_set_filter(fp_camera_filter, NULL);
        fpv_camera_set_after(fp_camera_after);
    }
#ifdef FPV_PROBE
    fp_probe_install();
#endif
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    fp_view_reset();
    fp_camera_reset();
    fp_aim_reset();
    fp_body_reset();
    fp_pose_reset();
    fp_dof_reset();
    if (!fpv_host_init(api, mod)) return 1;
    fp_options_load();
    fp_options_apply();
    if (api->on(mod, HF_EVENT_GAME_LOADED, OnGameLoaded, NULL) != 0)
        fpv_log("mod", "HF_EVENT_GAME_LOADED could not be registered: no camera");
    fpv_log("mod", "%s %s loaded for game build %s (mod SDK %u, API %u)", FP_MOD_NAME, FPV_VERSION, FPV_GAME_BUILD,
        api->version, FIRST_PERSON_API_VERSION);
    return 0;
}
