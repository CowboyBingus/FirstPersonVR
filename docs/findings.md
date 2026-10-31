# Findings (game build 25480438)

What the mods rely on, and how each fact was established. "Live" means read in the running game (HellForge on Linux,
2026-10-31), through the test harness's in-game Lua (`stingray` API and LuaJIT's ffi).

## The camera

- The script render step is `helldivers2.exe+0x689BC0`. A camera unit pose set before it is the one the frame renders
  with (HellForge's terminal camera found this; live here in missions too).
- The exe's code matches its shipped bytes only after `HF_EVENT_GAME_LOADED`. Checked before that, the pins fail. Hook
  then, as HellForge does for its own exe hooks.
- The main world (`exe+0x317DE0`, `Application.main_world`) has one unit with a camera, on the ship and in missions
  (live). Its scene graph's node 0 local pose is the camera's world pose. Camera: vertical fov 0.96, near range 0.05.
- The other worlds (six more in a mission) each have one camera with the default fov 0.785; they are not the player's
  view.
- `stingray.Camera` has `set_mode` with `MONO` and `STEREO`, `set_frustum`, `set_frustum_half_angles` (an asymmetric
  frustum) and `set_post_projection_transform`. `Application` has `create_viewport`, `create_render_target` and
  `render_world`. These are what VR Mode will look at first: Stingray had VR support, and the engine kept the API.

## Units (read the way the engine's Lua bindings read them)

| What | Where | Binding it comes from |
| --- | --- | --- |
| Unit* of a unit reference | `exe+0x9D8C0(ref)`; the object keeps the reference at +8 | every Unit binding (Lua passes `ref << 2`) |
| Resource name (u64) | `[[unit + 0x30]]` | `Unit.resource_name`, exe+0x40BD50 |
| Scene graph | the vtable's +0xE8 (`unit + 0x60` for a plain unit) | `Unit.has_node`, exe+0x405530 |
| Node count | graph + 0x10 | has_node |
| Local poses (16 floats each) | graph + 0x20 | terminal camera |
| World poses (4x4 row-major, 64 bytes each; position at floats 12-14) | graph + 0x28 | live |
| Node names (IdString32 each) | graph + 0x40 | has_node |
| IdString32 of a name | top 32 bits of murmur64A(name, seed 0) (exe+0x5D5840); `head` = 0x8C5570C9 | has_node; live |
| The world's units | u32 count at world + 0x2305B8, Unit* array at + 0x2305C0 | live |

## The Helldiver

- Resource 0x4D1C334D294DFA97 (HellForge's `EVP_AVATAR`). In a mission: 107 nodes, 1 mesh. Live.
- Node `head` is index 90 (Lua's 91), 1.58 m above the root; `neck` 89, 1.44 m.
- Other nodes worth knowing: `head_aim`, `aim_weapon`, `weapon_aim`, `boss_aim`, `attach_cam_1`, `attach_weapon`,
  `attach_hand_l/r`, `backpack`, `cape1`-`cape8`, `l/r_shoulderarmour`.
- Three more units with a `head` node sit on the Helldiver (armour pieces: resources 0xBC20D0B4EFFF128C 18 nodes 5
  meshes, 0x3C33CF10A26CBB3E 50 nodes 4 meshes, 0xE57627FE6214BDD8 28 nodes 6 meshes); one of them is the helmet.
- On the ship, the search for that resource near the camera found nothing: the ship's Helldiver is probably another
  resource. Not needed yet (the ship is third person by default).
- `HF_ON_WEAPON`'s avatar entity did not resolve to a unit through the entity manager's descriptors (game.dll
  +0x346BF98, map +0xF1AEB0, descriptors +0xF32F18); finding the unit by resource near the camera works instead.

## First person (G1 step 2, live 2026-10-31)

- With the camera at the head node, 0.12 m forward along the view and 0.06 m up, keeping the game camera's rotation:
  the view is at eye height, and the helmet does not show (screenshot in a solo mission).
- When the Helldiver died, the game's camera moved away from the head, and the mod went back to third person, as meant.
- Not checked yet: aim (whether shots land at the screen's centre), the weapon and arms in view while moving, other
  players' Helldivers, vehicles, emplacements, the hellpod.

## Cautions

- A harness Lua call that walked every unit with `Unit.scene_graph_parent` crashed the game (an access violation in
  exe+0x4087C4 under lua51.dll). Keep live Lua probes to the bindings used above.
