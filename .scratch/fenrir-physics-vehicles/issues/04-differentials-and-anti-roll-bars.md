# 04: Differentials and anti-roll bars

**What to build:** Game code chooses front-, rear- or all-wheel drive and torque splits, and stops a car leaning in corners, with components naming two wheel entities and no arrays (spec: "FenrirPhysics: the components" (`VehicleDifferential`, `VehicleAntiRollBar`, inactive), "settings and defaults" (drives out of the box, differentials), "the step in `PostTick`" (drain and diff), "the Jolt backend" (built from the shape fields)).

- **`VehicleDifferential`** (on any entity: `leftWheel`, `rightWheel`, `ratio`, `leftRightSplit`, `limitedSlipRatio`, `torqueShare`) and **`VehicleAntiRollBar`** (`leftWheel`, `rightWheel`, `stiffness`), declared and documented with the spec's defaults. Their watches drain with the others; edits and relinks are live and wake the chassis.
- **Drives out of the box, replaced:** a wheeled vehicle with no differential naming its wheels keeps ticket 01's all-wheel-drive default (one single-wheel differential per wheel with `VehicleDifferential`'s defaults). Adding any differential replaces that default; a wheel in no differential only rolls and brakes. An empty wheel name is "none", so a rear-only drive is one differential with one wheel. Torque shares are normalised privately per vehicle.
- **No anti-roll bars by default;** `VehicleAntiRollBar` works on any kind with wheels. Tracked and trailer vehicles will ignore differentials (tickets 08 and 09).
- **Inactive** (component kept, no effect, one warning on each transition to invalid, back once valid): a name that is a non-wheel entity, or wheels of two different vehicles. Names that *die* (the entity destroyed or its `VehicleWheel` removed) are ticket 05's.
- **Out of range,** one warning per component: negative stiffness treated as 0; a limited-slip ratio at or below 1 raised to just above it.
- **DemoApp (C++):** the car gets a rear `VehicleDifferential` and front and rear `VehicleAntiRollBar`s.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** resolved

- [x] `FenrirPhysicsTests`: with no differential every wheel is driven; a rear `VehicleDifferential` drives only its two wheels (the front wheels' spin follows the ground); a one-wheel differential drives one wheel; two differentials with shares 3 and 1 split torque as 0.75 and 0.25.
- [x] An anti-roll bar reduces the chassis's roll in a steady turn against the same car without one.
- [x] A differential or anti-roll bar naming a non-wheel or wheels of two vehicles is inactive with one warning and comes back once its names are fixed.
- [x] Out-of-range values corrected with one warning; edits wake a sleeping chassis.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.

**Resolved by:** PR #52 (branch claude/vehicles-differentials-ad7e73), merged into dotnet-scripting. The proof is `FenrirPhysicsTests`' cases in `Tests/VehicleDifferentials.cpp` and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run. Choices the ticket left open:
- Which wheels are driven is tested with the car on a stand under its chassis, its wheels in the air: a driven wheel spins up and an undriven one stays exactly still, whatever the ground does. The 3:1 share test opens the vehicle's limited-slip ratio between differentials (`WheeledVehicle::limitedSlipRatio` at `FLT_MAX`), since a 3:1 speed difference would otherwise move all the torque to the slower differential.
- The backend normalises torque shares (`Fill` in `JoltBackend.cpp`): the last differential takes 1 minus the others' shares, so they sum to exactly 1, and all-zero shares are split equally. Jolt sums only differentials naming a wheel, so physics passes only those. An empty differential list means the all-wheel-drive default, both at creation and through `SetVehicleDifferentials`.
- Physics gives each vehicle its differentials and anti-roll bars by wheel index (`LinksByVehicle` in `src/Vehicles.cpp`), in entity order. Step 2 diffs them against what it last gave the vehicle: a change calls `SetVehicleDifferentials` or `SetVehicleAntiRollBars` and wakes the chassis. Neither rebuilds the vehicle.
- Out of range, one warning per kind of value per component, as for joints: besides the ticket's negative stiffness and limited-slip ratio at or below 1, a ratio at or below 0, a left-right split outside 0 to 1 and a negative torque share are corrected, because Jolt asserts on each.
- A name that is a destroyed entity's stale handle counts as a non-wheel for now, so the component goes inactive; ticket 05 makes dead names empty instead. A differential or anti-roll bar naming a wheel that is itself not one of its vehicle's wheels (an inactive wheel) is quietly inert, since the wheel is what is wrong. An anti-roll bar with a wheel empty is inert.
- The DemoApp car's anti-roll bars use the default 1000 N/m. At 10000 N/m the rear-wheel-drive car rolled over in the bumps under the autopilot.
