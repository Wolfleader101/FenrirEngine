# 16: DemoApp Vehicles (C#) scene

**What to build:** A DemoApp scene built entirely by a C# script shows the C# vehicle surface working live, with its own C# chase camera (spec: "DemoApp: the two F6 scenes, the chase camera and the gamepad" ("Vehicles (C#)", camera, readouts, gamepad, autopilots)). C++ places only the `CSharpScene` marker with id 4 (`CSharpScenes::vehiclesId`) and registers the scene after Vehicles (C++), so F6 steps Default → Vehicles (C++) → Vehicles (C#) → Default; without scripting F6 still toggles Default ⇄ Vehicles (C++).

- **Track:** a small one of its own: bumps, a grass patch and a ramp.
- **One C# system builds everything:** the car's unscaled chassis with a scaled look child, four unscaled wheel entities linked by `EntityLink` (not children of the chassis), a rear `VehicleDifferential`, two `VehicleAntiRollBar`s, and a `TrailerVehicle` on a ball `Joint` whose brake is copied from the car each tick.
- **Camera:** a C# chase camera system writes the scene's `DemoCamera`; C frees it and snaps back.
- **Driving** is field writes through `Input`, from the keyboard or `DemoGamepad` (ticket 14) with the same deadzone and mapping; M swaps the gearbox to Manual through `Kind.Wheeled`, E/Q shift.
- **Readouts** in the C++ format every frame, the grass patch's name showing through the wheel's `GroundCollider`; L logs the full state.
- **Keys:** the same letters for the same jobs as C++ (WASD and arrows, Space, Shift, C, T, R, K, M, E/Q, L, P), plus V toggling `DebugDraw.Vehicles` through `SetDebugDraw`, and U swapping the trailer's kind by assigning a new `VehicleKind` (trailer ⇄ wheeled with its default engine, given the car's `forward` so it pushes).
- **Autopilot:** P drives a lap of this track. The smoke run presses P, about 40 L presses while it drives, then P again; taps each scene key twice; then K, R and T once each.
- No motorcycle, tank, getting in and out, pedestrian or ferry.

**Blocked by:** 14 (DemoApp gamepad driving); 15 (Vehicles in C#).

**Status:** resolved

- [x] `FENRIR_DEMO_SCENE="Vehicles (C#)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys with no errors, in Debug and Release.
- [ ] F6 steps Default → Vehicles (C++) → Vehicles (C#) → Default, logging each scene's key help; without scripting F6 toggles Default ⇄ Vehicles (C++) (checked by hand).
- [ ] C# builds the scene with no warning; the car drives and its trailer brakes with it; M and E/Q shift by hand; the readouts match the C++ format and the grass shows in the wheel's readout (checked by hand).
- [ ] V toggles the debug lines; K and R work as in C++; U turns the trailer into a wheeled car that pushes, and U again turns it back; the pad drives as in C++ (checked by hand).

**Resolved by:** PR #67 (branch claude/vehicles-csharp-scene), stacked on ticket 15's PR #65 (branch claude/vehicles-in-csharp). The proof is the `FENRIR_DEMO_SCENE="Vehicles (C#)" FENRIR_DEMO_KEYS=1` smoke run, clean in Debug and Release: the autopilot laps the track (the wheels read "Grass" on the patch), then 40 L presses, P, each key twice and K, R and T. The by-hand boxes are left for a person. Findings and choices:
- **F6 order:** the motorcycle and tank have C++ scenes of their own, so Vehicles (C#) is registered last in the family: Default → Vehicles (C++) → Motorcycle (C++) → Tank (C++) → Vehicles (C#) → Default. Left for the user to confirm.
- **Looks are entities of their own,** moved onto their targets every frame, because C# cannot parent (Relationship is read-only), as Character (C#)'s look is.
- **R and K follow the spec's recipe** in C# (ticket 12 was not merged): flipped after 2 s with no wheel down and the chassis's up more than 60° from world up; R is an upright Transform write lifted 1 m with velocities zeroed, the trailer put back behind; K is an upward and a roll impulse.
- **P off parks the car** (brake 1 until a drive key or the pad): let go in neutral, it rolled off the tarmac.
- **F2 now sets the app's debug-line mode on F2 and on a scene switch only** (`PhysicsDemo::DebugLines`), so V's `SetDebugDraw` from C# is not undone the next frame.
- The hitch's frames are the hitch's offset from each unturned, unscaled body, so the scene builds in one pass.
