# Plan

Two mods, built in groups: First Person and VR Mode, each its own switch on the Mods page. Each group ships on its own.
VR Mode works without First Person (seated third person) and follows First Person's view when both are on, through
the same API other mods use (`include/first_person_api.h`). Everything targets game
build 25480438 (the one HellForge runs). Addresses are pinned by their first bytes and checked before use; a pin that
does not match turns only its feature off, with a log line that says so.

The developer has no headset. Everything below is tested as far as one PC allows before it goes to testers; each group
says what that covers and what only a tester can check.

## G0: the scaffold (done)

- Both mods load on HellForge with mod SDK 13 or newer, on build 25480438 only, and share `src/common`.
- `fpv_camera.c` hooks the engine's script render step and finds the camera unit by the world's render pose (the method
  HellForge's terminal camera proved), so a filter can move the camera for one frame. Each mod hooks it; First Person's
  filter runs first, VR Mode's on top. G0 sets no filter: nothing changes.
- First Person's API (version 1): other mods ask for a view in situations (on foot, vehicle, mech, emplacement,
  hellpod, ship, ragdoll) while a request is held; the highest priority decides, else the player's settings. VR Mode
  looks it up at game load.
- Tests: the camera search and filter against a fake world; the pose math; the view decision and the API table.

## G1: first person (on foot): in progress

1. **The diver** (done, live). The Helldiver unit by its resource nearest the game's camera, its head node by name
   hash ([findings.md](findings.md)).
2. **The camera on the head** (done, live). The camera at the head node, 0.12 m forward and 0.06 m up, facing where
   the game's camera faces, so the direction the game aims along is unchanged. Third person again when the Helldiver
   dies (the game's camera moves away from the head).
3. **No head in the view.** The helmet, head and cape meshes hidden for the local player's camera only
   (Unit.set_mesh_visibility, proved by Difficulty 11 Plus), shown again when first person turns off. Shadows keep them if
   the engine allows it.
4. **The near plane and field of view.** Camera.set_near_range and set_vertical_fov (or their fields), so the weapon and
   arms do not clip.
5. **Aim.** The biggest unknown. The third-person reticle's ray starts at the shoulder camera; with the camera moved to
   the head, shots must land where the centre of the view points. Leads, in order:
   - the game's own first-person sights (it already has a first-person aim): keep that camera mode on, or
   - move the game's camera in its own update (before aim is computed) rather than at the render step, or
   - correct the aim ray where the weapon reads it.
6. **Situations.** Detected every frame and handed to the view decision (and so to the API): on foot, vehicle, mech,
   emplacement, hellpod, ship, ragdoll. By default first person on foot only, but first person has to work in every
   situation, since a mod (or the player's options) can ask for it in vehicles and the rest: the camera follows the
   diver's head wherever the diver sits. Emotes and menus stay third person; a key switches. Camera shake down (the weapon's `fire_loop_camera_shake` fields, or the shake
   function).
7. **Options.** `%LOCALAPPDATA%\CowboyBingus\FirstPerson\options.ini`: on/off, the toggle key, which situations are
   first person, "mods may change the view", head offset, fov, near plane, shake.
8. **API version 2 if a mod needs it:** a camera anchor (a node of a vehicle's unit and an offset) for seats where the
   head is the wrong place, and an event when the view changes.

Tested here: everything, in game (harness runs with the light preset and screenshots), plus unit tests.

## G2: VR Mode, seated (OpenXR, D3D12)

1. **OpenXR.** The Khronos loader (Apache 2.0), loaded only when VR is on; instance, system and a D3D12 session on the
   game's device and command queue (found from its swap chain, as HellForge's F8 editor finds them). Off with a reason
   when there is no runtime or headset.
2. **Head tracking.** The headset pose added to the camera each frame (the filter from G0, after First Person's), with
   a recentre key. With First Person's view active the head sits at the diver's eyes; in third person (First Person
   off, or a situation it leaves third person) the head moves the game's camera, and comfort options can show third
   person on a floating screen instead. The body still turns with the mouse or stick; the head looks around freely.
3. **Two eyes.** First alternate-eye rendering: each frame renders one eye (the camera shifted half the IPD, the eye's own
   asymmetric projection) and goes to that eye's swap chain image; the runtime's reprojection fills in. Then, if the
   frame budget allows, both eyes each frame (the world rendered twice; temporal AA/upscaler history per eye).
4. **The projection.** The eye's asymmetric field of view from xrLocateViews, written into the camera's projection
   (the field or function to be found), or a symmetric fov wide enough, cropped.
5. **The HUD.** First the whole frame on both eyes as rendered (HUD included); then, if the UI pass can be separated,
   the HUD as a head-locked quad layer.
6. **The flat window** keeps showing one eye.
7. **Comfort.** Options for snap turning, a vignette while the hellpod drops, and third person (a floating screen) in
   cutscenes and the ship.

Tested here without a headset: a fake OpenXR runtime (a small runtime DLL of our own, registered for the session with
`XR_RUNTIME_JSON`) that accepts the session, gives scripted head poses, and writes the submitted eye images to disk.
That covers the whole path in the real game: the device and queue, the copy into the swap chains, per-eye poses and
projections, alternating eyes, and the HUD. If Monado runs under Proton's wineopenxr here, its simulated headset is a
second check. Only testers can check: real runtimes (SteamVR, Oculus/Meta, Virtual Desktop, WiVRn), the frame timing
and reprojection, comfort and real performance.

## G3: motion controls (maybe)

Aiming with a controller apart from the head, and stratagem gestures. A project of its own; decided after G2.

## Multiplayer

Your own view only. Both manifests say `"gameplay": false`, so friends and lobbies, including HellForge 0.3, do not
require them. Other players see the normal diver.
