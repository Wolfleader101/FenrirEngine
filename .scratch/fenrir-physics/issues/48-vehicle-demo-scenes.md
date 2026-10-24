# What do the vehicle demo scenes show?

Type: grilling
Status: resolved
Blocked by: 44, 45, 46

## Question

The vehicles spec ships two DemoApp scenes on F6 through `DemoScenes` (examples/DemoApp/src/DemoScenes.hpp): "Vehicles (C++)" and "Vehicles (C#)", the C# one built only from C# as `scripts/Physics/Joints.cs` and the character C# scene are. Builds on [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md), [How does a vehicle meet other bodies, joints and characters?](45-vehicles-and-other-bodies.md) and [Spike: a car, a motorcycle and a tank driving a small track through the proposed model](46-spike-vehicles-on-a-track.md), and follows "What do the character demo scenes show?".

- **Camera and keys:** reusing the character scenes' `DemoCamera` follow camera, and how driving keys share the keyboard with the free-fly camera (F1, WASD).
- **Contents:** which kinds each scene shows (car, motorcycle, tank), the track (ramps, bumps, a low-grip surface, loose bodies, a trailer), and how the C# scene proves the C# surface rather than repeating the C++ one.
- **Readouts:** speed, RPM, gear and wheel contact in entity `Name`s, as the earlier scenes do (there is no text overlay).
- **Debug lines:** a `DebugDraw::Vehicles` category (wheel casts, suspension, contact points and normals, slip).
- **Wheels:** wheel meshes drawn from written-back wheel poses.
- **The spike's scene:** whether it becomes the C++ scene.
- **Smoke runs:** an autopilot for `FENRIR_DEMO_KEYS`, and `FENRIR_DEMO_SCENE`.
- **Hand checks:** what the engine developer verifies by hand.

From "How does a vehicle step each tick, and what does game code read back?": physics poses and interpolates the wheel entities, so wheel meshes are children of wheel entities with nothing else to do. Readouts (speed, RPM, gear, per-wheel slip and compression) come from `Vehicle::state` and `VehicleWheel::state`.

## Answer

