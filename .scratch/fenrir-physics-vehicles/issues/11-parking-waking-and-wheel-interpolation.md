# 11: Parking, waking and wheel interpolation

**What to build:** A parked car never creeps and falls asleep, pressing the throttle always drives, and wheels are drawn smoothly with the chassis at any frame rate (spec: "the step in `PostTick`" (sleep and waking), "state" (asleep), "wheel poses and interpolation" (interpolation)).

- **Waking:** any edit to any vehicle component (`Vehicle`, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar`), any input change, or a rebuild wakes the chassis (ticket 01 wired input). The backend then keeps it awake while forward input is held or the engine is above idle.
- **Parking** is the handbrake plus sleep; no anti-creep helper. A sleeping vehicle keeps its last `state`, with spin 0.
- **Interpolation** follows the chassis's `RigidBody::interpolate`, with no flag of its own, under `PhysicsConfig`'s app-wide switch: compression, steer and spin (angle unwrapped) blended in chassis space between the last two steps, then composed with the chassis's blended pose in `AfterTicks`; `BeforeTicks` restores the true pose (the spike skipped this). Teleports and rebuilds snap it.
- **Docs:** the stated cost (registering any vehicle stops the backend skipping an all-asleep world's update).
- **DemoApp (C++):** the spike's 15° parking slope; 5 toggles the car's `interpolate`.

**Blocked by:** 03 (Wheel settings, probe and tilt limit); 04 (Differentials and anti-roll bars).

**Status:** resolved

- [x] `FenrirPhysicsTests`: input and an edit to each vehicle component wake a sleeping chassis.
- [x] A handbraked car on 10° creeps under 1 mm and sleeps; asleep, it keeps its `state` with spin 0.
- [x] At frames of 0.3 ticks, the blended wheel pose sits on its axle relative to the blended chassis within 1e-4; `BeforeTicks` restores the true pose; with `interpolate` off the wheels take step poses.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The car parks on the 15° slope with Space without creeping; with 5 off the wheels still move with the chassis (checked by hand).

**Resolved by:** PR #59 (branch claude/determined-curran-65d409), merged into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[parking]` cases (`Tests/VehicleParking.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, whose autopilot now parks halfway up the slope for 5 s and taps 5 twice. The last box (watching the car park and the wheels with 5 off by hand) is left for a person. Findings and choices:
- Waking was already wired by tickets 01–05: every live edit, relink and rebuild either wakes the body in the backend or sets step 2's wake. The tests prove each component. One change: an input change wakes the chassis even when clamping hides it, because step 2 now compares the input as game code wrote it.
- Jolt lets a vehicle fall asleep with its wheels turning below 10°/s. Its wheel update still runs while the body sleeps, with no brake, so a parked car's wheels would keep turning slowly. The backend now stops a sleeping vehicle's wheels after each step, and step 5 keeps the read from the step it fell asleep. A car braked almost to rest and then let go shows the difference: its wheel spin is not 0 without the fix.
- The handbraked car on 10° creeps under the 1 mm the test allows (the spike measured 0 mm).
- Wheel interpolation keeps each wheel's compression, steer and spin from the last two steps, in the chassis's axes. It unwraps the backend's wrapped spin angle to the whole turn nearest the wheel's speed × dt. Both angles are moved by the same whole turns, so they stay small. `AfterTicks` blends them only for a chassis that `Blend` itself blended: interpolated, and not held by game code. It poses them on the chassis's blended pose, and `BeforeTicks` puts back each wheel's true local `Transform`.
- A wheel entity destroyed in `Update`, followed by a frame with no tick, left the blend reading a dead entity before the next drain rebuilt the vehicle. The Release smoke run crashed on key 8 this way. The blend now skips a vehicle with a dead wheel entity until it is rebuilt, and the restore holds entity handles.
- The demo's slope is at x = −80: a 12 m ramp at 15° up to a 10 m platform, and a ramp down. The autopilot's `Waypoint` gains `park` (seconds held on the handbrake), and its leg follows the gearbox loop. The smoke run's L presses go from 240 to 290 to cover the longer lap.
