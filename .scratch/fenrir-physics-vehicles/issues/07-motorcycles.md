# 07: Motorcycles

**What to build:** A motorcycle stands at rest and leans into turns, with a switch to let it fall (spec: "FenrirPhysics: settings and defaults" (kinds: `MotorcycleVehicle`, `MotorcycleLean`; live edits), "state" (`leanAngle`), "the step in `PostTick`" (rebuilds), "the Jolt backend" (motorcycle lean damping)).

- **`MotorcycleVehicle`** joins `VehicleKind` with the spec's engine, transmission and `limitedSlipRatio` defaults and `MotorcycleLean { enabled, maxAngle, limitSteering }`. Its doc comment says the backend marks motorcycles as still in development, and that a motorcycle drives both wheels unless given a rear-only `VehicleDifferential`.
- **Backend:** the motorcycle controller. **The lean damping cap** (spike lesson 2): Jolt's lean damping capped privately at half the chassis's roll inertia ÷ the collision-step time, Jolt's spring of 5000 kept. Lean gains stay private.
- **Live:** `lean.enabled` and `limitSteering` live; `maxAngle` a private rebuild carrying over RPM, gear and spin through ticket 05's carry-over.
- **Changing kind** (wheeled ⇄ motorcycle) is an assignment: a rebuild with the same carry-over.
- **State:** `leanAngle` (rad, + right), 0 for other kinds.
- **DemoApp (C++):** the spike's 240 kg motorcycle with a rear-only differential; Tab switches car ⇄ motorcycle (the driven vehicle; ticket 08 adds the tank to the cycle); 3 changes the max lean (a rebuild keeping RPM and gear); 6 toggles the lean controller. The readout adds its lean.

**Blocked by:** 04 (Differentials and anti-roll bars); 05 (Teleports, rebuilds and wheels lost mid-drive).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: the default motorcycle stands at rest; under full steer at 60 km/h it leans about 23°; `limitSteering` caps steer by speed so the lean stays under `maxAngle`; with `lean.enabled = false` it falls over.
- [ ] A light bike (the sample's box, roll inertia 10.4 kg m²) landing from a jump stays stable at 60 Hz (the damping cap).
- [ ] Changing `maxAngle` and changing kind wheeled ⇄ motorcycle keep RPM, gear and surviving spin.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] The motorcycle stands at rest and leans into turns; with 6 off it falls over; 3 changes the max lean without losing RPM or gear (checked by hand).
