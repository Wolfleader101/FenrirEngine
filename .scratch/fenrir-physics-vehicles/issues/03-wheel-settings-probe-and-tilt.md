# 03: Wheel settings, probe and tilt limit

**What to build:** Every wheel setting and the vehicle-wide probe and tilt limit are live edits with the spec's units and ranges, so tuning during play costs nothing (spec: "FenrirPhysics: the components" (wheels), "settings and defaults" (suspension, tyres, per-wheel angles, probe, tilt, live edits, out-of-range values), "wheel poses and interpolation" (the composed pose), "the Jolt backend" (wheel settings patched in place)).

- **Suspension and tyres:** `travel`, `spring` and `preload` patched in place in each vehicle's own copy of the wheel settings (Jolt reads them by reference; its construction asserts don't re-run, so physics validates). `travel` 0 or a spring frequency of 0 makes the suspension rigid. Each tyre's friction curve rebuilt from its four numbers, slip angles converted to degrees.
- **Wheel fields:** `radius`, `width`, `inertia`, `angularDamping`, `maxSteerAngle`, `maxBrakeTorque`, `maxHandbrakeTorque` (with `handbrake` wired up) live.
- **Per-wheel angles:** `caster` tilts the suspension and steering axis back, `camber` the wheel's top, `toe` its front; each signed to mean the same on both sides, mirrored by the sign of `position.x`, nothing mirrored at x = 0. The wheel pose composes them between compression and steer.
- **Probe:** `Vehicle::probe` is live: Ray and Sphere use the world's −gravity as up with a fixed 80° slope limit; Cylinder (the default for every kind) has none. Changing it swaps the collision tester before the next step.
- **Tilt:** `Vehicle::maxTilt` is live, the cone around world up past which the chassis can't pitch or roll; π is off.
- **Out of range,** one warning per wheel or vehicle: negative inertias, frictions, travel and widths treated as 0; radius raised to a small minimum; `maxSteerAngle`, `caster`, `camber` and `toe` clamped below π/2.
- Every edit here wakes the chassis.
- **DemoApp (C++):** 1 soft springs (live), 2 big wheels (live; the look child rescales), 7 `maxTilt` off ⇄ 60°. Smoke keys extended.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: softer springs lower the chassis; a bigger radius raises it; a new `maxSteerAngle` steers on the next tick; a higher `maxBrakeTorque` stops sooner; `handbrake` locks the wheels that have `maxHandbrakeTorque`.
- [ ] Caster, camber and toe show in the wheel poses, mirrored by side, and not mirrored at x = 0.
- [ ] Probe: each of Ray, Sphere and Cylinder drives on flat ground; a live probe change takes effect without a rebuild; the cylinder rolls over a kerb a ray catches on.
- [ ] `maxTilt` at 60° stops a roll a kicked car would otherwise make; π lets it roll.
- [ ] Each out-of-range wheel value is corrected with one warning per wheel.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] 1 and 2 change the ride height live; 7 toggles the tilt limit (checked by hand).
