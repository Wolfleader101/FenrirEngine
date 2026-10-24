# What do the character demo scenes show?

Type: grilling
Status: resolved
Blocked by: 34, 35, 37

## Question

The character spec ships two DemoApp scenes on F5 through `DemoScenes` (examples/DemoApp/src/DemoScenes.hpp): "Character (C++)" and "Character (C#)", the C# one built only from C# as `scripts/Physics/Joints.cs` is. Builds on [What does game code read back from a character, and which events?](34-character-state-and-events.md), [How does a character interact with other bodies?](35-characters-and-other-bodies.md) and [Spike: a character walking a small level through the proposed model](37-spike-character-walking-a-level.md).

- **Camera:** the DemoApp's camera is a free-fly camera (F1, WASD). A character scene needs a follow camera and keys that drive the character instead; both are demo code, not engine code. How the keys are shared with the free-fly camera.
- **Contents:** what each scene shows (slopes, stairs, a platform, pushable boxes, a crouch tunnel, triggers), and how the C# scene proves the C# surface rather than repeating the C++ one.
- **Readouts:** ground state shown live (in an entity's `Name`, as the joints scenes do; there is no text overlay).
- **Debug lines:** whether characters get a debug-line category (shape, ground normal, velocity).
- **The spike's scene:** whether it becomes the C++ scene.
- **Hand checks:** what the engine developer verifies by hand, and the `FENRIR_DEMO_KEYS` smoke runs.

From "What does game code read back from a character, and which events?": the readouts can show `state.ground`, `groundBody`, `movedVelocity`, `carriedVelocity`, `onWall` and `onCeiling`; landing is derived by comparing with last tick, so a scene showing a landing counter proves the derivation. Characters send the existing contact events (with `ContactEvents`) and trigger events, so a trigger the character walks through and a `ContactEvents` character are the checks for them. A debug-line category could draw `groundNormal` and `movedVelocity`.

From "Spike: a character walking a small level through the proposed model" (37): the spike's "Characters (spike)" scene (branch `prototype/fenrir-physics-character-spike`, `CharacterScene.hpp`) is a candidate for the C++ scene:

- **Level:** a 30° ramp, a 55° slope, ten 0.3 m stairs to a 3 m ledge, a lift beside the ledge (1.5 m/s), a sideways shuttle (2 m/s), five 1 kg boxes, and a low ceiling to jump into.
- **Camera:** a follow camera placed after the camera controller in `Update`, so it wins. WASD walks while it follows, and C frees the camera, giving WASD back to it, while the arrows still walk. Q/E orbit it.
- **Readouts:** the character's `Name` shows its ground state, `velocity`, carried velocity and ground changes per second. Each ground change is logged with its tick, which is how the stair-descent flicker and the lift-turn hop were found. Logging the ground body ("on Lift") was the most useful readout.
- **Autopilot:** an autopilot route (P) gives `FENRIR_DEMO_KEYS` something to walk, since smoke keys can only tap.
- **Hand checks:** on a 60 Hz display, interpolation cannot be seen by eye. Leave it to the headless check, or drop the frame cap for the demo.
- **Debug lines:** the scene had none. The character's look is two cubes over a capsule collider, and the colliders category does not draw characters. A character category drawing the capsule, the ground normal and `velocity` would show what the look hides.
- **Game code:** the scene's Tick sets horizontal speed from the keys, which stops `stopOnSlope = false` from sliding. The demo should add input to the written-back velocity instead.

From "How does a character interact with other bodies?": hand checks for pushing and being pushed, beyond the spike's 1 kg boxes:
- boxes of several masses (1, 20, 100 kg), showing that `pushForce` (100 N) moves the light ones and not the heaviest, and a seesaw or raft the character's `mass` tips;
- a kinematic door or paddle that shoves the character at its own speed, and a ball thrown at a standing character that bounces off without moving it, with a `ContactEvents` readout proving the bounce is reported (the inner body's step contacts);
- two characters (one autopilot) blocking each other, one standing on the other and riding it;
- a box resting on the character's head, which is not carried (a stated limit).
A character debug-line category could draw the capsule its inner body presents to bodies and queries.

From "What are the motion-test and sweep queries, and how does a character change shape?": the spike remade the character on a shape edit, and the spec reshapes it with a room check, so the scenes need a **crouch tunnel**: crouch to enter, release inside, and the character stays crouched (`state.shapeBlocked` in the readout) until it walks out and stands by itself. Further hand checks:
- crouching on the ground keeps the feet down, and crouching mid-jump tucks them up to clear a ledge it otherwise could not;
- a 1 kg box or another character above the head blocks standing; a trigger does not;
- a scaled-up character (world scale) walks with its larger shape;
- a mesh-shaped character goes inactive with one warning (the spike already shows this).

Motion tests have no character of their own to prove them, so one scene could show a custom mover using them: a kinematic `RigidBody` moved by game code with `MoveCast` and `Depenetrate`, or a dash that stops at `MoveCast`'s safe distance and teleports the character there. A character debug-line category should draw the shape in use, which differs from the `Collider` while a stand-up is blocked.

From "What does C# get for characters?" (38): what the C#-built scene must exercise, beyond what tests prove. This ticket decides the contents and keys. C++ places only a marker, and C# builds everything else:
- a character with a capsule `Collider`;
- a controller that adds gravity from `PhysicsWorld.Gravity`, jumps, and adds input to the written-back `Velocity`, so a `StopOnSlope = false` slide works;
- a readout every frame from `State` (ground state, `GroundBody`'s name, `MovedVelocity`, `CarriedVelocity`, `ShapeBlocked`), with a landing counter from comparing with last tick;
- a crouch key that writes `Collider.Shape` once, used through a crouch tunnel;
- one motion test of the scene's own, such as a dash that stops at `MoveCast`'s safe distance and teleports there, filtered like the character with `Layers`;
- ice, from reading `GroundCollider`'s `PhysicsMaterial`;
- a `ContactEvents` or `TriggerEnter` readout naming the character.

The C# scene does not repeat the C++ scene's push-mass and kinematic-door checks: C# reaches those unchanged.

## Answer

**There are two character scenes on F5: "Character (C++)" (the spike's scene, rewritten and extended) and "Character (C#)" (a level of its own, built only from C#).** Each scene has a follow camera. The C++ scene's follow camera is written in C++ and the C# scene's in C#, and both drive the DemoApp camera through one shared demo component. A new `DebugDraw::Characters` category draws what the character's look hides.

**Facts found in the code** (on `dotnet-scripting` and the spike branch):
- **C# cannot move the camera.** `Fenrir::Camera` is a C++ object in `main.cpp`, and C# sees it only through `SceneClickEvent`'s ray. `DemoMesh` is the precedent for C# driving something C++ owns: C# writes a demo component, and a C++ system applies it after the C# Update systems (`main.cpp`, `DemoMeshes`).
- **The spike's follow camera wins by order.** It runs after `CameraController::Update`, which still moves the camera with WASD and the right mouse button, and then overwrites it. F1 still turns the controller off.
- **The spike's keys:** WASD or the arrows, Shift, Space, Q/E, T, 1–5, L, P (autopilot) and C (free camera).
- **F2's cycle:** off, colliders, contacts, velocities, bounds, joints, all. `DebugDraw` has no characters category, and `Colliders` does not draw characters.
- **Smoke runs can only tap** (`DemoScenes::PressSmokeKeys`). Anything that must be held, such as walking, needs an autopilot or a toggle.

### Decisions

1. **The scenes, and the spike's fate.** F5 steps Default → "Character (C++)" → "Character (C#)" → Default. Without scripting, F5 toggles Default ⇄ Character (C++).
   - "Character (C++)" is the spike's `CharacterScene.hpp` rewritten against the final API:
     - it reads `body.state` instead of `GetCharacterState`;
     - it reshapes the character instead of remaking it;
     - it drops the spike's guesses.
   - It keeps the spike's level: the 30° ramp, the 55° slope, the ten 0.3 m stairs to the 3 m ledge, the lift (1.5 m/s), the shuttle (2 m/s) and the low ceiling.
   - "Character (C#)" is built only from C#. C++ places a `CSharpScene` marker with id 3 (`CSharpScenes::characterId`), as in ticket 29.
2. **Two camera controllers, one shared `DemoCamera` component.**
   - **The component:** `DemoCamera { Vec3 Position; float Yaw; float Pitch; int Active; }` (`Demo.DemoCamera`), registered in the demo's scripting extension and sitting on an entity in the scene.
   - **"Character (C++)":** a C++ `CharacterCamera` controller writes it.
   - **"Character (C#)":** a C# `CharacterCamera` system does the same job in C#. It reads its own keys, orbits on Q/E, follows the character's world position, and keeps the yaw that its walk code reads.
   - **The applier:** one C++ system, `DemoCameras`, copies the active scene's `DemoCamera` into `Fenrir::Camera` while `Active` is set. It runs after `CameraController` and after the C# Update systems, in the slot the spike's follow camera had, so it wins. This is DemoMesh's pattern: C# moves the camera without knowing `Fenrir::Camera`.
   - **Free camera:** C writes `Active = 0`, which leaves the camera to the free-fly `CameraController` (WASD), while the arrows still walk. C again sets `Active = 1`, and the camera snaps back to following.
   - Walking is relative to the follow camera's yaw.
   - Rejected: one C++ follow camera following a marker that C# places. The user wanted the C# scene's camera to be C# code, as its keys are.
3. **What "Character (C++)" adds to the spike's level** (from tickets 35 and 36):
   - **Push masses:** boxes of 1, 20 and 100 kg. `pushForce` (100 N) moves the light ones and not the 100 kg box.
   - **Seesaw:** a plank on a hinge that the character's `mass` tips.
   - **Paddle:** a kinematic paddle sweeping across a lane, which shoves the character at its own speed.
   - **Ball:** thrown at the standing character (B). It bounces off without moving the character, and the character has `ContactEvents`, so the `ContactBegin` is logged. These are the inner body's step contacts.
   - **Second character:** it walks a fixed loop of its own, driven by C++ code, not the autopilot. It blocks the player. Stopped (6), it can be stood on, and once started again it carries the player.
   - **Box on the head:** H drops a 1 kg box on the character's head. It is not carried, a stated limit, so it stays behind when the character walks away.
   - **Crouch tunnel:**
     - The character crouches to enter it.
     - Releasing crouch inside leaves the character crouched, with `shapeBlocked` set, until it walks out and stands by itself.
     - A 1 kg box resting over a gap in the tunnel's roof blocks standing there.
     - A trigger volume elsewhere in the tunnel does not block standing.
   - **Crouch-jump ledge:** a ledge too high to clear standing, which crouching mid-jump clears (the feet tuck up in the air).
   - **Edge-case keys:**
     - G scales the character by 1.5× (world scale) and back.
     - M swaps its collider to a mesh, giving one warning and leaving the character inactive, and back.
4. **What "Character (C#)" shows**: ticket 38's list, on a small level of its own built through the C# surface:
   - **Character and controller:** a capsule character with a `Collider`. Its controller adds gravity from `PhysicsWorld.Gravity`, jumps, and steers the written-back `Velocity` (decision 7).
   - **Slide ramp:** a ramp with `StopOnSlope = false` at the start, so the character slides down it.
   - **Crouch tunnel:** crouching writes `Collider.Shape` once, and the scene reads `ShapeBlocked`.
   - **Ice:** a patch whose `PhysicsMaterial` friction, read from `GroundCollider`, lowers the acceleration cap, so the character is slow to start and stop.
   - **Dash (F):** `MoveCast` sweeps the character's shape ahead, filtered with `Layers` like the character, and the character teleports to the safe distance.
   - **Push out of the wall (K):** teleports the character half into a test wall, logs `Penetrations`' count and deepest depth, then applies `Depenetrate`'s offset. With the dash, this exercises all three motion tests. No other scene shows a custom mover.
   - **Trigger pad:** logs `TriggerEnter`, naming the character.
   - **Landing counter:** landings counted by comparing with last tick.
   - **Lift:** one kinematic lift, so `CarriedVelocity` shows.

   It does not repeat the C++ scene's push masses, paddle, ball, second character, scale or mesh keys: C# reaches those unchanged.
5. **Readouts.** In both scenes, in the same format, the character's `Name` shows every frame:
   - the ground state and the ground body's name ("on ground: Lift");
   - `velocity`, `movedVelocity` and `carriedVelocity`;
   - `onWall`, `onCeiling` and `shapeBlocked`, shown only while set;
   - the landing count;
   - ground changes per second.

   Each ground change is still logged with its tick, and L logs the full `state`. Other readouts go on the entity they concern:
   - the push boxes show their mass and speed ("Box 20 kg: 0.4 m/s");
   - the seesaw shows its angle;
   - the second character shows its own ground state, which is "on ground: Character" while the player rides it.
6. **`DebugDraw::Characters`**, in FenrirPhysics and its C# mirror, is included in `All`. F2's cycle gains "characters" before "all". It draws, for each active character:
   - **the shape in use**, as a wireframe at its step pose, in a new `Character` colour. This is the crouched shape while a stand-up is blocked, which differs from the `Collider`, and it is the shape the inner body presents to bodies and queries;
   - **the ground normal** from the ground point, coloured by ground state;
   - **a `movedVelocity` line** as far as the character travels in a tenth of a second, like `Velocities`.

   Inactive characters draw nothing, and `Colliders` still skips characters.
7. **The demo's game code steers velocity.** Both scenes move the written-back horizontal `velocity` toward `input × speed` by at most `acceleration × dt` each tick, with a lower acceleration on ice in C#. A `stopOnSlope = false` slide therefore keeps its speed (ticket 37's lesson 4), which the spike's "set horizontal speed from the keys" threw away. With no input, a slide continues.
8. **Interpolation:** both scenes keep a key that toggles `interpolate`. Seeing it is not a hand check, because a 60 Hz display can't show it. The headless check covers it, and the demo keeps vsync.
9. **"Character (C++)" keys.** Crouch is a toggle, not a hold, because smoke runs can only tap.

   | Key | Action |
   |---|---|
   | WASD / arrows | Walk, relative to the camera (arrows only while the camera is free) |
   | Shift | Run |
   | Space | Jump |
   | Q / E | Orbit the camera |
   | C | Free the camera, and re-attach it |
   | X | Toggle crouch (toggling off inside the tunnel leaves `shapeBlocked`) |
   | T | Teleport to the start |
   | 1 | Toggle `stopOnSlope` |
   | 2 | Toggle `interpolate` |
   | 3 | Pause the platforms, lift, shuttle and paddle |
   | 4 | Toggle `enabled` |
   | 5 | Toggle stairs and floor snap |
   | 6 | Stop or start the second character |
   | B | Throw a ball at the character |
   | H | Drop a 1 kg box on the character's head |
   | G | Scale the character by 1.5×, and back |
   | M | Swap the collider to a mesh (one warning, inactive), and back |
   | L | Log the full `state` |
   | P | Toggle the autopilot |
10. **"Character (C#)" keys.** The same letters do the same jobs as in C++ wherever both scenes have the job: WASD and the arrows, Shift, Space, Q/E, C, X, T, L, P, and 1, which toggles `StopOnSlope` and starts off. The scene's own keys:

    | Key | Action |
    |---|---|
    | F | Dash: `MoveCast` ahead and teleport to the safe distance, logging the hit's name and the distance |
    | K | Push out of the wall: teleport half into the test wall, log `Penetrations`, apply `Depenetrate` |
11. **Autopilots and smoke runs.** Both scenes have a P autopilot, because smoke runs can't hold walk keys.
    - **C++ route:** the spike's route, extended through the push-mass boxes, the seesaw, the paddle lane and the crouch tunnel. It toggles crouch at the tunnel's mouth and off inside, so `shapeBlocked` shows in the log.
    - **C# route:** the slide ramp, the ice, the tunnel, the trigger pad and the lift.
    - **Smoke keys:** each scene's run presses P, about 25 L presses while the autopilot walks, then P again. It taps each scene key twice to toggle it there and back, F and K once each, and finally T.
12. **Hand checks** (the spec's list for the engine developer):
    - **Switching and smoke runs:**
      - F5 steps Default → Character (C++) → Character (C#) → Default, logging each scene's key help on entry.
      - The F2 mode carries across a switch, and "characters" draws the character's shape, its ground normal and its `movedVelocity`.
      - `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each character scene through its autopilot and keys with no errors, in Debug and Release.
      - Without scripting, F5 toggles Default ⇄ Character (C++).
    - **Camera (both scenes):** it follows, and Q/E orbit it. C frees it (WASD flies the camera, the arrows still walk), and C again snaps it back.
    - **Character (C++):**
      - **Slopes:** the ramp is walked. The 55° slope is not climbed and reads "on steep ground". With 1 off, the character slides down the ramp at full speed, not a creep.
      - **Stairs:** climbed with no airborne ticks, and come down with at most a brief steep flicker. With 5 off, they block the character.
      - **Platforms:** the lift and the shuttle are ridden ("on ground: Lift"). A jump off the lift keeps `carriedVelocity` until landing. The hover at the lift's turn is visible, as stated.
      - **Ceiling:** a jump into the low ceiling stops short, with `onCeiling` shown.
      - **Pushing:** the 1 and 20 kg boxes move when walked into, and the 100 kg box doesn't. The seesaw tips under the character.
      - **Being pushed:** the paddle shoves the character at its own speed. B's ball bounces off without moving it, and its `ContactBegin` is logged.
      - **Second character:** it blocks the player. With 6, the player stands on it ("on ground: Character") and rides it.
      - **Box on the head:** H's box stays behind when the character walks off.
      - **Crouch tunnel:** X crouches into it. X again inside leaves `shapeBlocked` until the exit, where the character stands by itself. The box over the roof gap blocks standing, and the trigger volume does not.
      - **Crouch-jump:** crouching mid-jump clears the ledge.
      - **Scale:** after G, the character walks with its larger shape, and the characters lines show it.
      - **Mesh:** M gives one warning, and the character goes inactive and draws nothing. M again restores it.
      - **Enabled:** with 4 off, boxes pass through the character. With 4 on again, it resumes where it stands.
      - **Teleport:** T snaps the character to the start.
      - **Readouts:** the names show all of decision 5 live.
    - **Character (C#):**
      - **Build:** C# builds the scene with no warning.
      - **Walking:** the C# camera follows the character, which walks, jumps and slides down the ramp.
      - **Ice:** on the ice, the character is slow to start and stop.
      - **Crouch tunnel:** it works as in C++.
      - **Dash:** F stops short of what it hits and logs the hit.
      - **Push out:** K logs the penetrations and resolves out of the wall.
      - **Trigger pad:** it logs `TriggerEnter` naming the character.
      - **Lift:** riding it shows `CarriedVelocity`.
      - **Landings:** the landing counter goes up once per landing.

**No ADR:** the demo scenes, the `DemoCamera` pattern and a debug-line category are all easy to reverse. **Glossary:** no new terms. `DemoCamera`, demo markers and family keys are DemoApp vocabulary, as ticket 29 ruled, and the character terms are already defined. **Fog:** nothing new. A post-step follow to remove the lift hover stays a possible later refinement, as ticket 37 left it.
