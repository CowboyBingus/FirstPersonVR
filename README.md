# First Person VR for HellForge

A HellForge code mod, one DLL, that gives HELLDIVERS 2 a first-person camera, and later seated VR on top of it. It
changes your own view only, so friends and lobbies do not need it.

The mod is in progress. G0, the scaffold, is done: the mod loads and hooks the engine's render step so the camera can be
moved for a frame, but it sets no camera yet and changes nothing in the game. [docs/plan.md](docs/plan.md) has the
groups and what each one is tested with.

## Requirements

- HellForge with mod SDK 13 or newer, on game build 25480438 only.

## Building and checks

```bash
bash scripts/build.sh           # first_person_vr.dll beside hellforge-mod.json
bash scripts/build.sh --test    # the DLL and every tests/*_test.c, run under Proton Experimental's wine
bash scripts/lint.sh            # HellForge's structure lint
bash scripts/package.sh         # a test-build zip; never publish it
```

`scripts/build.sh` uses Zig at `~/.local/share/zig-native/zig` (or `$HELLFORGE_ZIG`, or `--zig`), and the mod SDK of a
HellForge-Open checkout at `~/HellForge` (or `$HELLFORGE_OPEN`, or `--hellforge`). Tests are Windows executables and run
under the wine in Proton Experimental (`$WINE` overrides it); the test prefix and temporary files live under `build/`.

## License

Zero-Clause BSD ([LICENSE](LICENSE)).
