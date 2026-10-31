/* First Person's API for other HellForge code mods (the First Person mod, cowboybingus.first-person, first_person.dll).
 *
 * A mod can ask for a view in a situation, e.g. first person while the player drives a vehicle, where First Person
 * would otherwise go back to third person. Requests are held until released; in each situation the held request with
 * the highest priority decides (equal priorities: the latest), and with none the player's own settings do. The player
 * stays in charge: with First Person switched off, or with "mods may change the view" off in its options, requests are
 * kept but not used.
 *
 * Usage, from the game thread (an event's or on_game_thread's callback), after HF_EVENT_GAME_LOADED (mods load in an
 * order the player picks, so look the API up then, not in hellforge_mod_init):
 *
 *     const FirstPersonApi* fp = first_person_api_find();
 *     if (fp != NULL)
 *         g_request = fp->request("my-vehicle-mod", FP_SITUATION_VEHICLE, FP_VIEW_FIRST, 0);
 *     ...
 *     if (fp != NULL) fp->release(g_request);
 *
 * NULL means First Person is not loaded (not installed, switched off on the Mods page, or another game build): carry on
 * without it. Every function is safe to call with any value; none may be called from another thread.
 *
 * Versions: FIRST_PERSON_API_VERSION grows when functions are added, at the end only. first_person_get_api(v) returns
 * the table for any v up to the mod's own, and table->size says how much of it there is.
 */
#ifndef FIRST_PERSON_API_H
#define FIRST_PERSON_API_H

#include <stdint.h>

#define FIRST_PERSON_API_VERSION 1u

/* Views. */
#define FP_VIEW_THIRD 1u /* the game's own camera */
#define FP_VIEW_FIRST 2u /* the camera at the diver's eyes */

/* Situations, as bits (a request can cover several). */
#define FP_SITUATION_ON_FOOT 0x01u     /* walking, running, aiming, prone, swimming */
#define FP_SITUATION_VEHICLE 0x02u     /* driving or riding a vehicle */
#define FP_SITUATION_MECH 0x04u        /* in an exosuit */
#define FP_SITUATION_EMPLACEMENT 0x08u /* on a turret or emplacement */
#define FP_SITUATION_HELLPOD 0x10u     /* the drop, until the diver climbs out */
#define FP_SITUATION_SHIP 0x20u        /* on the ship (the Destroyer) */
#define FP_SITUATION_RAGDOLL 0x40u     /* knocked down, flying, dead */
#define FP_SITUATION_ALL 0x7Fu

typedef struct FirstPersonApi
{
    uint32_t version; /* the mod's FIRST_PERSON_API_VERSION */
    uint32_t size;    /* sizeof(FirstPersonApi) of the mod */

    /* Asks for `view` in the `situations` (FP_SITUATION_... bits) while held. `owner` names the asking mod in First
     * Person's log (copied; up to 63 bytes). A handle (never 0), or 0 when refused: an unknown view, no situation
     * bits, or 64 requests held already. */
    uint32_t (*request)(const char* owner, uint32_t situations, uint32_t view, int32_t priority);
    /* Drops a request. 1 dropped, 0 not held. */
    int (*release)(uint32_t handle);
    /* The view First Person uses in one situation (one FP_SITUATION_... bit), with the requests and settings as they
     * are now: FP_VIEW_FIRST or FP_VIEW_THIRD. */
    uint32_t (*view_in)(uint32_t situation);
    /* The player's situation this frame (one FP_SITUATION_... bit), or 0 when not known (menus, loading). */
    uint32_t (*situation)(void);
    /* 1 when this frame's camera is First Person's. */
    int (*active)(void);
} FirstPersonApi;

/* first_person.dll's export. */
typedef const FirstPersonApi* (*FirstPersonGetApi)(uint32_t version);
#define FIRST_PERSON_GET_API "first_person_get_api"

#ifdef _WIN32
#include <windows.h>
/* The loaded First Person's API, or NULL when it is not loaded. */
static inline const FirstPersonApi* first_person_api_find(void)
{
    HMODULE module = GetModuleHandleA("first_person.dll");
    if (module == NULL) return NULL;
    FirstPersonGetApi get = (FirstPersonGetApi)(void*)GetProcAddress(module, FIRST_PERSON_GET_API);
    return get != NULL ? get(FIRST_PERSON_API_VERSION) : NULL;
}
#endif

#endif
