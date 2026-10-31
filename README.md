# First Person and VR Mode for HellForge

Two HellForge code mods for HELLDIVERS 2, each its own switch on the Mods page:

- **First Person** (`mods/first-person`): a first-person camera. Other mods can ask it for first person, or third
  person, in a situation such as driving a vehicle ([For mod authors](#for-mod-authors)).
- **VR Mode** (`mods/vr-mode`): seated VR through OpenXR. With First Person on, you see through the diver's eyes;
  without it, the game's own camera in VR.

Both change your own view only, so friends and lobbies do not need them.

The mods are in progress. First Person puts the camera at the Helldiver's eyes on foot in missions (seen in game
2026-10-31); aim, the helmet and arms, options and the other situations are next. VR Mode loads and does nothing yet. [docs/plan.md](docs/plan.md) has the groups and what each one is tested with.

## Requirements

- HellForge with mod SDK 13 or newer, on game build 25480438 only.

## For mod authors

Copy [include/first_person_api.h](include/first_person_api.h) into your mod. After `HF_EVENT_GAME_LOADED`, on the game
thread:

```c
const FirstPersonApi* fp = first_person_api_find(); /* NULL: First Person is not loaded */
if (fp != NULL)
    g_request = fp->request("my-vehicle-mod", FP_SITUATION_VEHICLE, FP_VIEW_FIRST, 0);
/* ... and when your mod no longer wants it: */
if (fp != NULL) fp->release(g_request);
```

In each situation the held request with the highest priority decides (equal priorities: the latest one); with none,
the player's settings do. The player stays in charge: with First Person switched off, or "mods may change the view" off
in its options, requests are kept but not used. The header documents every function.

## Building and checks

```bash
bash scripts/build.sh           # mods/first-person/first_person.dll and mods/vr-mode/vr_mode.dll
bash scripts/build.sh --test    # the DLLs and every tests/*_test.c, run under Proton Experimental's wine
bash scripts/lint.sh            # HellForge's structure lint
bash scripts/package.sh         # a test-build zip of each mod; never publish them
```

`scripts/build.sh` uses Zig at `~/.local/share/zig-native/zig` (or `$HELLFORGE_ZIG`, or `--zig`), and the mod SDK of a
HellForge-Open checkout at `~/HellForge` (or `$HELLFORGE_OPEN`, or `--hellforge`). Tests are Windows executables and run
under the wine in Proton Experimental (`$WINE` overrides it); the test prefix and temporary files live under `build/`.

## License

Zero-Clause BSD ([LICENSE](LICENSE)).
