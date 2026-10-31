/* The player's options: options.ini in the mod's folder, shipped with the defaults and read at load (the defaults when
 * it is missing), and the toggle key, polled once a frame while the game's window is in front. The mod's folder, not
 * %LOCALAPPDATA%: HellForge gives each session a %LOCALAPPDATA% of its own, so nothing written there is kept, and it
 * holds mod folders read-only while the game runs, so the mod only reads the file.
 *
 *     enabled = true                   ; First Person on at the start
 *     toggle_key = F6                  ; switches it on and off in game (F1-F12, A-Z, 0-9, or none)
 *     first_person_in = on_foot        ; the situations that are first person: on_foot, vehicle, mech, emplacement,
 *                                      ; hellpod, ship, ragdoll (only on_foot is detected so far)
 *     mods_may_change_view = true      ; other mods' requests count (a vehicle mod's first person, say)
 *     eye_forward = 0.12               ; metres from the head node along the view
 *     eye_up = 0.06                    ; and up
 *     fov = 74                         ; the vertical field of view in first person, in degrees (40-110), or game
 *     ready_pose = true                ; the weapon held up in the view while hip firing
 *
 * Unknown keys and bad values are logged and left at their defaults.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fp.h"

#ifndef FPV_TEST
#include <windows.h>
#endif

#define FP_DEFAULT_TOGGLE 0x75u /* VK_F6 */

#define FP_DEFAULT_FOV 74.0f /* Arma 3's: fovTop 0.75 (2 atan 0.75 = 73.7 degrees), about 106 wide at 16:9 */

static FpOptions g_options = {{1, 1, FP_SITUATION_ON_FOOT}, FP_DEFAULT_TOGGLE, 0.12f, 0.06f, FP_DEFAULT_FOV, 1};
static int g_keyDown;

/* mods/first-person/options.ini as shipped (a test keeps the two the same). */
static const char kDefaults[] =
    "; First Person's options, read when the game starts.\n"
    "enabled = true\n"
    "; switches First Person on and off in game: F1-F12, A-Z, 0-9, or none\n"
    "toggle_key = F6\n"
    "; first person in: on_foot, vehicle, mech, emplacement, hellpod, ship, ragdoll (comma separated)\n"
    "first_person_in = on_foot\n"
    "; other mods may change the view (a vehicle mod's first person, say)\n"
    "mods_may_change_view = true\n"
    "; the eye, from the head node: metres along the view, and up\n"
    "eye_forward = 0.12\n"
    "eye_up = 0.06\n"
    "; the field of view in first person: degrees top to bottom (40-110), or game for the game's own\n"
    "fov = 74\n"
    "; the weapon held up in the view while hip firing, as a soldier carries a rifle at the ready\n"
    "ready_pose = true\n";

static const struct
{
    const char* name;
    uint32_t bit;
} kSituations[] = {
    {"on_foot", FP_SITUATION_ON_FOOT}, {"vehicle", FP_SITUATION_VEHICLE}, {"mech", FP_SITUATION_MECH},
    {"emplacement", FP_SITUATION_EMPLACEMENT}, {"hellpod", FP_SITUATION_HELLPOD}, {"ship", FP_SITUATION_SHIP},
    {"ragdoll", FP_SITUATION_RAGDOLL},
};

void fp_options_defaults(FpOptions* options)
{
    memset(options, 0, sizeof *options);
    options->settings.enabled = 1;
    options->settings.mods_may_change = 1;
    options->settings.first_in = FP_SITUATION_ON_FOOT;
    options->toggle_key = FP_DEFAULT_TOGGLE;
    options->eye_forward = 0.12f;
    options->eye_up = 0.06f;
    options->fov = FP_DEFAULT_FOV;
    options->ready_pose = 1;
}

const FpOptions* fp_options(void) { return &g_options; }
const char* fp_options_default_text(void) { return kDefaults; }

/* Trims spaces at both ends, in place. */
static char* Trim(char* text)
{
    while (isspace((unsigned char)*text)) text++;
    size_t n = strlen(text);
    while (n > 0 && isspace((unsigned char)text[n - 1])) text[--n] = 0;
    return text;
}

static int Bool(const char* value, int* out)
{
    if (!strcmp(value, "true") || !strcmp(value, "on") || !strcmp(value, "1")) { *out = 1; return 1; }
    if (!strcmp(value, "false") || !strcmp(value, "off") || !strcmp(value, "0")) { *out = 0; return 1; }
    return 0;
}

/* A key name as a Windows virtual-key code: F1-F12, A-Z, 0-9, or none (0). */
int fp_options_key(const char* name, uint32_t* vk)
{
    char upper[8] = {0};
    size_t n = strlen(name);
    if (n == 0 || n >= sizeof upper) return 0;
    for (size_t i = 0; i < n; i++) upper[i] = (char)toupper((unsigned char)name[i]);
    if (!strcmp(upper, "NONE")) { *vk = 0; return 1; }
    if (n == 1 && (isupper((unsigned char)upper[0]) || isdigit((unsigned char)upper[0]))) { *vk = (uint32_t)upper[0]; return 1; }
    if (upper[0] != 'F') return 0;
    char* end;
    long f = strtol(upper + 1, &end, 10);
    if (*end != 0 || f < 1 || f > 12) return 0;
    *vk = 0x70u + (uint32_t)(f - 1);
    return 1;
}

