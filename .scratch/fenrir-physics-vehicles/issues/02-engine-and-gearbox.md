# 02: Engine and gearbox, Automatic and Manual

**What to build:** Game code tunes the engine and gearbox as plain fields, live, and can build a stick-shift (spec: "FenrirPhysics: settings and defaults" (engine, gearbox, live edits, out-of-range values), "state" (Automatic and Manual `gear` and `clutch`), "the Jolt backend" (built from the shape fields, input)).

- **`VehicleEngine` and `VehicleTransmission`** on `WheeledVehicle` are live edits applied on the next tick through the backend's writable references: the torque curve rebuilt from `maxTorque`, the RPM range, `peakTorqueRpm` and the two fractions on Jolt's `RPM / maxRpm` axis; forward ratios spaced geometrically from `firstGear` to `topGear` over `gearCount`; one reverse gear from its positive magnitude; the shift and switch fields; `clutchStrength`; `WheeledVehicle::limitedSlipRatio` (between differentials). The engine never stalls.
- **Manual:** `mode = Manual` makes `input.gear` (−1 reverse, 0 neutral, 1..n, clamped) and `input.clutch` apply at once through `VehicleTransmission::Set(gear, clutch)`; `forward` is a pedal, a negative value clamped to 0 (spike lesson 6). The shift and switch fields are read only in Automatic. Switching mode is a live edit.
- **State:** in Automatic `gear` and `clutch` are the gearbox's own choices (dropping to neutral braking to a stop) and `shifting` is set while it shifts; in Manual they echo the clamped input. Any change to `input.gear` or `input.clutch` wakes the chassis.
- **Out of range,** one warning per vehicle: negative torques and inertias treated as 0; `minRpm < maxRpm` enforced, `peakTorqueRpm` and the shift points clamped into the range, shift-down kept below shift-up; `gearCount ≥ 1`; a limited-slip ratio at or below 1 raised to just above it.
- **DemoApp (C++):** M toggles the car's gearbox between Automatic and Manual, E/Q shift up and down in Manual, Shift holds the clutch; the readouts already show gear, "shifting" and the clutch. Smoke keys and the autopilot leg extended.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: geometric gear spacing measured in Manual (successive ratios ≈ 1.377 for the defaults); Manual `gear` and `clutch` echoed in `state`; a negative Manual `forward` clamped to 0; Automatic shifting up under throttle and down braking, `shifting` seen during a shift, neutral at rest.
- [ ] Live edits: a higher `maxTorque` accelerates faster from the next tick; a changed `gearCount` or `topGear` changes top-gear speed; switching Automatic ⇄ Manual mid-drive works.
- [ ] Each out-of-range engine and gearbox value is corrected with one warning per vehicle, and a later valid value clears it.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] M then E/Q shift by hand, Shift holds the clutch, and the gear readout follows (checked by hand).
