/* The body: the Helldiver's capes and helmet hidden in first person and shown again after, one hidden already left alone, one the
 * game shows again hidden again, and a changed Helldiver's capes shown. */
#include <string.h>
#include "fp.h"
#include "harness/test.h"

TEST_GLOBALS;

#define CAPES 2

static struct
{
    uint32_t units[CAPES];
    uint8_t masks[CAPES];
    uint8_t alive[CAPES];
    int count;          /* what capes() answers (-1: the chain does not read) */
    int resolves, hides, shows;
} f;

static int Index(uint32_t unit)
{
    for (int i = 0; i < CAPES; i++)
        if (f.units[i] == unit) return i;
    return -1;
}

static uint8_t Alive(uint32_t unit) { int i = Index(unit); return i >= 0 && f.alive[i]; }
static uint32_t Meshes(uint32_t unit) { return 1; }
static uintptr_t Mesh(uint32_t unit, uint32_t index) { return (uintptr_t)(Index(unit) + 1); }

static void Visibility(uint32_t unit, uint32_t visible, uint8_t children)
{
    int i = Index(unit);
    if (i < 0) return;
    f.masks[i] = visible ? 0xFF : 0;
    if (visible) f.shows++;
    else f.hides++;
}

static int Capes(uint32_t ref, uint32_t* units, uintptr_t* meshes, int max, const char** why)
{
    f.resolves++;
    if (f.count < 0) { *why = "the chain does not read"; return -1; }
    for (int i = 0; i < f.count && i < max; i++)
    {
        units[i] = f.units[i] + (ref - 1) * 100;
        meshes[i] = Mesh(units[i], 0);
    }
    return f.count;
}

static int Mask(uintptr_t mesh, uint8_t* mask)
{
    if (mesh == 0 || mesh > CAPES) return 0;
    *mask = f.masks[mesh - 1];
    return 1;
}

static const FpBodyFns kFns = {Alive, Meshes, Mesh, Visibility, Capes, Mask};

/* Two capes of Helldiver 1 (units 101, 102), both shown and alive. */
static void Setup(void)
{
    memset(&f, 0, sizeof f);
    f.units[0] = 101;
    f.units[1] = 102;
    f.masks[0] = f.masks[1] = 0xFF;
    f.alive[0] = f.alive[1] = 1;
    f.count = CAPES;
    fp_body_set_for_test(&kFns);
}

static void CapesHiddenInFirstPersonAndShownAfter(void)
{
    Setup();
    fp_body_tick(1, 0);
    CHECK_EQ(f.hides, 0);
    fp_body_tick(1, 1);
    CHECK_EQ(f.hides, 2);
    CHECK(f.masks[0] == 0 && f.masks[1] == 0);
    CHECK_EQ(fp_body_hidden(), 1);
    fp_body_tick(1, 1);
    CHECK_EQ(f.hides, 2);       /* hidden once */
    CHECK_EQ(f.resolves, 1);    /* found once */
    fp_body_tick(1, 0);
    CHECK_EQ(f.shows, 2);
    CHECK(f.masks[0] == 0xFF && f.masks[1] == 0xFF);
    CHECK_EQ(fp_body_hidden(), 0);
    CHECK_LOG("wears 2 cape and helmet units");
    CHECK_LOG("the cape and helmet hidden (2 of 2)");
    CHECK_LOG("the cape and helmet shown");
}

static void CapeHiddenAlreadyIsLeftHidden(void)
{
    Setup();
    f.masks[1] = 0;             /* Megapack's Hide Cape */
    fp_body_tick(1, 1);
    CHECK_EQ(f.hides, 1);
    fp_body_tick(1, 0);
    CHECK_EQ(f.shows, 1);
    CHECK_EQ(f.masks[1], 0);
}

static void CapeTheGameShowsIsHiddenAgain(void)
{
    Setup();
    fp_body_tick(1, 1);
    f.masks[0] = 0xFF;          /* the hellpod's exit */
    fp_body_tick(1, 1);
    fp_body_tick(1, 1);
    CHECK_EQ(f.masks[0], 0);
}

static void ChangedHelldiverShowsTheOldCapes(void)
{
    Setup();
    fp_body_tick(1, 1);
    fp_body_tick(2, 1);         /* reinforced: the corpse's capes shown, the new ones (201, 202 in Capes) not ours */
    CHECK_EQ(f.shows, 2);
    CHECK_EQ(f.resolves, 2);
}

static void DeadCapeIsNotShown(void)
{
    Setup();
    fp_body_tick(1, 1);
    f.alive[0] = 0;
    fp_body_tick(1, 0);
    CHECK_EQ(f.shows, 1);
}

static void UnreadableChainIsTriedAgainLater(void)
{
    Setup();
    f.count = -1;
    fp_body_tick(1, 1);
    CHECK_EQ(f.resolves, 1);
    for (int i = 0; i < 30; i++) fp_body_tick(1, 1);
    CHECK_EQ(f.resolves, 1);
    f.count = CAPES;
    fp_body_tick(1, 1);
    CHECK_EQ(f.resolves, 2);
    CHECK_EQ(f.hides, 2);
    CHECK_LOG("the cape and helmet are not found (the chain does not read)");
}

static void NoCapeYetIsTriedAgainLater(void)
{
    Setup();
    f.count = 0;
    for (int i = 0; i < 31; i++) fp_body_tick(1, 1);
    f.count = CAPES;
    fp_body_tick(1, 1);
    CHECK_EQ(f.hides, 2);
    CHECK_LOG("it wears none yet");
}

static void EngineMissingLeavesTheCape(void)
{
    fp_body_set_for_test(NULL);
    fp_body_tick(1, 1);
    CHECK_EQ(fp_body_hidden(), 0);
}

int main(void)
{
    RUN(CapesHiddenInFirstPersonAndShownAfter);
    RUN(CapeHiddenAlreadyIsLeftHidden);
    RUN(CapeTheGameShowsIsHiddenAgain);
    RUN(ChangedHelldiverShowsTheOldCapes);
    RUN(DeadCapeIsNotShown);
    RUN(UnreadableChainIsTriedAgainLater);
    RUN(NoCapeYetIsTriedAgainLater);
    RUN(EngineMissingLeavesTheCape);
    TEST_END();
}
