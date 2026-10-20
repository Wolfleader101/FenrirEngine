# 02: Engine and gearbox, Automatic and Manual

**What to build:** Game code tunes the engine and gearbox as plain fields, live, and can build a stick-shift (spec: "FenrirPhysics: settings and defaults" (engine, gearbox, live edits, out-of-range values), "state" (Automatic and Manual `gear` and `clutch`), "the Jolt backend" (built from the shape fields, input)).

- **`VehicleEngine` and `VehicleTransmission`** on `WheeledVehicle` are live edits applied on the next tick through the backend's writable references: the torque curve rebuilt from `maxTorque`, the RPM range, `peakTorqueRpm` and the two fractions on Jolt's `RPM / maxRpm` axis; forward ratios spaced geometrically from `firstGear` to `topGear` over `gearCount`; one reverse gear from its positive magnitude; the shift and switch fields; `clutchStrength`; `WheeledVehicle::limitedSlipRatio` (between differentials). The engine never stalls.
- **Manual:** `mode = Manual` makes `input.gear` (−1 reverse, 0 neutral, 1..n, clamped) and `input.clutch` apply at once through `VehicleTransmission::Set(gear, clutch)`; `forward` is a pedal, a negative value clamped to 0 (spike lesson 6). The shift and switch fields are read only in Automatic. Switching mode is a live edit.
- **State:** in Automatic `gear` and `clutch` are the gearbox's own choices (dropping to neutral braking to a stop) and `shifting` is set while it shifts; in Manual they echo the clamped input. Any change to `input.gear` or `input.clutch` wakes the chassis.
- **Out of range,** one warning per vehicle: negative torques and inertias treated as 0; `minRpm < maxRpm` enforced, `peakTorqueRpm` and the shift points clamped into the range, shift-down kept below shift-up; `gearCount ≥ 1`; a limited-slip ratio at or below 1 raised to just above it.
- **DemoApp (C++):** M toggles the car's gearbox between Automatic and Manual, E/Q shift up and down in Manual, Shift holds the clutch; the readouts already show gear, "shifting" and the clutch. Smoke keys and the autopilot leg extended.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** resolved

- [x] `FenrirPhysicsTests`: geometric gear spacing measured in Manual (successive ratios ≈ 1.377 for the defaults); Manual `gear` and `clutch` echoed in `state`; a negative Manual `forward` clamped to 0; Automatic shifting up under throttle and down braking, `shifting` seen during a shift, neutral at rest.
- [x] Live edits: a higher `maxTorque` accelerates faster from the next tick; a changed `gearCount` or `topGear` changes top-gear speed; switching Automatic ⇄ Manual mid-drive works.
- [x] Each out-of-range engine and gearbox value is corrected with one warning per vehicle, and a later valid value clears it.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] M then E/Q shift by hand, Shift holds the clutch, and the gear readout follows (checked by hand).

**Resolved by:** PR #53 (branch claude/vehicles-engine-gearbox-41572a), merged into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[vehicle-gearbox]` cases (`Tests/VehicleGearbox.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run; the last box (shifting by hand) is left for a person. Choices the ticket left open:
- Live edits go through the new `Backend::World::UpdateVehicle`, which refills Jolt's engine and gearbox settings in place, keeps the engine's RPM in its new range, moves the engaged gear down to the top one if there are fewer gears now, and sets the limited-slip ratio. Physics keeps the kind as game code gave it to notice edits, and as applied, in range, to clamp input against.
- Switching to Manual mid-shift finishes the automatic shift at once (`FinishShift`): Jolt counts a shift's timers down only in Auto, so `shifting` would otherwise stay set and the chassis never sleep.
- In Manual the gear and clutch are given to Jolt every tick (`VehicleTransmission::Set(gear, 1 - clutch)`), so `state` echoes them from the tick they are set. `input.gear` is clamped to -1 through `gearCount` and `clutch` to 0 through 1.
- Each kind of out-of-range value warns once per vehicle while the vehicle holds one; when the vehicle's kind is next applied without it, it is cleared, so a later out-of-range value warns again. A rebuild (a wheel-set change) does not warn again. Values the spec does not name as out of range (negative times, dampings and `minRpm`, gear ratios at or below 0, `clutchStrength`) are still corrected silently, as in 01. An engine inertia of 0 is raised to 0.001 silently: Jolt divides by it.
- An empty RPM range also moves the peak torque RPM and the shift points that lay in it, so it gives up to four warnings.
- The engine never stalls, so in first gear with the clutch engaged and no throttle the idling engine creeps the car at about 3.4 m/s. The negative-forward test compares forward -1 with 0, and the torque test drives past that speed first: below it the clutch, not the engine's torque, drives the car.
- DemoApp: M switches the mode (Manual starts in the gear the car is in, or first from neutral), E/Q shift by hand, either Shift holds the clutch; in Manual W is the pedal and S brakes. The autopilot's new gearbox leg is a loop to the west in Manual, shifting by hand at 4000 and 2000 RPM with the clutch held briefly; the L presses went from 40 to 70 to cover it.
