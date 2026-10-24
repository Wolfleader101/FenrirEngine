# PhysicsDemo

The physics demo app (`GLOSSARY.md`: demo app): the legacy DemoApp's physics scenes ported to the demo scaffold
(`examples/DemoScaffold/README.md`). Built with the examples when the renderer and physics (`FENRIR_ENABLE_PHYSICS`) are
on; the C# scenes are in it only with scripting (`FENRIR_ENABLE_SCRIPTING`).

The port is faithful: each scene has the legacy scene's bodies, keys, autopilot and log lines. What changed:

- **Scene switching:** PageDown and PageUp step through the scene list (the scaffold's), not the legacy F-key families.
  Each scene is spawned afresh each time it is entered, and destroyed when it is left.
- **Looks:** the legacy `DemoMesh` (and the flat-colour models of the C++ scenes) is a `MeshRenderer` drawing the shared
  cube or sphere in `builtin:lit`, one material per colour shared by every body of that colour, under a sun and the
  skybox (`src/SceneLooks.cpp`, `scripts/Looks.cs`). Playground (C#)'s trigger zone tints a body by swapping its
  `MeshRenderer` to a second, paler shared material, and back.
- **Debug lines** are physics's own output through FenrirDebugDraw, in its `Physics` group.
- **Cameras:** a follow camera is a `Camera` entity whose `Transform` the scene's code writes, not the legacy
  `DemoCamera`. A scene with one starts with the scaffold's free-fly camera off; F3 switches between them.
- **Input:** keys come from FenrirInput (`Keyboard`, in C++ and C#), the left-click raycast from the scaffold's
  `DemoSceneClickEvent`, and a gamepad from `Gamepads` (the vehicle scenes, ticket 29).
- **Readouts:** the legacy scenes write live readings into entity names, which the legacy editor's hierarchy showed. Each
  scene shows those names as debug screen text from the window's top-left.

## Scenes

| Scene | What it shows | Keys |
| --- | --- | --- |
| Playground (C++) | The legacy Default scene's physics: a stack of eight boxes on a ground, a kinematic paddle game code sweeps through it in `Tick`, and a static backpack whose collider is a triangle mesh cooked from its model, with three boxes dropped onto it. | None of its own. |
| Playground (C#) | The legacy Physics (C#) scene, built and run entirely by `scripts/Playground.cs`: a pyramid of ten boxes, five bouncy spheres (their hard `ContactBegin`s logged), a paddle and a green trigger zone (`TriggerEnter`/`TriggerExit` logged, and a body in it pale). | J kicks every box and sphere into the air. A left click casts a ray (`PhysicsWorld.Raycast`), logs what it hit and pokes it. |
| Joints (C++) | A door hinged to a static post (limits, friction, a motor, and a trigger child overlapping the post), and a chain of eight links from a world point (`src/Joints.cpp`, whose header describes it). | 1 pushes the door, 2 cycles its motor, L its limits. 9 toggles `collideConnected` on the chain. 0 logs every joint's state. B toggles a 15 N break force on chain joint 1, R repairs broken joints. 4 teleports the door, 5 the door and its frame. 6 scales link 5, 7 adds or removes a lump on link 6, 8 moves it. 3 destroys chain link 3. |
| Joints (C#) | A breakable bridge, a lift on a slider, a spring rope and a swing-twist limb, built and run by `scripts/Joints.cs` over two frames (frames from `Joint.FrameFromWorld`). | B drops a weight on the bridge (its middle hinge breaks over 250 N), R repairs, K swaps the middle joint between hinge and fixed, 1 cycles the lift's motor, 2 raises and lowers the limb, 0 logs every joint's state. |
| Character (C++) | A character body walked through a small level with a follow camera (`src/Character.cpp`, whose header describes it): slopes and stairs, a wall, a low ceiling, a crouch tunnel, a crouch-jump ledge, a lift and a shuttle, push boxes, a seesaw, a paddle and a second character. | WASD or the arrows walk (relative to the camera), Shift runs, Space jumps, Q and E turn the camera, C frees it and snaps it back, X crouches, G grows. B throws a ball, H drops a box on the head. T teleports to the start, 1 toggles `stopOnSlope`, 2 interpolation, 3 pauses the platforms, 4 toggles the character, 5 stairs and snapping, 6 the second character, M swaps to a mesh collider. L logs the state, P walks the route by itself. |
| Character (C#) | A character body built and driven by `scripts/Character.cs`, with its own follow camera: a slide ramp, ice, a crouch tunnel, a trigger pad, a lift, a ghost pane and a test wall. | As Character (C++) for walking, the camera, X, T, 1, L and P; F dashes 8 m ahead (`MoveCast`), K pushes the character out of the test wall (`Penetrations`, `Depenetrate`). |

The vehicle scenes come with ticket 29.

| Key | What it does |
| --- | --- |
| F2 | Cycles the physics debug lines: off, colliders, contacts, velocities, bounds, joints, characters, vehicles, all. The choice is the app's: a scene entered later gets it once its physics world exists. |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs and tests

The scaffold's switches work as in every demo app: `FENRIR_DEMO_SCENE="Joints (C#)" FENRIR_DEMO_KEYS=1` runs a scene
through its keys (the Character scenes' keys start and stop the autopilot), and `FENRIR_DEMO_SMOKE` runs every scene.

Besides the scaffold's `PhysicsDemo-Smoke-<backend>`, CTest runs each scene alone as `PhysicsDemo-<Scene>-<backend>`
(`PhysicsDemo-JointsCSharp-null`, say), for a second per key, and fails unless it logs no error and logs the legacy
scene's lines for its keys and autopilot (`cmake/CheckSceneLog.cmake`).

## Hand checks

Moved from the legacy DemoApp's physics tickets 29 (joints) and 39 (characters), with the switching lines rewritten for
the scene list. Check each scene as its legacy scene behaved.

- **Switching and smoke runs:**
  - PageDown steps Playground (C++) → Playground (C#) → Joints (C++) → Joints (C#) → Character (C++) → Character (C#), and
    F1 shows each scene's keys.
  - The F2 mode carries across a switch: the lines drawn match the logged mode. "characters" draws the character's
    shape, its ground normal and its `movedVelocity`.
  - A key pressed in one scene does nothing in another.
  - `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each scene through its keys (and the Character scenes through their
    autopilots) with no errors, in Debug and Release.
  - Without scripting, the scene list has only the C++ scenes.
- **Playground (C++):** the paddle sweeps the stack, and the boxes dropped on the backpack come to rest on its shape, not
  on its bounding box.
- **Playground (C#):** a box or sphere on the green pad turns pale while it is there and logs `TriggerEnter` and
  `TriggerExit`; J throws the bodies; a left click logs `Raycast: hit ...` and pokes what it hit.
- **Joints (C++):**
  - **Door:** it swings, stops at its limits, and its readout shows its angle. With limits on, a velocity motor holds the
    door at its limit, not past it.
  - **Waking:** a motor edit on a sleeping door wakes it.
  - **Trigger child:** the door's trigger child reports its post.
  - **Chain:** it hangs with its frames together and shows its load in its readouts. Links two apart still collide.
  - **`collideConnected`:** turning it on (9) visibly pushes overlapping links apart.
  - **Destroying a link (3):** one warning per joint naming it. The links below fall, and the inactive joints draw grey.
  - **Teleports:** the door teleported alone (4) is pulled back without a jolt. The door and frame together (5) line up
    at once.
  - **Shape changes (6, 7, 8):** the anchors stay put.
  - **Breaking:** B then a swing breaks joint 1 with one `JointBroken`, and it draws dimmed. R repairs it, and it breaks
    again if still loaded.
- **Joints (C#):**
  - **Build:** C# builds the scene with no warning and no inactive joint.
  - **Bridge:** B drops a weight, the bridge breaks with one logged `JointBroken`, and R repairs it.
  - **Kind swap:** K swaps the middle joint to fixed and back, and the bridge visibly stiffens.
  - **Lift:** 1 drives it between its limits and to a position.
  - **Rope:** it bobs from its world point.
  - **Limb:** it stays inside its cone, and 2 raises and lowers it.
  - **Readouts:** each joint's reading shows live.
- **With the `Joints` lines on:** every joint's frames coincide at rest.
- **Camera (both Character scenes):** it follows, and Q/E orbit it. C frees it (it stays where it is, and the arrows
  still walk), and C again snaps it back. F3 switches to the free-fly camera and back.
- **Character (C++):**
  - **Slopes:** the ramp is walked. The 55° slope is not climbed and reads "on steep ground". With 1 off, the character
    slides down the ramp at full speed, not a creep.
  - **Stairs:** climbed with no airborne ticks, and come down with at most a brief steep flicker. With 5 off, they block
    the character.
  - **Platforms:** the lift and the shuttle are ridden ("on ground: Lift"). A jump off the lift keeps `carriedVelocity`
    until landing. The hover at the lift's turn is visible, as stated.
  - **Ceiling:** a jump into the low ceiling stops short, with `onCeiling` shown.
  - **Pushing:** the 1 and 20 kg boxes move when walked into, and the 100 kg box doesn't. The seesaw tips under the
    character.
  - **Being pushed:** the paddle shoves the character at its own speed. B's ball bounces off without moving it, and its
    `ContactBegin` is logged.
  - **Second character:** it blocks the player. With 6, the player stands on it ("on ground: Character 2") and rides it.
  - **Box on the head:** H's box stays behind when the character walks off.
  - **Crouch tunnel:** X crouches into it. X again inside leaves `shapeBlocked` until the exit, where the character
    stands by itself. The box over the roof gap blocks standing, and the trigger volume does not.
  - **Crouch-jump:** crouching mid-jump clears the ledge.
  - **Scale:** after G, the character walks with its larger shape, and the characters lines show it.
  - **Mesh:** M gives one warning, and the character goes inactive and draws nothing. M again restores it.
  - **Enabled:** with 4 off, boxes pass through the character. With 4 on again, it resumes where it stands.
  - **Teleport:** T snaps the character to the start.
  - **Readouts:** the character's, the push boxes', the seesaw's and the second character's readings show live.
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