**There are two vehicle scenes on F6: "Vehicles (C++)" (the spike's scene, rewritten and extended) and "Vehicles (C#)" (a track of its own, built only from C#).** Each has a chase camera writing the shared `DemoCamera`, readouts in `Name`s, a P autopilot for smoke runs, and both keyboard and gamepad driving. A new `DebugDraw::Vehicles` category draws the suspension, the wheels and their contacts coloured by slip.

**Facts found in the code** (on `dotnet-scripting` and the spike branch, `a5c6d8c`):
- The spike's `VehicleScene.hpp` already uses `DemoFamilyKey::vehicles`, a chase camera writing `DemoCamera`, readouts in names, a P autopilot and smoke keys. While its camera is free, every driving key is off.
- Each spike wheel *is* its scaled cube (the wheel entity's own scale sizes it), with a stripe child to show spin.
- The DemoApp has three models: a cube, a sphere and the backpack. There is no cylinder.
- `DebugDraw` has no vehicles bit. "What does C# get for vehicles?" reserves `DebugDraw.Vehicles`.
- There is no gamepad support anywhere in the engine or the DemoApp. Input reaches scenes only as `KeyboardKeyEvent`s. GLFW, which the DemoApp uses, has `glfwGetGamepadState` with a standard layout.
- `CSharpScenes` ids run to 3 (`characterId`).

### Decisions

1. **The scenes, and the spike's fate.** F6 steps Default → "Vehicles (C++)" → "Vehicles (C#)" → Default. Without scripting, F6 toggles Default ⇄ Vehicles (C++).
   - "Vehicles (C++)" is the spike's `VehicleScene.hpp` rewritten against the final API, with the spike's guesses dropped. It keeps the spike's track: the ten bumps, the 2 m ramp, the ice (0.05) and grass (0.3) lanes, the wall of 20 kg crates and the 15° parking slope, on tarmac (0.5). It keeps the spike's three vehicles: the car towing a trailer, the motorcycle and the tank. Tab switches between them.
   - "Vehicles (C#)" is built only from C#. C++ places a `CSharpScene` marker with id 4 (`CSharpScenes::vehiclesId`), as in ticket 29.
2. **Camera and the keyboard.**
   - Both scenes have a chase camera behind the driven vehicle (or the character on foot) that writes `DemoCamera`, which `DemoCameras` applies, as in ticket 39. The C++ camera is the spike's. The C# scene has its own C# chase camera.
   - C frees the camera. WASD then flies it, and the arrows still drive (or walk), matching the character scenes. C again snaps it back. F1 works as usual.
   - Q/E are the shift keys, not camera orbit keys, because the chase camera turns with the vehicle.
3. **Readouts.** In both scenes, in the same format, every frame:
   - **The driven chassis's `Name`:**
     - speed in km/h (the demo converts; the API stays m/s);
     - RPM, gear, "shifting" while set, and the clutch;
     - wheels on ground;
     - "flipped" while decision 9's test holds.

     The motorcycle adds its lean, the tank its two track speeds. A trailer shows only speed and wheels on ground.
   - **Each wheel of the driven vehicle:** the ground's `Name` or "in air", compression, longitudinal and lateral slip, and "sliding" while a slip is past the tyre's `slideSlip`.
   - L logs the full `state` of the driven vehicle and all its wheels.
   - Parked vehicles keep their last names.
4. **`DebugDraw::Vehicles`**, in FenrirPhysics and its C# mirror (`DebugDraw.Vehicles`, the same bit), is included in `All`. F2's cycle gains "vehicles" before "all". It draws at step poses, for each wheel of each active, enabled vehicle:
   - **the suspension:** a line from the top of the travel to the full-extension point (`position`), with a short cross-tick at the current wheel centre;
   - **the wheel:** a circle of its radius in the wheel's plane at its step pose, with one spoke so spin and steer show;
   - **the contact, while on ground:** a cross at the point and a line along the normal, coloured by grip: gripping, past `peakSlip`, past `slideSlip`. These are new `DebugColour`s.

   Probe shapes are not drawn; the circle stands in for them. Disabled and inactive vehicles draw nothing, and `Colliders` still draws the chassis.
5. **Wheel meshes.** A wheel entity stays unscaled and carries only `VehicleWheel` and the `Transform` physics writes. Its look is a child: a sphere scaled to (width, 2 × radius, 2 × radius) as the tyre, plus a cube stripe to show spin. A radius change rescales the child. Both scenes do this, as do the C# docs. Together with ticket 46's lesson 5 (an unscaled chassis with a scaled look child; wheel entities not children of the chassis), the spec never has to say whether physics keeps a wheel entity's scale.
6. **Getting in and out (C++ only)**, as demo code following the spec's recipe:
   - The scene starts with the driver character seated in the car.
   - **Getting out (F):** the character is unparented, placed beside the driver's door, cleared with `Depenetrate` and re-enabled. The chase camera follows it, and it walks with a small copy of the character scene's steering (WASD relative to the camera, Shift to run, Space to jump, no crouch).
   - **Getting in (F):** within 3 m of any vehicle, the character is disabled and parented to that vehicle's seat child, and that vehicle becomes the driven one.
   - Tab still switches vehicles directly, moving the seated driver.
   - While nobody is driving, every vehicle gets handbrake 1. Parked vehicles always do.
7. **What "Vehicles (C++)" adds to the spike:**
   - **Pedestrian:** a standing character on the start straight. A car driven into it stops at the hull, and its wheels never climb it. The autopilot avoids it.
   - **Ferry:** a slow kinematic platform (2 m/s) shuttling across a gap. A parked vehicle rides it through grip, and one driving on it still steers.
   - **Flip reset:** decision 9.
   - **Trailer brakes:** the car's `brake` and `handbrake` are copied into the trailer's `input` every tick, as the recipe says.
   - **Rear-wheel drive:** the car gets a rear `VehicleDifferential` and front and rear `VehicleAntiRollBar`s, so key 8 has something to show.
   - **Edge-case keys kept from the spike:** 1 soft springs (live), 2 big wheels (live), 3 the motorcycle's max lean (a rebuild that keeps RPM and gear), 4 `enabled`, 5 `interpolate`, 6 the lean controller, M manual gearbox, E/Q shift, Shift clutch.
   - **New edge-case keys:**
     - 7 toggles `maxTilt` between off and 60°.
     - 8 destroys the car's rear-left wheel, and pressing it again makes a new one: a rebuild each time, and one dead-name warning from the rear differential, which keeps driving the remaining rear wheel.
     - 9 toggles the car's chassis to kinematic and back: inactive with one warning, then built fresh at idle in neutral.
8. **What "Vehicles (C#)" shows**: the list from "What does C# get for vehicles?", on a small track of its own (bumps, a grass patch, a ramp).
   - **One C# system builds everything:** the car's chassis, four wheel entities linked by `EntityLink`, a rear `VehicleDifferential`, two `VehicleAntiRollBar`s, and a `TrailerVehicle` on a ball `Joint`. The trailer's brake is copied from the car each tick.
   - **Driving is field writes through `Input`.** M swaps the gearbox to Manual through the kind's view, and E/Q shift.
   - **Readouts** follow decision 3. The grass patch's name shows through the wheel's `GroundCollider`.
   - **V** toggles `DebugDraw.Vehicles` through `SetDebugDraw`.
   - **K and R** kick the car over and right it, as in C++.
   - **U** (C# only) swaps the trailer's kind by assigning a new `VehicleKind`: trailer ⇄ wheeled with its default engine, given the car's `forward` so it pushes. This exercises union assignment.

   It has no motorcycle, tank, getting in and out, pedestrian or ferry: C# reaches those unchanged.
9. **Flipped vehicles.** The demo marks a vehicle "flipped" once `state.wheelsOnGround` has been 0 for 2 s with the chassis's up more than 60° from world up. R rights the driven vehicle by the recipe: a `Transform` write upright, lifted 1 m, with its velocities zeroed. K kicks it over with a roll torque, so R can be tested. With 7 on, K can't roll the car past 60°.
10. **Gamepad driving: demo code only.** Input mapping stays out of the vehicles spec. The engine gains nothing.
    - **The component:** the DemoApp polls the first connected GLFW gamepad each frame and writes a demo `DemoGamepad { int Connected; Vec2 LeftStick; Vec2 RightStick; float LeftTrigger; float RightTrigger; uint32 Buttons; }` (`Demo.DemoGamepad`, registered in the demo's scripting extension) on an entity in the active scene. This is `DemoCamera`'s pattern turned around: C++ owns the device and C# reads it.
    - **Both vehicle scenes read it,** and keyboard or pad both work. The pad's value wins whenever it is beyond a deadzone of about 0.15. Analog values go straight into `VehicleInput`:

      | Pad | Driving | On foot (C++) |
      |---|---|---|
      | Right trigger | `forward` | Not used |
      | Left trigger | `brake`, then reverse once stopped (as S does) | Not used |
      | Left stick X | `steer` | Walk (both axes) |
      | A | Handbrake | Jump |
      | Left / right bumper | Shift down / up | Not used |
      | Y | Get out (C++) | Get in |
      | Back | Right the vehicle | Not used |
      | Right stick | Orbit the chase camera, springing back behind on release | Same |

    - Unplugging mid-drive falls back to the keyboard with no stuck input.
    - The other scenes ignore the pad. The character scenes could adopt it later, but that is not part of this spec.
11. **"Vehicles (C++)" keys:**

    | Key | Driving | On foot |
    |---|---|---|
    | W / S, arrows | Throttle; S brakes, then reverses once stopped (the arrows also work while the camera is free) | Walk, relative to the camera |
    | A / D | Steer (the tank pivots when stopped) | Walk |
    | Space | Handbrake | Jump |
    | Shift | Hold the clutch | Run |
    | F | Get out | Get in the vehicle within 3 m |
    | Tab | Switch car / motorcycle / tank, moving the driver | Not used |
    | T | Teleport the driven vehicle to the start | Teleport the character to the start |
    | R / K | Right the vehicle / kick it over | Not used |
    | C | Free the camera, and re-attach it | Same |
    | M, E / Q | Manual gearbox, shift up / down | Not used |
    | 1–6 | Soft springs, big wheels, max lean (rebuild), `enabled`, `interpolate`, lean controller | Not used |
    | 7 / 8 / 9 | `maxTilt` 60°, destroy and remake the rear-left wheel, chassis kinematic (aimed at the car) | Same |
    | L | Log the state | Same |
    | P | Autopilot (gets the driver back into the car first) | Same |
12. **"Vehicles (C#)" keys.** The same letters do the same jobs as in C++: WASD and the arrows, Space, Shift, C, T, R, K, M, E/Q, L and P. Its own keys are V (`DebugDraw.Vehicles`) and U (swap the trailer's kind).
13. **Autopilots and smoke runs.** Both scenes have a P autopilot, because smoke runs can only tap.
    - **C++ route:** the spike's lap (bumps, ramp, ice and grass, back to the start), extended over the ferry. It avoids the pedestrian.
    - **C# route:** a lap of its own track.
    - **Smoke keys:** each scene presses P, about 40 L presses while it drives, then P again. It then taps each scene key twice, to toggle it there and back. K, R, T and F are pressed once each, in the order K, R, F, F (out and back in), T.
    - `FENRIR_DEMO_SCENE` selects each scene as before.
    - Smoke runs can't press the gamepad. The spec states this.
14. **Hand checks** (the spec's list for the engine developer):
    - **Switching and smoke runs:**
      - F6 steps Default → Vehicles (C++) → Vehicles (C#) → Default, logging each scene's key help.
      - The F2 mode carries across a switch, and "vehicles" draws the suspension, the wheel circles and contacts coloured by slip.
      - `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each scene with no errors, in Debug and Release. The only warnings are the expected one for each of 8 and 9.
      - Without scripting, F6 toggles Default ⇄ Vehicles (C++).
    - **Camera (both scenes):** it chases the vehicle. C frees it (WASD flies, the arrows still drive), and C again snaps it back.
    - **Wheels:** the meshes sit on their axles, spin (the stripe), steer and compress over the bumps. With 5 off, they still move with the chassis.
    - **Gamepad (both scenes, with a pad connected):** analog throttle, brake and steer drive. Unplugging mid-drive falls back to the keyboard with no stuck input.
    - **Vehicles (C++):**
      - **Car:** it reaches about 100 km/h on the straight and parks on the 15° slope with Space without creeping. On ice its wheels spin ("sliding", contacts coloured past `slideSlip`), and grass is looser than tarmac. M with E/Q shifts by hand.
      - **Trailer:** towed over the ramp, it brakes with the car.
      - **Motorcycle:** it stands at rest and leans into turns. With 6 off, it falls over. 3 changes the max lean without losing RPM or gear.
      - **Tank:** it pivots on the spot.
      - **Crates:** the car shoves them aside, with the chassis dip as stated.
      - **Pedestrian:** it stops the car at the hull, and the wheels never climb it.
      - **Ferry:** a parked vehicle rides it.
      - **Getting in and out:** F gets out beside the door, not inside anything, and the character walks. F near the tank gets in and drives it. Tab moves the driver.
      - **Flip and tilt:** K flips the car, "flipped" shows, and R rights it. With 7 on, K can't roll it past 60°.
      - **Live edits:** 1 and 2 change the ride height live. With 4 off, the car is a plain rolling box.
      - **Wheel destroyed:** 8 gives one warning, and the car stays rear-wheel drive on its one rear wheel. 8 again restores it.
      - **Kinematic:** 9 gives one warning, and on return the car is at idle in neutral.
      - **Readouts:** the names show all of decision 3 live.
    - **Vehicles (C#):**
      - **Build:** C# builds the scene with no warning. The car drives, and its trailer brakes with it.
      - **Gears and readouts:** M and E/Q shift by hand. The readouts match the C++ format, and the grass shows in the wheel's readout.
      - **Debug lines:** V toggles them.
      - **Flip:** K and R work as in C++.
      - **Trailer swap:** U turns the trailer into a wheeled car that pushes, and U again turns it back into a trailer.

**No ADR:** the demo scenes, the debug-line category and `DemoGamepad` are all easy to reverse. **Glossary:** no new terms. `DemoCamera`, `DemoGamepad`, demo markers and family keys are DemoApp vocabulary, as ticket 29 ruled, and the vehicle terms are already defined. **Fog:** nothing new. Gamepad input in the other scenes is not a spec question, and an engine input-mapping layer stays outside FenrirPhysics.
