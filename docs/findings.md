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
- Not checked yet: other players' Helldivers, emotes, vehicles, emplacements.

## Aim (live 2026-10-31)

- game.dll aims from its own third-person camera and never reads the engine camera (a census of the Camera getters,
  exe+0x30DE10, +0x30DE90, +0x30E190, +0x30E1F0, +0x30D940, +0x30D980, hooked in a probe build: only the renderer's
  setup at exe+0x77E5A0 called them). Moving the engine camera does not move where shots go.
- The engine's synchronous raycast: the physics interface `*(game.dll+0x3326328)`, slot +0x68 = exe+0x77DEC0,
  `int (u32 query, const float from[3], const float dir[3], float length, int 0, float result[16], int 1)`; the return
  is the number of hits, the first hit's position at result[0..2].
- Query ids are 0x80000000 | the row of the engine's query table (exe+0x27B9440, 20-byte rows: mode, ?, ?, the query
  name's hash at +12, flags). Row 4 is damage_los_query (0x2315515D, mode 1: the closest hit); row 1 damage_query,
  row 3 damage_no_foliage, rows 10 and 11 climbable. Rows 1-3 (mode 2) report a hit at the ray's start when it starts
  inside the Helldiver; row 7 (the one the game's prebuilt ground rays use, `*(*(game.dll+0x346D508)+8)+0xA4`) passes
  through props.
- With the view turned to the damage line of sight's hit along the game's line, the game's reticle, its weapon laser
  and the impacts are at the screen's centre.
- Middle mouse while aiming is the game's own first-person aim: its camera goes to 0.16 m from the head.

## Moving about (live)

- Jogging bobs the head 13-24 cm up and down at about 2 Hz relative to the root (node 0), and 5-25 cm fore and aft.
- Prone puts the head 0.41 m above the root.
- Looking down with the eye offset along the full view sank it into the neck (clipped shoulder armour and chest in
  view); looking up put the helmet's brim over the view. The offset is level, and 0.15 m further out looking straight up.
- The screen after a mission (the squad's cards) has its camera 1.6 m from the head: a distance alone is not "on foot".

## HellForge sessions

- The game runs with `LOCALAPPDATA`, `APPDATA` and `TEMP` pointed into the session's folder
  (`logs/sessions/<id>/game/environment`), new each session, so a mod's file written there is gone the next time. The
  mod's own folder is kept, read-only while the game runs.

## The test harness

- Screenshots from the OBS scene crop a game window larger than its canvas: the game drew 1920x1080 on a 1280x720
  canvas, so the scene showed the top-left part and the screen's centre looked off. Capture the source itself.
- The harness's test-input mod drives the engine's keyboard and mouse (`harness.py input`): W walks, the mouse buttons
  aim and fire, `look` turns, Z goes prone, G throws. Left Ctrl did not crouch, and X did not emote, standing on the
  hellpod.

## Edge cases (live 2026-10-31)

- The diver faces where it runs, not where the camera looks: strafing, running diagonally or back towards the camera,
  and standing still after a quick turn all leave the body turned from the view. Looking down then showed the shoulder
  armour, the back or the backpack until the eye went further out along the view (fp_camera.c).
- The cape still shows looking down while running back or sideways, and across the top of the view during a dive: it
  hangs where the eye goes. It needs hiding, not a camera move.
- A dead Helldiver's body stays in the world as the same unit; the reinforcement is another unit.
- Mesh visibility: exe+0x200CF0 (the Unit C API's +432) only looks a mesh up (Unit vtable +0x118: a name or index to
  the mesh's index). The Lua binding Unit.set_mesh_visibility (exe+0x4128E0, a thunk to the body at exe+0x4124D0)
  takes an optional fourth argument, a visibility context (IdString through exe+0x5D5840), and sets visibility deeper
  in. Not found yet: the setter itself. Setting a node's local scale once from Lua (cape1 node 35, backpack node 30 of
  the Helldiver) changed nothing on screen: the animation writes those poses every frame, or the cape is skinned to an
  armour unit's own bones.

## Cautions

- A harness Lua call that read `Unit.num_scene_graph_items` and `Unit.mesh` of every unit within 1.5 m of the Helldiver
  crashed the game (an access violation in exe+0x9E27E, under the unit lookup exe+0x9D8C0 and the Unit bindings).
- A harness Lua call that walked every unit with `Unit.scene_graph_parent` crashed the game (an access violation in
  exe+0x4087C4 under lua51.dll). Keep live Lua probes to the bindings used above.
