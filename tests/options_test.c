/* The options file: its lines, bad values left at their defaults, key names, and the toggle key. */
#include <string.h>
#include "fp.h"
#include "harness/fake_host.h"
#include "harness/test.h"

TEST_GLOBALS;

static int Near(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }

static void DefaultsAreFirstPersonOnFoot(void)
{
    FpOptions o;
    fp_options_defaults(&o);
    CHECK_EQ(o.settings.enabled, 1);
    CHECK_EQ(o.settings.mods_may_change, 1);
    CHECK_EQ(o.settings.first_in, FP_SITUATION_ON_FOOT);
    CHECK_EQ(o.toggle_key, 0x75); /* F6 */
    CHECK(Near(o.eye_forward, 0.12f) && Near(o.eye_up, 0.06f));
}

static void TheWrittenFileReadsAsTheDefaults(void)
{
    FpOptions o, d;
    fp_options_defaults(&d);
    memset(&o, 0, sizeof o);
    CHECK_EQ(fp_options_parse(fp_options_default_text(), &o), 0);
    CHECK(memcmp(&o, &d, sizeof o) == 0);
}

static void LinesAreRead(void)
{
    FpOptions o;
    fp_options_defaults(&o);
    int bad = fp_options_parse(
        "; a comment\r\n"
        "enabled = false\r\n"
        "  toggle_key=f9   # trailing\n"
        "first_person_in = on_foot, vehicle ,mech\n"
        "mods_may_change_view = off\n"
        "eye_forward = 0.2\n"
        "eye_up = -0.05\n"
        "\n", &o);
    CHECK_EQ(bad, 0);
    CHECK_EQ(o.settings.enabled, 0);
    CHECK_EQ(o.toggle_key, 0x78);
    CHECK_EQ(o.settings.first_in, FP_SITUATION_ON_FOOT | FP_SITUATION_VEHICLE | FP_SITUATION_MECH);
    CHECK_EQ(o.settings.mods_may_change, 0);
    CHECK(Near(o.eye_forward, 0.2f) && Near(o.eye_up, -0.05f));
}

static void BadValuesKeepTheDefaults(void)
{
    FpOptions o;
    fp_options_defaults(&o);
    int bad = fp_options_parse("enabled = maybe\ntoggle_key = F13\nfirst_person_in = boat\neye_up = 3\ncolour = red\nnoequals\n", &o);
    CHECK_EQ(bad, 6);
    CHECK_EQ(o.settings.enabled, 1);
    CHECK_EQ(o.toggle_key, 0x75);
    CHECK_EQ(o.settings.first_in, FP_SITUATION_ON_FOOT);
    CHECK(Near(o.eye_up, 0.06f));
    CHECK_LOG("not understood: toggle_key = F13 (the default kept)");
}

static void KeyNames(void)
{
    uint32_t vk = 99;
    CHECK(fp_options_key("F1", &vk) && vk == 0x70);
    CHECK(fp_options_key("f12", &vk) && vk == 0x7B);
    CHECK(fp_options_key("k", &vk) && vk == 'K');
    CHECK(fp_options_key("7", &vk) && vk == '7');
    CHECK(fp_options_key("None", &vk) && vk == 0);
    CHECK(!fp_options_key("F0", &vk));
    CHECK(!fp_options_key("ctrl", &vk));
    CHECK(!fp_options_key("", &vk));
}

static void ToggleKeySwitchesOnPress(void)
{
    fp_view_reset();
    fp_options_load();
    fp_options_apply();
    fp_view_set_situation(FP_SITUATION_ON_FOOT);
    CHECK_EQ(fp_view_active(), 1);
    fp_options_set_key_for_test(1);
    fp_options_tick();
    CHECK_EQ(fp_view_active(), 0);
    fp_options_tick();                 /* held: no second switch */
    CHECK_EQ(fp_view_active(), 0);
    fp_options_set_key_for_test(0);
    fp_options_tick();
    fp_options_set_key_for_test(1);
    fp_options_tick();
    CHECK_EQ(fp_view_active(), 1);
    CHECK_LOG("the toggle key: First Person off");
    fp_options_set_key_for_test(0);
}

int main(void)
{
    RUN(DefaultsAreFirstPersonOnFoot);
    RUN(TheWrittenFileReadsAsTheDefaults);
    RUN(LinesAreRead);
    RUN(BadValuesKeepTheDefaults);
    RUN(KeyNames);
    RUN(ToggleKeySwitchesOnPress);
    TEST_END();
}
