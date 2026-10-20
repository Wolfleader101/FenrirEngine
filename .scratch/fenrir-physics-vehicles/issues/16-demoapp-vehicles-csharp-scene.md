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

**Status:** ready-for-agent

- [ ] `FENRIR_DEMO_SCENE="Vehicles (C#)"` with `FENRIR_DEMO_KEYS=1` runs the autopilot and keys with no errors, in Debug and Release.
- [ ] F6 steps Default → Vehicles (C++) → Vehicles (C#) → Default, logging each scene's key help; without scripting F6 toggles Default ⇄ Vehicles (C++) (checked by hand).
- [ ] C# builds the scene with no warning; the car drives and its trailer brakes with it; M and E/Q shift by hand; the readouts match the C++ format and the grass shows in the wheel's readout (checked by hand).
- [ ] V toggles the debug lines; K and R work as in C++; U turns the trailer into a wheeled car that pushes, and U again turns it back; the pad drives as in C++ (checked by hand).
