/* Which view each situation gets: other mods' requests (first_person_api.h), else the player's settings. The decision
 * is a scan of at most FP_MAX_REQUESTS rows; it runs when asked, on the game thread. */
#include <stdio.h>
#include <string.h>
#include "fp.h"

typedef struct Request
{
    uint32_t handle;   /* 0: the row is free */
    uint32_t situations;
    uint32_t view;
    int32_t priority;
    uint32_t order;    /* when it was made: equal priorities go to the latest */
    char owner[64];
} Request;

static Request g_requests[FP_MAX_REQUESTS];
static uint32_t g_nextHandle = 1, g_nextOrder = 1;
static FpSettings g_settings = {1, 1, FP_SITUATION_ON_FOOT};
static uint32_t g_situation;

void fp_view_reset(void)
{
    memset(g_requests, 0, sizeof g_requests);
    g_nextHandle = g_nextOrder = 1;
    g_settings.enabled = 1;
    g_settings.mods_may_change = 1;
    g_settings.first_in = FP_SITUATION_ON_FOOT;
    g_situation = 0;
}

void fp_view_settings(const FpSettings* settings)
{
    if (settings != NULL) g_settings = *settings;
}

static const char* Owner(const char* owner) { return owner != NULL && owner[0] != 0 ? owner : "a mod"; }

uint32_t fp_view_request(const char* owner, uint32_t situations, uint32_t view, int32_t priority)
{
    situations &= FP_SITUATION_ALL;
    if ((view != FP_VIEW_FIRST && view != FP_VIEW_THIRD) || situations == 0) return 0;
    for (int i = 0; i < FP_MAX_REQUESTS; i++)
    {
        Request* row = &g_requests[i];
        if (row->handle != 0) continue;
        row->handle = g_nextHandle++;
        if (g_nextHandle == 0) g_nextHandle = 1;
        row->situations = situations;
        row->view = view;
        row->priority = priority;
        row->order = g_nextOrder++;
        snprintf(row->owner, sizeof row->owner, "%s", Owner(owner));
        fpv_log("api", "%s asks for %s in situations 0x%02X (priority %d)", row->owner,
            view == FP_VIEW_FIRST ? "first person" : "third person", situations, priority);
        return row->handle;
    }
    fpv_log("api", "%s's request refused: %d requests are held", Owner(owner), FP_MAX_REQUESTS);
    return 0;
}

int fp_view_release(uint32_t handle)
{
    if (handle == 0) return 0;
    for (int i = 0; i < FP_MAX_REQUESTS; i++)
        if (g_requests[i].handle == handle)
        {
            fpv_log("api", "%s released its request", g_requests[i].owner);
            memset(&g_requests[i], 0, sizeof g_requests[i]);
            return 1;
        }
    return 0;
}

/* The deciding request for one situation bit, or NULL. */
static const Request* Deciding(uint32_t situation)
{
    const Request* best = NULL;
    for (int i = 0; i < FP_MAX_REQUESTS; i++)
    {
        const Request* row = &g_requests[i];
        if (row->handle == 0 || (row->situations & situation) == 0) continue;
        if (best == NULL || row->priority > best->priority || (row->priority == best->priority && row->order > best->order))
            best = row;
    }
    return best;
}

uint32_t fp_view_in(uint32_t situation)
{
    /* One bit only: the lowest one asked about. */
    situation &= FP_SITUATION_ALL;
    situation &= ~situation + 1u;
    if (!g_settings.enabled || situation == 0) return FP_VIEW_THIRD;
    const Request* request = g_settings.mods_may_change ? Deciding(situation) : NULL;
    if (request != NULL) return request->view;
    return (g_settings.first_in & situation) != 0 ? FP_VIEW_FIRST : FP_VIEW_THIRD;
}

void fp_view_set_situation(uint32_t situation) { g_situation = situation & FP_SITUATION_ALL; }
uint32_t fp_view_situation(void) { return g_situation; }
int fp_view_active(void) { return g_situation != 0 && fp_view_in(g_situation) == FP_VIEW_FIRST; }

/* ---- the exported table ------------------------------------------------------------------------------------------ */

static const FirstPersonApi kApi = {
    FIRST_PERSON_API_VERSION,
    sizeof(FirstPersonApi),
    fp_view_request,
    fp_view_release,
    fp_view_in,
    fp_view_situation,
    fp_view_active,
};

const FirstPersonApi* fp_api_table(void) { return &kApi; }
