# First Person and VR Mode for HellForge

Two HellForge code mods for HELLDIVERS 2, each its own switch on the Mods page:

- **First Person** (`mods/first-person`): a first-person camera. Other mods can ask it for first person, or third
  person, in a situation such as driving a vehicle ([For mod authors](#for-mod-authors)).
- **VR Mode** (`mods/vr-mode`): seated VR through OpenXR. With First Person on, you see through the diver's eyes;
  without it, the game's own camera in VR.

Both change your own view only, so friends and lobbies do not need them.

The mods are in progress. First Person, on foot in missions (seen in game 2026-10-31):

- The camera at the Helldiver's eyes, steadied so running does not bob the view, down to the ground when prone.
- A wider view, Arma 3's 74 degrees top to bottom (the game's is 55), with the weapon held up at the lower right of
  the view while hip firing, as a soldier carries a rifle at the ready, its scope and other attachments on it. It
  goes back to the game's own pose while you aim (right mouse), reload, throw or melee.
- Your cape and helmet are hidden while you look through your Helldiver's eyes (the cape hung in the view looking
  down, the helmet's brim across the top), and shown again in third person. One hidden by another mod stays hidden.
- Depth of field is off in first person (the game focuses it a few metres out, so the weapon in your hands was always
  blurred), and back to your setting in third person.
- Shots land at the centre of the screen: aiming shows the weapon and the game's reticle there.
- The game's own first-person aim (middle mouse while aiming: sights and scopes) is kept, and First Person comes back
  when the aim ends.
- Third person when the Helldiver dies, in vehicles and the hellpod, on the ship and after a mission; the switches blend
  over a quarter second.
- Options and a toggle key (F6), below.

Not checked yet: other players in the lobby, emotes, vehicles in first person through a mod's request. VR Mode loads and
does nothing yet. [docs/plan.md](docs/plan.md) has the groups and what each one is tested with.

## Options

`options.ini` in the mod's folder (`mods/first-person`), read when the game starts:

| Option | Default | What it does |
| --- | --- | --- |
| `enabled` | `true` | First Person on at the start |
| `toggle_key` | `F6` | switches it on and off in game (F1-F12, A-Z, 0-9, or `none`) |
| `first_person_in` | `on_foot` | the situations that are first person: `on_foot`, `vehicle`, `mech`, `emplacement`, `hellpod`, `ship`, `ragdoll` (only `on_foot` is detected so far) |
| `mods_may_change_view` | `true` | other mods' requests count |
| `eye_forward`, `eye_up` | `0.12`, `0.06` | the eye's place from the head, in metres (-0.5 to 0.5) |
| `fov` | `74` | the field of view in first person, degrees top to bottom (40-110), or `game` for the game's own 55 |
| `ready_pose` | `true` | the weapon held up in the view while hip firing (`false`: the game's pose, the weapon at the belt) |

The mod's log (the session's `mod-cowboybingus.first-person.log`) says what it read, and why the view is third person
when it switches.

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
