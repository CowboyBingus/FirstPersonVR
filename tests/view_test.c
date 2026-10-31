/* First Person's view decision: other mods' requests by situation and priority, the player's settings, and the table
 * first_person_get_api hands out. */
#include "fp.h"
#include "harness/test.h"

TEST_GLOBALS;

static void SettingsWithoutRequests(void)
{
    fp_view_reset();
    CHECK_EQ(fp_view_in(FP_SITUATION_ON_FOOT), FP_VIEW_FIRST);
    CHECK_EQ(fp_view_in(FP_SITUATION_VEHICLE), FP_VIEW_THIRD);
    CHECK_EQ(fp_view_in(0), FP_VIEW_THIRD);
    CHECK_EQ(fp_view_active(), 0); /* no situation known */
    fp_view_set_situation(FP_SITUATION_ON_FOOT);
    CHECK_EQ(fp_view_active(), 1);
}

static void RequestKeepsFirstPersonInAVehicle(void)
{
    fp_view_reset();
    uint32_t handle = fp_view_request("vehicles", FP_SITUATION_VEHICLE | FP_SITUATION_MECH, FP_VIEW_FIRST, 0);
    CHECK(handle != 0);
    CHECK_EQ(fp_view_in(FP_SITUATION_VEHICLE), FP_VIEW_FIRST);
    CHECK_EQ(fp_view_in(FP_SITUATION_MECH), FP_VIEW_FIRST);
    CHECK_EQ(fp_view_in(FP_SITUATION_HELLPOD), FP_VIEW_THIRD);
    fp_view_set_situation(FP_SITUATION_VEHICLE);
    CHECK_EQ(fp_view_active(), 1);
    CHECK_LOG("vehicles asks for first person in situations 0x06 (priority 0)");
    CHECK_EQ(fp_view_release(handle), 1);
    CHECK_EQ(fp_view_release(handle), 0);
    CHECK_EQ(fp_view_in(FP_SITUATION_VEHICLE), FP_VIEW_THIRD);
}

static void HighestPriorityThenLatest(void)
{
    fp_view_reset();
    uint32_t low = fp_view_request("a", FP_SITUATION_ON_FOOT, FP_VIEW_THIRD, 5);
    fp_view_request("b", FP_SITUATION_ON_FOOT, FP_VIEW_FIRST, 1);
    CHECK_EQ(fp_view_in(FP_SITUATION_ON_FOOT), FP_VIEW_THIRD); /* 5 beats 1 */
    fp_view_request("c", FP_SITUATION_ON_FOOT, FP_VIEW_FIRST, 5);
    CHECK_EQ(fp_view_in(FP_SITUATION_ON_FOOT), FP_VIEW_FIRST); /* equal: the latest */
    fp_view_release(low);
    CHECK_EQ(fp_view_in(FP_SITUATION_ON_FOOT), FP_VIEW_FIRST);
}

static void PlayerStaysInCharge(void)
{
    fp_view_reset();
    fp_view_request("vehicles", FP_SITUATION_VEHICLE, FP_VIEW_FIRST, 100);
    FpSettings settings = {1, 0, FP_SITUATION_ON_FOOT};
    fp_view_settings(&settings);
    CHECK_EQ(fp_view_in(FP_SITUATION_VEHICLE), FP_VIEW_THIRD); /* mods may not change the view */
    settings.mods_may_change = 1;
    settings.enabled = 0;
    fp_view_settings(&settings);
    CHECK_EQ(fp_view_in(FP_SITUATION_VEHICLE), FP_VIEW_THIRD); /* First Person off */
    CHECK_EQ(fp_view_in(FP_SITUATION_ON_FOOT), FP_VIEW_THIRD);
}

static void BadRequestsAreRefused(void)
{
    fp_view_reset();
    CHECK_EQ(fp_view_request("x", 0, FP_VIEW_FIRST, 0), 0);
    CHECK_EQ(fp_view_request("x", 0x80, FP_VIEW_FIRST, 0), 0); /* no known situation bit */
    CHECK_EQ(fp_view_request("x", FP_SITUATION_SHIP, 7, 0), 0);
    CHECK_EQ(fp_view_release(0), 0);
    for (int i = 0; i < FP_MAX_REQUESTS; i++) CHECK(fp_view_request(NULL, FP_SITUATION_SHIP, FP_VIEW_FIRST, 0) != 0);
    CHECK_EQ(fp_view_request("late", FP_SITUATION_SHIP, FP_VIEW_FIRST, 0), 0);
    CHECK_LOG("late's request refused: 64 requests are held");
}

static void TableIsTheApi(void)
{
    fp_view_reset();
    const FirstPersonApi* api = fp_api_table();
    CHECK_EQ(api->version, FIRST_PERSON_API_VERSION);
    CHECK_EQ(api->size, sizeof(FirstPersonApi));
    uint32_t handle = api->request("vehicles", FP_SITUATION_VEHICLE, FP_VIEW_FIRST, 0);
    CHECK_EQ(api->view_in(FP_SITUATION_VEHICLE), FP_VIEW_FIRST);
    CHECK_EQ(api->release(handle), 1);
    CHECK_EQ(api->situation(), 0);
    CHECK_EQ(api->active(), 0);
}

int main(void)
{
    RUN(SettingsWithoutRequests);
    RUN(RequestKeepsFirstPersonInAVehicle);
    RUN(HighestPriorityThenLatest);
    RUN(PlayerStaysInCharge);
    RUN(BadRequestsAreRefused);
    RUN(TableIsTheApi);
    TEST_END();
}
