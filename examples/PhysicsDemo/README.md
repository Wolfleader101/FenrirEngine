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
  `DemoSceneClickEvent`, and the vehicle scenes' gamepad from `Gamepads` (the first connected pad, the oldest
  connection), not the legacy `DemoGamepad`. Its buttons are named by the standard layout: A is South, B East, X West
  and Y North.
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
| Vehicles (C++) | A car towing a trailer round a track, with a chase camera (`src/Vehicles.cpp`, whose header describes it): ten bumps and a ramp to the north, a pedestrian on the start straight, ice and grass lanes, a wall of crates and a ferry across a gap to the east, and a 15° parking slope to the west. Its driver gets out and walks, and a flipped car is righted, both as game code following the `Vehicle` docs' recipes. | W or Up drives, S or Down brakes and then reverses, A/D or the arrows steer, Space pulls the handbrake, C frees the camera and snaps it back. M switches the gearbox (Manual: E/Q shift, Shift holds the clutch). 1 softens the springs, 2 fits big wheels, 7 limits the tilt, 4 switches the car off and on, 5 its interpolation, 9 makes its chassis kinematic, 8 destroys and remakes its rear-left wheel. T respawns the car. F gets out and back in (on foot WASD walks, Shift runs, Space jumps, Q/E turn the camera, T teleports the driver). K kicks the car over, R rights it. L logs the state, P drives the track by itself. A gamepad drives too: the triggers drive and brake, the left stick steers (or walks), the right stick looks round the car, A is Space, B Shift, X M, Y F, Back R, Start 9, the bumpers Q/E and the D-pad 1, 2, 7 and 4. |
| Motorcycle (C++) | Jolt's sample motorcycle on open tarmac, standing at rest and leaning into turns, with the same chase camera (`src/Motorcycle.cpp`). | The driving keys and C, as Vehicles (C++). 3 changes the max lean between 45° and 20° (a rebuild keeping RPM and gear), 6 switches the lean controller off (it falls over) and on, 5 its interpolation, T stands it up at the start. L logs the state, P rides a loop by itself. |
| Tank (C++) | A 4000 kg tank on two tracks of nine wheels each, pivoting on the spot when stopped, on open tarmac with a row of crates, with the same chase camera (`src/Tank.cpp`). | The driving keys and C, as Vehicles (C++); Space brakes. 3 changes the tracks' grip between 4 and 1, 5 its interpolation, T puts it back at the start. L logs the state, P drives a loop by itself, pivoting at each end. |
| Vehicles (C#) | A car towing a trailer, built and driven entirely by `scripts/Vehicles.cs` (whose header lists its keys), on a track of its own: five bumps, a ramp and a grass patch, with its own chase camera. | As Vehicles (C++) for driving, C, M and E/Q, Shift, T, K, R, L, P and the gamepad. V turns the vehicles' debug lines on and off, U turns the trailer into a wheeled car that pushes, and back. |

| Key | What it does |
| --- | --- |
| F2 | Cycles the physics debug lines: off, colliders, contacts, velocities, bounds, joints, characters, vehicles, all. The choice is the app's: a scene entered later gets it once its physics world exists. |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs and tests

The scaffold's switches work as in every demo app: `FENRIR_DEMO_SCENE="Joints (C#)" FENRIR_DEMO_KEYS=1` runs a scene
through its keys (the Character scenes' keys start and stop the autopilot), and `FENRIR_DEMO_SMOKE` runs every scene.

Besides the scaffold's `PhysicsDemo-Smoke-<backend>`, CTest runs each scene alone as `PhysicsDemo-<Scene>-<backend>`
(`PhysicsDemo-JointsCSharp-null`, say; `PhysicsDemo-VehiclesCpp-<backend>` is the slowest, a lap of its track), for a second per key, and fails unless it logs no error and logs the legacy
scene's lines for its keys and autopilot (`cmake/CheckSceneLog.cmake`).

## Hand checks

Moved from the legacy DemoApp's physics tickets 29 (joints), 39 (characters) and 48 (vehicles), with the switching lines
rewritten for the scene list. Ticket 48's vehicle lines follow the scenes as they were built: the motorcycle and the tank
have scenes of their own, so there is no Tab, and the driver gets in only the car. Check each scene as its legacy scene behaved.

- **Switching and smoke runs:**
  - PageDown steps Playground (C++) → Playground (C#) → Joints (C++) → Joints (C#) → Character (C++) → Character (C#) →
    Vehicles (C++) → Motorcycle (C++) → Tank (C++) → Vehicles (C#), and F1 shows each scene's keys.
  - The F2 mode carries across a switch: the lines drawn match the logged mode. "characters" draws the character's
    shape, its ground normal and its `movedVelocity`. "vehicles" draws the suspension, the wheel circles and the contacts
    coloured by slip.
  - A key pressed in one scene does nothing in another.
  - `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each scene through its keys (and the Character and vehicle scenes
    through their autopilots) with no errors, in Debug and Release. In Vehicles (C++) the only warnings are physics's
    one for each of 8 and 9.
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
- **Camera (the vehicle scenes):** it chases the vehicle (in Vehicles (C++), the driver on foot too). C frees it (it stays
  where it is, and the arrows still drive), and C again snaps it back. F3 switches to the free-fly camera and back.
- **Wheels (the vehicle scenes):** the meshes sit on their axles, spin (the stripe), steer and compress over the bumps.
  With 5 off, they still move with the chassis.
- **Gamepad (Vehicles (C++) and Vehicles (C#), with a pad connected):** analog throttle, brake and steer drive. The
  bumpers shift in Manual, the right stick orbits the camera and springs back, and Y and Back get out and in and right
  the car (Vehicles (C++)). Unplugging mid-drive falls back to the keyboard with no stuck input.
- **Vehicles (C++):**
  - **Car:** it reaches about 100 km/h on the straight and parks on the 15° slope with Space without creeping. On ice its
    wheels spin ("sliding", contacts coloured past `slideSlip`), and grass is looser than tarmac. M with E/Q shifts by
    hand, and Shift holds the clutch.
  - **Trailer:** towed over the ramp, it brakes with the car.
  - **Crates:** the car shoves them aside, with the chassis dip as stated.
  - **Pedestrian:** it stops the car at the chassis, and the wheels never climb it.
  - **Ferry:** a parked car rides it.
  - **Getting in and out:** F gets out beside the door, not inside anything, and the driver walks. F within 3 m of the
    car gets back in and drives it.
  - **Flip and tilt:** K flips the car, "flipped" shows, and R rights it. With 7 on, K can't roll it past 60°.
  - **Live edits:** 1 and 2 change the ride height live. With 4 off, the car is a plain rolling box.
  - **Wheel destroyed:** 8 gives one warning, and the car stays rear-wheel drive on its one rear wheel. 8 again restores
    it. T respawns the car.
  - **Kinematic:** 9 gives one warning, and on return the car is at idle in neutral.
  - **Readouts:** the car's, the trailer's and each wheel's readings show live.
- **Motorcycle (C++):** it stands at rest and leans into turns. With 6 off, it falls over. 3 changes the max lean without
  losing RPM or gear. Its readout shows its lean.
- **Tank (C++):** it pivots on the spot on A/D when stopped, and its readout shows both track speeds.
- **Vehicles (C#):**
  - **Build:** C# builds the scene with no warning. The car drives, and its trailer brakes with it.
  - **Gears and readouts:** M and E/Q shift by hand. The readouts match the C++ format, and the grass shows in a wheel's
    readout.
  - **Debug lines:** V toggles them.
  - **Flip:** K and R work as in C++.
  - **Trailer swap:** U turns the trailer into a wheeled car that pushes, and U again turns it back into a trailer.
