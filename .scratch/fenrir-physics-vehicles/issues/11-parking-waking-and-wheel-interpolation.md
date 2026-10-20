# 11: Parking, waking and wheel interpolation

**What to build:** A parked car never creeps and falls asleep, pressing the throttle always drives, and wheels are drawn smoothly with the chassis at any frame rate (spec: "the step in `PostTick`" (sleep and waking), "state" (asleep), "wheel poses and interpolation" (interpolation)).

- **Waking:** any edit to any vehicle component (`Vehicle`, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar`), any input change, or a rebuild wakes the chassis (ticket 01 wired input). The backend then keeps it awake while forward input is held or the engine is above idle.
- **Parking** is the handbrake plus sleep; no anti-creep helper. A sleeping vehicle keeps its last `state`, with spin 0.
- **Interpolation** follows the chassis's `RigidBody::interpolate`, with no flag of its own, under `PhysicsConfig`'s app-wide switch: compression, steer and spin (angle unwrapped) blended in chassis space between the last two steps, then composed with the chassis's blended pose in `AfterTicks`; `BeforeTicks` restores the true pose (the spike skipped this). Teleports and rebuilds snap it.
- **Docs:** the stated cost (registering any vehicle stops the backend skipping an all-asleep world's update).
- **DemoApp (C++):** the spike's 15° parking slope; 5 toggles the car's `interpolate`.

**Blocked by:** 03 (Wheel settings, probe and tilt limit); 04 (Differentials and anti-roll bars).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: input and an edit to each vehicle component wake a sleeping chassis.
- [ ] A handbraked car on 10° creeps under 1 mm and sleeps; asleep, it keeps its `state` with spin 0.
- [ ] At frames of 0.3 ticks, the blended wheel pose sits on its axle relative to the blended chassis within 1e-4; `BeforeTicks` restores the true pose; with `interpolate` off the wheels take step poses.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The car parks on the 15° slope with Space without creeping; with 5 off the wheels still move with the chassis (checked by hand).
