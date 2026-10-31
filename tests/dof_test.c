/* Depth of field: off in first person when on, on again after only when First Person turned it off, and turned off
 * again when the game turns it on while in first person. */
#include <string.h>
#include "fp.h"
#include "harness/test.h"

TEST_GLOBALS;

static struct
{
    int value;          /* the setting: 1, 0, -1 not found */
    int sets, gets;
} f;

static int Get(const char* name) { f.gets++; return strcmp(name, "dof_enabled") == 0 ? f.value : -1; }

static void Set(const char* name, const char* value)
{
    if (strcmp(name, "dof_enabled") != 0) return;
    f.sets++;
    f.value = strcmp(value, "true") == 0;
}

static const FpDofFns kFns = {Get, Set};

static void Setup(int value)
{
    memset(&f, 0, sizeof f);
    f.value = value;
    fp_dof_set_for_test(&kFns);
}

static void OffInFirstPersonAndOnAfter(void)
{
    Setup(1);
    fp_dof_tick(0);
    CHECK_EQ(f.sets, 0);
    fp_dof_tick(1);
    CHECK_EQ(f.value, 0);
    fp_dof_tick(0);
    CHECK_EQ(f.value, 1);
    CHECK_EQ(f.sets, 2);
    CHECK_LOG("depth of field off in first person");
    CHECK_LOG("depth of field on again");
}

static void OffAlreadyIsLeftOff(void)
{
    Setup(0);
    fp_dof_tick(1);
    fp_dof_tick(0);
    CHECK_EQ(f.sets, 0);
    CHECK_EQ(f.value, 0);
}

static void TurnedOnByTheGameIsTurnedOffAgain(void)
{
    Setup(1);
    fp_dof_tick(1);
    f.value = 1;                /* the options menu's Apply */
    for (int i = 0; i < 61; i++) fp_dof_tick(1);
    CHECK_EQ(f.value, 0);
    CHECK(f.gets <= 3);         /* checked once a second, not every frame */
}

static void UnknownSettingIsLeftAlone(void)
{
    Setup(-1);
    fp_dof_tick(1);
    fp_dof_tick(0);
    CHECK_EQ(f.sets, 0);
}

static void EngineMissingDoesNothing(void)
{
    fp_dof_set_for_test(NULL);
    fp_dof_tick(1);
    fp_dof_tick(0);
}

int main(void)
{
    RUN(OffInFirstPersonAndOnAfter);
    RUN(OffAlreadyIsLeftOff);
    RUN(TurnedOnByTheGameIsTurnedOffAgain);
    RUN(UnknownSettingIsLeftAlone);
    RUN(EngineMissingDoesNothing);
    TEST_END();
}