static int Situations(char* value, uint32_t* out)
{
    uint32_t bits = 0;
    for (char* part = strtok(value, ","); part != NULL; part = strtok(NULL, ","))
    {
        part = Trim(part);
        if (!strcmp(part, "none")) continue;
        uint32_t bit = 0;
        for (size_t i = 0; i < sizeof kSituations / sizeof kSituations[0]; i++)
            if (!strcmp(part, kSituations[i].name)) bit = kSituations[i].bit;
        if (bit == 0) return 0;
        bits |= bit;
    }
    *out = bits;
    return 1;
}

static int Metres(const char* value, float* out)
{
    char* end;
    float f = strtof(value, &end);
    if (end == value || *end != 0 || !(f >= -0.5f && f <= 0.5f)) return 0;
    *out = f;
    return 1;
}

static int Degrees(const char* value, float* out)
{
    if (!strcmp(value, "game")) { *out = 0; return 1; }
    char* end;
    float f = strtof(value, &end);
    if (end == value || *end != 0 || !(f >= 40.0f && f <= 110.0f)) return 0;
    *out = f;
    return 1;
}

/* One `key = value` line into options; 1 when it was understood. */
static int Line(char* key, char* value, FpOptions* options)
{
    if (!strcmp(key, "enabled")) return Bool(value, &options->settings.enabled);
    if (!strcmp(key, "toggle_key")) return fp_options_key(value, &options->toggle_key);
    if (!strcmp(key, "first_person_in")) return Situations(value, &options->settings.first_in);
    if (!strcmp(key, "mods_may_change_view")) return Bool(value, &options->settings.mods_may_change);
    if (!strcmp(key, "eye_forward")) return Metres(value, &options->eye_forward);
    if (!strcmp(key, "eye_up")) return Metres(value, &options->eye_up);
    if (!strcmp(key, "fov")) return Degrees(value, &options->fov);
    if (!strcmp(key, "ready_pose")) return Bool(value, &options->ready_pose);
    return 0;
}

/* One line of the file: 1 understood (or empty), 0 not (logged). */
static int ParseLine(char* line, FpOptions* options)
{
    char* comment = strpbrk(line, ";#");
    if (comment != NULL) *comment = 0;
    char* equals = strchr(line, '=');
    char* key = Trim(line);
    if (*key == 0) return 1;
    if (equals == NULL) { fpv_log("options", "not understood: %s", key); return 0; }
    *equals = 0;
    key = Trim(key);
    char* value = Trim(equals + 1);
    if (Line(key, value, options)) return 1;
    fpv_log("options", "not understood: %s = %s (the default kept)", key, value);
    return 0;
}

int fp_options_parse(const char* text, FpOptions* options)
{
    int bad = 0;
    char line[256];
    for (const char* at = text; at != NULL && *at != 0;)
    {
        const char* end = strchr(at, '\n');
        size_t n = end != NULL ? (size_t)(end - at) : strlen(at);
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line ? n : sizeof line - 1), at);
        at = end != NULL ? end + 1 : NULL;
        bad += !ParseLine(line, options);
    }
    return bad;
}

#ifndef FPV_TEST
void fp_options_load(void)
{
    fp_options_defaults(&g_options);
    const char* folder = fpv_mod_folder();
    char path[1024], text[4096];
    if (folder == NULL || snprintf(path, sizeof path, "%s\\options.ini", folder) >= (int)sizeof path)
    {
        fpv_log("options", "the mod's folder is not known: the defaults are used");
        return;
    }
    wchar_t wide[1024];
    FILE* file = MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1024) > 0 ? _wfopen(wide, L"rb") : NULL;
    if (file == NULL) { fpv_log("options", "no %s: the defaults are used", path); return; }
    size_t n = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[n] = 0;
    fp_options_parse(text, &g_options);
    fpv_log("options", "%s: %s, toggle key 0x%02X, first person in 0x%02X, mods %s, fov %.0f", path,
        g_options.settings.enabled ? "on" : "off", g_options.toggle_key, g_options.settings.first_in,
        g_options.settings.mods_may_change ? "may change the view" : "may not change the view", g_options.fov);
}

/* The key is down and the game's window is the one in front (a key typed elsewhere is not the game's). */
static int KeyDown(uint32_t vk)
{
    HWND front = GetForegroundWindow();
    DWORD process = 0;
    if (front == NULL || (GetWindowThreadProcessId(front, &process), process != GetCurrentProcessId())) return 0;
    return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
}
#else
static int g_testKey;
void fp_options_set_key_for_test(int down) { g_testKey = down; }
static int KeyDown(uint32_t vk) { return g_testKey; }
void fp_options_load(void) { fp_options_defaults(&g_options); }
#endif

void fp_options_tick(void)
{
    if (g_options.toggle_key == 0) return;
    int down = KeyDown(g_options.toggle_key);
    if (down && !g_keyDown)
    {
        g_options.settings.enabled = !g_options.settings.enabled;
        fp_view_settings(&g_options.settings);
        fpv_log("options", "the toggle key: First Person %s", g_options.settings.enabled ? "on" : "off");
    }
    g_keyDown = down;
}

void fp_options_apply(void)
{
    g_keyDown = 0;
    fp_view_settings(&g_options.settings);
}
