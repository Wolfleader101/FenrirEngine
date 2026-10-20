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

**Status:** resolved

- [x] `FenrirPhysicsTests`: softer springs lower the chassis; a bigger radius raises it; a new `maxSteerAngle` steers on the next tick; a higher `maxBrakeTorque` stops sooner; `handbrake` locks the wheels that have `maxHandbrakeTorque`.
- [x] Caster, camber and toe show in the wheel poses, mirrored by side, and not mirrored at x = 0.
- [x] Probe: each of Ray, Sphere and Cylinder drives on flat ground; a live probe change takes effect without a rebuild; the cylinder rolls over a kerb a ray catches on.
- [x] `maxTilt` at 60° stops a roll a kicked car would otherwise make; π lets it roll.
- [x] Each out-of-range wheel value is corrected with one warning per wheel.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] 1 and 2 change the ride height live; 7 toggles the tilt limit (checked by hand).

**Resolved by:** PR #54 (branch claude/vehicles-wheel-settings-988548), merged into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[vehicles]` cases in `Tests/VehicleSettings.cpp` and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run; the last box (driving keys 1, 2 and 7 by hand) is left for a person. Choices the ticket left open:
- The backend gained `UpdateVehicleWheel` (one wheel's settings re-filled into the vehicle's own `WheelSettingsWV`), `SetVehicleProbe` and `SetVehicleMaxTilt`, apart from ticket 02's `UpdateVehicle` for the kind. Physics diffs each wheel's settings bit for bit against what it last applied, as joints do. The wheel's position is compared with the chassis's world scale applied, so a scale change is patched the same way (ticket 05's scale box gets this for free).
- Sign conventions: + caster tilts the top of the steering axis back (+Z), + camber tilts the wheel's top outwards, + toe turns its front inwards (toe-in). Camber and toe are mirrored for x < 0; a wheel at x = 0 counts as a right-hand one. Caster has no side to mirror. The suspension runs down the steering axis, so with caster a wheel rises up and back.
- The collision tester is remade when the probe changes, when the chassis's object layer changes (in the backend's `Relayer`, so a released layer index is never probed with), and when gravity changes (ray and sphere hold the world's up). The sphere takes the vehicle's smallest wheel radius, and is resized when a radius changes.
- Out-of-range values warn once per wheel for each kind of problem, as joints warn per kind; the warnings are forgotten when the wheel's component is removed. Beyond the spec's list, negative brake and handbrake torques, angular damping, preload and spring damping become 0, a negative peak slip becomes 0, and a slide slip not past the peak is moved just past it, each warned about too. `maxTilt` is kept within 0 to π silently. A wheel inertia of 0 (written, or corrected from negative) reaches the backend as 0.001 kg m², because Jolt divides by it (an assert in MSVC Debug).
- The `maxTilt` test throws the car up as it rolls it: on the ground, the wheels on the low side stop a kick's roll even with no limit. The kerb test uses a car with a deep, low chassis (0.17 m off the ground) whose front wheels stick out past its nose, so a ray car's nose meets the 0.3 m kerb before its ray reaches it; with less overlap, the car squatting under throttle let MSVC's ray car hop the kerb.
